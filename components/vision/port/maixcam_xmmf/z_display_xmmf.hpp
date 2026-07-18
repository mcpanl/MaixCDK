#pragma once

#include "z_display_base.hpp"
#include "maix_log.hpp"
#include "z_image.hpp"
#include "maix_cvi_media_runtime.hpp"

extern "C" {
#include "x_mmf.h"
#include "sample_comm.h"
}

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace maix;

// VPSS group / channel used for the display pipeline (user RGB888 → VPSS → NV21 → VO).
static constexpr VPSS_GRP  XMMF_DISP_GRP = 0;
static constexpr VPSS_CHN  XMMF_DISP_CHN = VPSS_CHN0;
static constexpr VO_LAYER  XMMF_VO_LAYER = 0;
static constexpr VO_CHN    XMMF_VO_CHN   = 0;

namespace maix::display {

class DisplayCviXmmf final : public DisplayBase {
public:
    DisplayCviXmmf(const std::string &device, int width, int height, image::Format format)
        : _width(width <= 0 ? 640 : width)
        , _height(height <= 0 ? 480 : height)
        , _format(format)
        , _ch(0)
        , _opened(false)
        , _max_backlight(50.0f)
        , _is_channel(false)
    {
        (void)device;
        if (_format != image::FMT_RGB888
            && _format != image::FMT_YVU420SP
            && _format != image::FMT_BGRA8888
            && _format != image::FMT_RGBA8888) {
            throw err::Exception(err::ERR_ARGS, "DisplayCviXmmf: unsupported format");
        }
        maix::cvi::MediaRuntime::acquire();
        _sync_vpss_disp_input();
    }

    /* Secondary channel constructor (add_channel result — OSD layer) */
    DisplayCviXmmf(int /*layer*/, int width, int height, image::Format format)
        : _width(width)
        , _height(height)
        , _format(format)
        , _ch(1)
        , _opened(false)
        , _max_backlight(50.0f)
        , _is_channel(true)
    {
        if (_format != image::FMT_BGRA8888) {
            throw err::Exception(err::ERR_ARGS, "add_channel only supports BGRA8888");
        }
        maix::cvi::MediaRuntime::acquire();
    }

    /* DisplayBase 无 virtual ~DisplayBase()，不可使用 override */
    ~DisplayCviXmmf()
    {
        if (_opened) close();
        maix::cvi::MediaRuntime::release();
    }

    int width()  override { return _width; }
    int height() override { return _height; }
    std::vector<int> size() override { return {_width, _height}; }
    image::Format format() override { return _format; }
    bool is_opened() override { return _opened; }
    int get_ch_nums() override { return 2; }
    void set_backlight(float /*v*/) override {}
    float get_backlight() override { return _max_backlight; }
    err::Err set_hmirror(bool /*en*/) override { return err::ERR_NONE; }
    err::Err set_vflip(bool /*en*/)   override { return err::ERR_NONE; }

    err::Err open(int width, int height, image::Format format) override
    {
        if (_opened) return err::ERR_NONE;
        if (width  > 0) _width  = width;
        if (height > 0) _height = height;
        if (format != image::FMT_INVALID) _format = format;
        _opened = true;
        _sync_vpss_disp_input();
        return err::ERR_NONE;
    }

    err::Err close() override
    {
        _opened = false;
        return err::ERR_NONE;
    }

    DisplayBase *add_channel(int width, int height, image::Format format) override
    {
        int w = (width  <= 0) ? _width  : std::min(width,  _width);
        int h = (height <= 0) ? _height : std::min(height, _height);
        image::Format fmt = (format == image::FMT_INVALID) ? image::FMT_BGRA8888 : format;
        return new DisplayCviXmmf(/*layer=*/1, w, h, fmt);
    }

    err::Err show(image::Image &img, image::Fit fit) override
    {
        if (!_opened) return err::ERR_NOT_OPEN;

        image::Image *tmp_resize = nullptr;
        image::Image *tmp_fmt    = nullptr;
        image::Image *use        = &img;

        const bool size_mismatch = (img.width() != _width || img.height() != _height);
        const bool format_mismatch =
            (img.format() != _format) && !_rgb_rgba_equivalent(img.format(), _format);
        const bool mismatch = size_mismatch || format_mismatch;
        if (mismatch) {
            log::warn(
                "DisplayCviXmmf::show: image %dx%d fmt=%d != screen %dx%d fmt=%d — CPU resize/to_format",
                img.width(), img.height(), (int)img.format(), _width, _height, (int)_format);
            /* Resize in source pixel format first, then convert at target size (better for YUV paths). */
            if (img.width() != _width || img.height() != _height) {
                tmp_resize = img.resize(_width, _height, fit);
                if (!tmp_resize) {
                    log::error("DisplayCviXmmf::show: resize failed");
                    return err::ERR_RUNTIME;
                }
                use = tmp_resize;
            }
            if ((use->format() != _format) && !_rgb_rgba_equivalent(use->format(), _format)) {
                tmp_fmt = use->to_format(_format);
                if (!tmp_fmt) {
                    if (tmp_resize) {
                        delete tmp_resize;
                    }
                    log::error("DisplayCviXmmf::show: to_format failed");
                    return err::ERR_RUNTIME;
                }
                if (tmp_resize) {
                    delete tmp_resize;
                    tmp_resize = nullptr;
                }
                use = tmp_fmt;
            }
        }

        if ((use->width() % 2 != 0) || (use->height() % 2 != 0)) {
            if (tmp_fmt) {
                delete tmp_fmt;
            } else if (tmp_resize) {
                delete tmp_resize;
            }
            return (err::Err)err::ERR_ARGS;
        }

        err::Err ret = _push_normalized_frame(*use);
        if (tmp_fmt) {
            delete tmp_fmt;
        } else if (tmp_resize) {
            delete tmp_resize;
        }
        return ret;
    }

    err::Err poll_events() override { return err::ERR_NONE; }

private:
    int           _width;
    int           _height;
    image::Format _format;
    int           _ch;
    bool          _opened;
    float         _max_backlight;
    bool          _is_channel;

    /** RGB888 vs RGBA8888: show path drops alpha; treat as same for skip CPU to_format. */
    static bool _rgb_rgba_equivalent(image::Format a, image::Format b)
    {
        if (a == b) {
            return true;
        }
        return (a == image::FMT_RGB888 && b == image::FMT_RGBA8888)
            || (a == image::FMT_RGBA8888 && b == image::FMT_RGB888);
    }

    static CVI_U32 _vpss_even_dim(int v)
    {
        if (v <= 0) {
            return 2;
        }
        CVI_U32 u = (CVI_U32)v;
        return (u + 1u) & ~1u;
    }

    /** Pixel format VPSS group 0 expects for SendFrame (must match VIDEO_FRAME_S). */
    static PIXEL_FORMAT_E _cvi_vpss_src_pixfmt(image::Format f)
    {
        switch (f) {
        case image::FMT_YVU420SP:
            return PIXEL_FORMAT_NV21;
        case image::FMT_BGRA8888:
            return PIXEL_FORMAT_ARGB_8888;
        case image::FMT_RGBA8888:
            /* show() converts RGBA → packed RGB888 before SendFrame */
            return PIXEL_FORMAT_RGB_888;
        case image::FMT_RGB888:
        default:
            return PIXEL_FORMAT_RGB_888;
        }
    }

    void _sync_vpss_disp_input()
    {
        if (_is_channel) {
            return;
        }
        CVI_U32 mw = _vpss_even_dim(_width);
        CVI_U32 mh = _vpss_even_dim(_height);
        CVI_S32 r  = maix::cvi::MediaRuntime::configure_display_vpss_input(mw, mh, _cvi_vpss_src_pixfmt(_format));
        if (r != CVI_SUCCESS) {
            log::warn("DisplayCviXmmf: configure_display_vpss_input failed: 0x%x", r);
        }
    }

    void _ensure_vpss_src_max(CVI_U32 w, CVI_U32 h, PIXEL_FORMAT_E pix)
    {
        if (w > _vpss_even_dim(_width) || h > _vpss_even_dim(_height)) {
            log::warn("DisplayCviXmmf: frame %ux%u exceeds VPSS max %dx%d — updating grp", w, h, _width, _height);
            (void)maix::cvi::MediaRuntime::configure_display_vpss_input(_vpss_even_dim((int)w), _vpss_even_dim((int)h), pix);
        }
    }

    err::Err _push_normalized_frame(image::Image &im)
    {
        switch (im.format()) {
        case image::FMT_RGB888:
            return _push_rgb888((const uint8_t *)im.data(), im.width(), im.height());
        case image::FMT_RGBA8888: {
            int w = im.width();
            int h = im.height();
            size_t rgb_size = (size_t)w * (size_t)h * 3u;
            uint8_t *rgb_data = (uint8_t *)malloc(rgb_size);
            if (!rgb_data) {
                log::error("DisplayCviXmmf: malloc failed for RGBA→RGB");
                return err::ERR_RUNTIME;
            }
            const uint8_t *rgba = (const uint8_t *)im.data();
            for (int i = 0; i < w * h; i++) {
                rgb_data[i * 3 + 0] = rgba[i * 4 + 0];
                rgb_data[i * 3 + 1] = rgba[i * 4 + 1];
                rgb_data[i * 3 + 2] = rgba[i * 4 + 2];
            }
            err::Err e = _push_rgb888(rgb_data, w, h);
            free(rgb_data);
            return e;
        }
        case image::FMT_BGRA8888:
            return _push_bgra8888(im);
        case image::FMT_YVU420SP:
            return _push_nv21_packed((const uint8_t *)im.data(), im.width(), im.height());
        default:
            log::error("DisplayCviXmmf::show: unsupported normalized format %d", (int)im.format());
            return err::ERR_ARGS;
        }
    }

    /* Push an RGB888 frame: ION alloc → VPSS group 0 → NV21 → VO (via binding). */
    err::Err _push_rgb888(const uint8_t *rgb, int w, int h)
    {
        const CVI_U32 stride    = (CVI_U32)(((w * 3) + 63u) & ~63u);
        const CVI_U32 frameSize = stride * (CVI_U32)h;

        CVI_U64   phyAddr = 0;
        CVI_VOID *virAddr = nullptr;
        CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &virAddr, "disp_rgb", frameSize);
        if (ret != CVI_SUCCESS) {
            log::error("disp ION alloc failed: 0x%x", ret);
            return err::ERR_RUNTIME;
        }

        /* Copy row-by-row if stride != packed width. */
        if ((CVI_U32)(w * 3) == stride) {
            memcpy(virAddr, rgb, (size_t)w * h * 3);
        } else {
            for (int y = 0; y < h; ++y) {
                memcpy((uint8_t *)virAddr + (size_t)y * stride,
                       rgb + (size_t)y * w * 3,
                       (size_t)w * 3);
            }
        }
        CVI_SYS_IonFlushCache(phyAddr, virAddr, frameSize);

        VIDEO_FRAME_INFO_S srcFrame;
        memset(&srcFrame, 0, sizeof(srcFrame));
        VIDEO_FRAME_S *vf    = &srcFrame.stVFrame;
        vf->u32Width         = (CVI_U32)w;
        vf->u32Height        = (CVI_U32)h;
        vf->enPixelFormat    = PIXEL_FORMAT_RGB_888;
        vf->enVideoFormat    = VIDEO_FORMAT_LINEAR;
        vf->u32Stride[0]     = stride;
        vf->u32Length[0]     = frameSize;
        vf->u64PhyAddr[0]    = phyAddr;
        vf->pu8VirAddr[0]    = (CVI_U8 *)virAddr;

        _ensure_vpss_src_max((CVI_U32)w, (CVI_U32)h, PIXEL_FORMAT_RGB_888);

        {
            std::lock_guard<std::mutex> send_lk(maix::cvi::MediaRuntime::display_vpss0_send_mutex());
            ret = CVI_VPSS_SendFrame(XMMF_DISP_GRP, &srcFrame, -1);
        }
        CVI_SYS_IonFree(phyAddr, virAddr);   // input buffer no longer needed

        if (ret != CVI_SUCCESS) {
            log::error("CVI_VPSS_SendFrame failed: 0x%x", ret);
            return err::ERR_RUNTIME;
        }

        /* Retrieve and immediately release the VPSS output (already sent to VO
         * automatically via the VPSS→VO binding set up in MediaRuntime::_init). */
        // VIDEO_FRAME_INFO_S outFrame;
        // ret = CVI_VPSS_GetChnFrame(XMMF_DISP_GRP, XMMF_DISP_CHN, &outFrame, 2000);
        // if (ret == CVI_SUCCESS) {
        //     CVI_VPSS_ReleaseChnFrame(XMMF_DISP_GRP, XMMF_DISP_CHN, &outFrame);
        // }

        return err::ERR_NONE;
    }

    err::Err _push_bgra8888(image::Image &im)
    {
        const int w = im.width();
        const int h = im.height();
        const CVI_U32 stride = (CVI_U32)(((w * 4) + 63u) & ~63u);
        const CVI_U32 frameSize = stride * (CVI_U32)h;

        CVI_U64   phyAddr = 0;
        CVI_VOID *virAddr = nullptr;
        CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &virAddr, "disp_argb", frameSize);
        if (ret != CVI_SUCCESS) {
            log::error("disp ARGB ION alloc failed: 0x%x", ret);
            return err::ERR_RUNTIME;
        }

        const uint8_t *src = (const uint8_t *)im.data();
        for (int y = 0; y < h; ++y) {
            memcpy((uint8_t *)virAddr + (size_t)y * stride, src + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
        }
        CVI_SYS_IonFlushCache(phyAddr, virAddr, frameSize);

        VIDEO_FRAME_INFO_S srcFrame;
        memset(&srcFrame, 0, sizeof(srcFrame));
        VIDEO_FRAME_S *vf = &srcFrame.stVFrame;
        vf->u32Width         = (CVI_U32)w;
        vf->u32Height        = (CVI_U32)h;
        vf->enPixelFormat    = PIXEL_FORMAT_ARGB_8888;
        vf->enVideoFormat    = VIDEO_FORMAT_LINEAR;
        vf->u32Stride[0]     = stride;
        vf->u32Length[0]     = frameSize;
        vf->u64PhyAddr[0]    = phyAddr;
        vf->pu8VirAddr[0]    = (CVI_U8 *)virAddr;

        _ensure_vpss_src_max((CVI_U32)w, (CVI_U32)h, PIXEL_FORMAT_ARGB_8888);

        {
            std::lock_guard<std::mutex> send_lk(maix::cvi::MediaRuntime::display_vpss0_send_mutex());
            ret = CVI_VPSS_SendFrame(XMMF_DISP_GRP, &srcFrame, -1);
        }
        CVI_SYS_IonFree(phyAddr, virAddr);
        if (ret != CVI_SUCCESS) {
            log::error("CVI_VPSS_SendFrame (ARGB) failed: 0x%x", ret);
            return err::ERR_RUNTIME;
        }
        return err::ERR_NONE;
    }

    err::Err _push_nv21_packed(const uint8_t *nv21, int w, int h)
    {
        VB_CAL_CONFIG_S vb;
        COMMON_GetPicBufferConfig((CVI_U32)w, (CVI_U32)h, PIXEL_FORMAT_NV21, DATA_BITWIDTH_8,
                                  COMPRESS_MODE_NONE, DEFAULT_ALIGN, &vb);

        CVI_U64   phyAddr = 0;
        CVI_VOID *virAddr = nullptr;
        CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &virAddr, "disp_nv21", vb.u32VBSize);
        if (ret != CVI_SUCCESS) {
            log::error("disp NV21 ION alloc failed: 0x%x", ret);
            return err::ERR_RUNTIME;
        }

        CVI_U8 *pY = (CVI_U8 *)virAddr;
        const CVI_U32 strideY = vb.u32MainStride;
        for (int y = 0; y < h; ++y) {
            memcpy(pY + (size_t)y * strideY, nv21 + (size_t)y * (size_t)w, (size_t)w);
        }

        const CVI_U32 offC = ALIGN(vb.u32MainYSize, vb.u16AddrAlign);
        CVI_U8 *pC = (CVI_U8 *)virAddr + offC;
        const uint8_t *srcC = nv21 + (size_t)w * (size_t)h;
        const CVI_U32 strideC = vb.u32CStride;
        const int hc = h / 2;
        for (int y = 0; y < hc; ++y) {
            memcpy(pC + (size_t)y * strideC, srcC + (size_t)y * (size_t)w, (size_t)w);
        }
        CVI_SYS_IonFlushCache(phyAddr, virAddr, vb.u32VBSize);

        VIDEO_FRAME_INFO_S srcFrame;
        memset(&srcFrame, 0, sizeof(srcFrame));
        VIDEO_FRAME_S *vf = &srcFrame.stVFrame;
        vf->u32Width         = (CVI_U32)w;
        vf->u32Height        = (CVI_U32)h;
        vf->enPixelFormat    = PIXEL_FORMAT_NV21;
        vf->enVideoFormat    = VIDEO_FORMAT_LINEAR;
        vf->u32Stride[0]     = vb.u32MainStride;
        vf->u32Stride[1]     = vb.u32CStride;
        vf->u32Length[0]     = vb.u32MainYSize;
        vf->u32Length[1]     = vb.u32MainCSize;
        vf->u64PhyAddr[0]    = phyAddr;
        vf->pu8VirAddr[0]    = pY;
        vf->u64PhyAddr[1]    = phyAddr + (CVI_U64)offC;
        vf->pu8VirAddr[1]    = pC;

        _ensure_vpss_src_max((CVI_U32)w, (CVI_U32)h, PIXEL_FORMAT_NV21);

        {
            std::lock_guard<std::mutex> send_lk(maix::cvi::MediaRuntime::display_vpss0_send_mutex());
            ret = CVI_VPSS_SendFrame(XMMF_DISP_GRP, &srcFrame, -1);
        }
        CVI_SYS_IonFree(phyAddr, virAddr);
        if (ret != CVI_SUCCESS) {
            log::error("CVI_VPSS_SendFrame (NV21) failed: 0x%x", ret);
            return err::ERR_RUNTIME;
        }
        return err::ERR_NONE;
    }
};

} // namespace maix::display
