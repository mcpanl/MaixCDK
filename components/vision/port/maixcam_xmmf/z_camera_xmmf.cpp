/**
 * Camera implementation for PLATFORM_MAIXCAM using x_lib (X_MMF).
 *
 * Pipeline  (replicates z_lib topology):
 *   VI → VPSS group 1 → channel _ch → user RGB888 frame
 *
 * Channel allocation:
 *   _ch == 0 : primary camera (main user constructor)
 *   _ch == 1 : secondary channel (returned by add_channel)
 *
 * Lifetime rule:
 *   acquire() is called once per Camera open(); release() once per close().
 *   Secondary (_ch==1): close()/destructor drain VPSS ch1 and CVI_VPSS_DisableChn
 *   so add_channel / delete cycles do not leave hardware ch1 running.
 */

#include "z_camera.hpp"
#include "maix_basic.hpp"
#include "maix_cvi_media_runtime.hpp"

extern "C" {
#include "x_mmf.h"
#include "sample_comm.h"
}

#include <cstring>
#include <cstdlib>
#include <algorithm>

#define MMF_SENSOR_NAME  "MMF_SENSOR_NAME"
#define MAIX_SENSOR_FPS  "MAIX_SENSOR_FPS"

static constexpr VPSS_GRP CAM_GRP     = 1;
static constexpr int      CAM_CHN_MAX = 2;

using namespace maix;

namespace maix::camera {

    static bool s_regs_flag = false;

    std::vector<std::string> list_devices()
    {
        log::warn("Camera not driven by device files on MaixCam.");
        return {};
    }

    void set_regs_enable(bool enable) { s_regs_flag = enable; }

    std::string get_device_name() { return "gc2083"; }

    err::Err Camera::show_colorbar(bool enable)
    {
        _show_colorbar = enable;
        return err::ERR_NONE;
    }

    // ── Private helpers ────────────────────────────────────────────────────────

    bool Camera::_check_format(image::Format fmt)
    {
        switch (fmt) {
            case image::FMT_RGB888:
            case image::FMT_BGR888:
            case image::FMT_RGBA8888:
            case image::FMT_BGRA8888:
            case image::FMT_YVU420SP:
            case image::FMT_GRAYSCALE:
                return true;
            default:
                return false;
        }
    }

    static void _config_sensor_env(double fps)
    {
        if (!getenv(MMF_SENSOR_NAME))
            setenv(MMF_SENSOR_NAME, "gc2083", 0);
        if (!getenv(MAIX_SENSOR_FPS)) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", (int)fps);
            setenv(MAIX_SENSOR_FPS, buf, 0);
        }
    }

    /* Resize a VPSS channel while keeping other channels running. */
    static CVI_S32 _vpss_resize_chn(VPSS_GRP grp, VPSS_CHN chn,
                                     CVI_U32 w, CVI_U32 h,
                                     PIXEL_FORMAT_E fmt,
                                     CVI_S32 fps)
    {
        CVI_VPSS_DisableChn(grp, chn);

        VPSS_CHN_ATTR_S attr;
        memset(&attr, 0, sizeof(attr));
        attr.u32Width                     = w;
        attr.u32Height                    = h;
        attr.enVideoFormat                = VIDEO_FORMAT_LINEAR;
        attr.enPixelFormat                = fmt;
        attr.stFrameRate.s32SrcFrameRate  = fps;
        attr.stFrameRate.s32DstFrameRate  = fps;
        attr.u32Depth                     = 0;
        attr.stAspectRatio.enMode         = ASPECT_RATIO_AUTO;
        attr.stAspectRatio.bEnableBgColor = CVI_TRUE;
        attr.stAspectRatio.u32BgColor     = COLOR_RGB_BLACK;

        CVI_S32 ret = CVI_VPSS_SetChnAttr(grp, chn, &attr);
        if (ret != CVI_SUCCESS) {
            CVI_VPSS_EnableChn(grp, chn);
            return ret;
        }
        return CVI_VPSS_EnableChn(grp, chn);
    }

    /* Drop queued frames before DisableChn to reduce driver edge errors. */
    static void _vpss_drain_chn(VPSS_GRP grp, VPSS_CHN chn, int max_frames)
    {
        for (int i = 0; i < max_frames; ++i) {
            VIDEO_FRAME_INFO_S f;
            memset(&f, 0, sizeof(f));
            if (X_MMF_VPSS_GetFrame(grp, chn, &f, 10) != CVI_SUCCESS)
                break;
            X_MMF_VPSS_ReleaseFrame(grp, chn, &f);
        }
    }

    static void _vpss_teardown_out_chn(VPSS_GRP grp, VPSS_CHN chn)
    {
        _vpss_drain_chn(grp, chn, 16);
        (void)CVI_VPSS_DisableChn(grp, chn);
    }

    /* Get one VPSS frame and copy it into a new image::Image (RGB888). */
    static image::Image *_read_rgb888_frame(VPSS_GRP grp, VPSS_CHN chn,
                                             int want_w, int want_h,
                                             int block_ms)
    {
        VIDEO_FRAME_INFO_S frame;
        memset(&frame, 0, sizeof(frame));

        CVI_S32 ret = X_MMF_VPSS_GetFrame(grp, chn, &frame, (CVI_S32)block_ms);
        if (ret != CVI_SUCCESS) {
            log::error("VPSS GetFrame grp=%d chn=%d failed: 0x%x", grp, chn, ret);
            return nullptr;
        }

        VIDEO_FRAME_S *vf = &frame.stVFrame;
        const uint32_t fw = vf->u32Width;
        const uint32_t fh = vf->u32Height;

        if (fw == 0 || fh == 0) {
            X_MMF_VPSS_ReleaseFrame(grp, chn, &frame);
            return nullptr;
        }

        CVI_BOOL need_unmap = CVI_FALSE;
        uint8_t *src = vf->pu8VirAddr[0];
        if (!src) {
            src = (uint8_t *)CVI_SYS_MmapCache(vf->u64PhyAddr[0], vf->u32Length[0]);
            need_unmap = CVI_TRUE;
        }
        if (src) {
            CVI_SYS_IonInvalidateCache(vf->u64PhyAddr[0], src, vf->u32Length[0]);
        }

        int out_w = (want_w > 0) ? want_w : (int)fw;
        int out_h = (want_h > 0) ? want_h : (int)fh;

        image::Image *img = new image::Image(out_w, out_h, image::FMT_RGB888);
        if (img && src) {
            uint8_t *dst    = (uint8_t *)img->data();
            const uint32_t stride    = vf->u32Stride[0];
            const uint32_t row_bytes = (uint32_t)std::min(out_w * 3, (int)(fw * 3));
            for (int y = 0; y < out_h && (uint32_t)y < fh; ++y) {
                memcpy(dst + (size_t)y * out_w * 3,
                       src + (size_t)y * stride,
                       row_bytes);
            }
        }

        if (need_unmap) CVI_SYS_Munmap(src, vf->u32Length[0]);
        X_MMF_VPSS_ReleaseFrame(grp, chn, &frame);

        return img;
    }

    // ── Constructor / Destructor ───────────────────────────────────────────────

    Camera::Camera(int width, int height, image::Format format,
                   const char *device, double fps, int buff_num,
                   bool open, bool raw)
    {
        err::check_bool_raise(_check_format(format), "Format not supported");

        _width         = (width  <= 0) ? 640 : width;
        _height        = (height <= 0) ? 480 : height;
        _format        = format;
        _format_impl   = format;
        _buff_num      = buff_num;
        _fps           = (fps <= 0) ? ((_width <= 1280 && _height <= 720) ? 60 : 30) : fps;
        _ch            = 0;
        _show_colorbar = false;
        _open_set_regs = s_regs_flag;
        _device        = "";
        _last_read_us  = time::ticks_us();
        _invert_flip   = false;
        _invert_mirror = false;
        _is_opened     = false;
        _hmirror       = 0;
        _vflip         = 0;
        _exposure      = 0;
        _gain          = 0;
        _param         = nullptr;

        (void)device; (void)raw;

        if (open) {
            err::Err e = this->open(_width, _height, _format, _fps, _buff_num);
            err::check_raise(e, "camera open failed");
        }
    }

    Camera::~Camera()
    {
        if (is_opened()) {
            close();
        } else if (_ch == 1) {
            /* add_channel(..., open=false): ch1 was enabled but no acquire — still teardown HW. */
            _vpss_teardown_out_chn(CAM_GRP, VPSS_CHN1);
        }
    }

    int Camera::get_ch_nums() { return CAM_CHN_MAX; }
    int Camera::get_channel() { return _ch; }

    // ── open / close ──────────────────────────────────────────────────────────

    err::Err Camera::open(int width, int height, image::Format format,
                          double fps, int buff_num)
    {
        if (_is_opened) return err::ERR_NONE;

        int   w   = (width  <= 0)               ? _width  : width;
        int   h   = (height <= 0)               ? _height : height;
        double f  = (fps    <= 0)               ? _fps    : fps;
        image::Format fmt = (format == image::FMT_INVALID) ? _format : format;
        (void)buff_num;

        err::check_bool_raise(_check_format(fmt), "Format not supported");

        _width  = w;
        _height = h;
        _fps    = f;
        _format = fmt;
        _format_impl = fmt;

        _config_sensor_env(_fps);
        maix::cvi::MediaRuntime::acquire();

        /* Resize the VPSS camera channel to the requested resolution. */
        CVI_S32 ret = _vpss_resize_chn(CAM_GRP, (VPSS_CHN)_ch,
                                        (CVI_U32)_width, (CVI_U32)_height,
                                        PIXEL_FORMAT_RGB_888,
                                        (CVI_S32)_fps);
        if (ret != CVI_SUCCESS) {
            log::warn("VPSS resize chn %d failed 0x%x — using init default", _ch, ret);
        }

        _is_opened = true;
        return err::ERR_NONE;
    }

    void Camera::close()
    {
        if (!_is_opened) return;
        _is_opened = false;
        if (_ch == 1)
            _vpss_teardown_out_chn(CAM_GRP, VPSS_CHN1);
        maix::cvi::MediaRuntime::release();
    }

    // ── read ──────────────────────────────────────────────────────────────────

    image::Image *Camera::read(void * /*buff*/, size_t /*buff_size*/,
                               bool block, int block_ms)
    {
        if (!_is_opened) {
            err::Err e = open(_width, _height, _format, _fps, _buff_num);
            err::check_raise(e, "camera open failed");
        }

        int timeout = block ? (block_ms <= 0 ? 2000 : block_ms) : 50;

        image::Image *img = _read_rgb888_frame(CAM_GRP, (VPSS_CHN)_ch,
                                               _width, _height, timeout);
        if (!img) return nullptr;

        if (_format == image::FMT_RGB888) return img;

        image::Image *out = img->to_format(_format);
        delete img;
        return out;
    }

    image::Image *Camera::read_raw()
    {
        log::error("read_raw not supported in x_mmf backend");
        return nullptr;
    }

    void Camera::clear_buff()
    {
        for (int i = 0; i < 2; ++i) {
            VIDEO_FRAME_INFO_S f;
            memset(&f, 0, sizeof(f));
            if (X_MMF_VPSS_GetFrame(CAM_GRP, (VPSS_CHN)_ch, &f, 50) == CVI_SUCCESS)
                X_MMF_VPSS_ReleaseFrame(CAM_GRP, (VPSS_CHN)_ch, &f);
        }
    }

    void Camera::skip_frames(int num)
    {
        for (int i = 0; i < num; ++i) {
            image::Image *img = this->read();
            delete img;
        }
    }

    bool Camera::is_opened() { return _is_opened; }

    // ── add_channel ───────────────────────────────────────────────────────────

    Camera *Camera::add_channel(int width, int height, image::Format format,
                                 double fps, int buff_num, bool open)
    {
        int   w   = (width  <= 0) ? _width  : width;
        int   h   = (height <= 0) ? _height : height;
        double f  = (fps    <= 0) ? _fps    : fps;
        image::Format fmt = (format == image::FMT_INVALID) ? _format : format;
        (void)buff_num;

        err::check_bool_raise(_check_format(fmt), "Format not supported");

        /* Enable VPSS group 1, channel 1 with the requested size. */
        CVI_S32 ret = _vpss_resize_chn(CAM_GRP, VPSS_CHN1,
                                        (CVI_U32)w, (CVI_U32)h,
                                        PIXEL_FORMAT_RGB_888,
                                        (CVI_S32)f);
        if (ret != CVI_SUCCESS) {
            log::error("add_channel: VPSS chn1 resize failed: 0x%x", ret);
            return nullptr;
        }

        /* Construct a Camera object that bypasses the normal constructor path. */
        Camera *cam = new Camera(w, h, fmt, nullptr, f, 3, /*open=*/false, false);
        cam->_ch = 1;

        if (open) {
            /* Manually mark as open and acquire the runtime reference. */
            maix::cvi::MediaRuntime::acquire();
            cam->_is_opened = true;
        }
        return cam;
    }

    // ── Resolution / FPS ─────────────────────────────────────────────────────

    err::Err Camera::set_resolution(int width, int height)
    {
        if (!_is_opened) return err::ERR_NOT_OPEN;
        CVI_S32 ret = _vpss_resize_chn(CAM_GRP, (VPSS_CHN)_ch,
                                        (CVI_U32)width, (CVI_U32)height,
                                        PIXEL_FORMAT_RGB_888,
                                        (CVI_S32)_fps);
        if (ret != CVI_SUCCESS) return err::ERR_RUNTIME;
        _width  = width;
        _height = height;
        return err::ERR_NONE;
    }

    err::Err Camera::set_fps(double fps)
    {
        log::warn("set_fps not fully supported in x_mmf backend");
        _fps = fps;
        return err::ERR_NONE;
    }

    // ── ISP / sensor controls (stubs matching z_lib behaviour) ───────────────

    int Camera::hmirror(int value)
    {
        log::warn("hmirror not supported");
        if (value != -1) _hmirror = value;
        return _hmirror;
    }

    int Camera::vflip(int value)
    {
        log::warn("vflip not supported");
        if (value != -1) _vflip = value;
        return _vflip;
    }

    int Camera::exposure(int value)
    {
        log::warn("exposure not supported");
        return value;
    }

    int Camera::gain(int value)
    {
        log::warn("gain not supported");
        return value;
    }

    int Camera::luma(int value)
    {
        log::warn("luma not supported");
        return value;
    }

    int Camera::constrast(int value)
    {
        log::warn("constrast not supported");
        return value;
    }

    int Camera::saturation(int value)
    {
        log::warn("saturation not supported");
        return value;
    }

    int Camera::awb_mode(int value)
    {
        log::warn("awb_mode not supported");
        return value;
    }

    int Camera::set_awb(int value)
    {
        log::warn("set_awb not supported");
        return value;
    }

    int Camera::exp_mode(int value)
    {
        log::warn("exp_mode not supported");
        return value;
    }

    err::Err Camera::set_windowing(std::vector<int> roi)
    {
        (void)roi;
        log::warn("set_windowing not supported");
        return err::ERR_NONE;
    }

    std::vector<int> Camera::get_sensor_size()
    {
        return {1920, 1080};
    }

    err::Err Camera::write_reg(int addr, int data, int bit_width)
    {
        (void)addr; (void)data; (void)bit_width;
        return err::ERR_NONE;
    }

    int Camera::read_reg(int addr, int bit_width)
    {
        (void)addr; (void)bit_width;
        return -1;
    }

} // namespace maix::camera
