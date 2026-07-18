#include "z_camera.hpp"
#include "maix_basic.hpp"
#include "maix_log.hpp"
#include "rk_lib.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

using namespace maix;

namespace maix::camera
{
    namespace
    {
        struct RkCameraPriv
        {
            rk_vi_ctx_t *vi = nullptr;
            bool streaming = false;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t pixfmt = 0;
            uint32_t y_stride = 0;
            uint32_t uv_stride = 0;
            std::vector<uint8_t> rgb888;
        };

        bool set_regs_flag = false;

        static std::vector<std::string> list_video_devs()
        {
            std::vector<std::string> devs;
            DIR *d = opendir("/dev");
            if (!d)
                return devs;

            dirent *ent = nullptr;
            while ((ent = readdir(d)) != nullptr) {
                if (strncmp(ent->d_name, "video", 5) == 0) {
                    devs.push_back(std::string("/dev/") + ent->d_name);
                }
            }
            closedir(d);
            std::sort(devs.begin(), devs.end());
            return devs;
        }

        static bool is_supported_format(image::Format format)
        {
            return format == image::FMT_RGB888 || format == image::FMT_BGR888 ||
                   format == image::FMT_RGBA8888 || format == image::FMT_BGRA8888 ||
                   format == image::FMT_GRAYSCALE || format == image::FMT_YVU420SP;
        }
    } // namespace

    std::vector<std::string> list_devices()
    {
        return list_video_devs();
    }

    void set_regs_enable(bool enable)
    {
        set_regs_flag = enable;
        (void)set_regs_flag;
        log::warn("RK3566 camera register passthrough is not implemented yet");
    }

    std::string get_device_name()
    {
        auto devs = list_video_devs();
        return devs.empty() ? std::string() : devs[0];
    }

    Camera::Camera(int width, int height, image::Format format, const char *device, double fps, int buff_num,
                   bool open, bool raw)
    {
        (void)raw;
        err::check_bool_raise(is_supported_format(format), "Format not support");

        _width = (width == -1) ? 640 : width;
        _height = (height == -1) ? 480 : height;
        _fps = (fps == -1) ? 30.0 : fps;
        _buff_num = (buff_num <= 0) ? 3 : buff_num;
        _format = format;
        _format_impl = image::FMT_RGB888;
        _hmirror = 0;
        _vflip = 0;
        _exposure = -1;
        _gain = -1;
        _show_colorbar = false;
        _open_set_regs = set_regs_flag;
        _last_read_us = time::ticks_us();
        _invert_flip = false;
        _invert_mirror = false;
        _is_opened = false;
        _ch = 0;
        _device = device ? device : "";

        _param = new RkCameraPriv();

        if (open) {
            err::Err e = this->open(_width, _height, _format, _fps, _buff_num);
            err::check_raise(e, "camera open failed");
        }
    }

    Camera::~Camera()
    {
        if (this->is_opened())
            this->close();

        auto *priv = (RkCameraPriv *)_param;
        delete priv;
        _param = nullptr;
    }

    int Camera::get_ch_nums()
    {
        return 1;
    }

    err::Err Camera::open(int width, int height, image::Format format, double fps, int buff_num)
    {
        auto *priv = (RkCameraPriv *)_param;
        err::check_null_raise(priv, "camera private context not initialized");

        int width_tmp = (width == -1) ? _width : width;
        int height_tmp = (height == -1) ? _height : height;
        image::Format format_tmp = (format == image::FMT_INVALID) ? _format : format;
        double fps_tmp = (fps == -1) ? _fps : fps;
        int buff_num_tmp = (buff_num == -1) ? _buff_num : buff_num;
        (void)fps_tmp;
        (void)buff_num_tmp;
        (void)_open_set_regs;

        err::check_bool_raise(is_supported_format(format_tmp), "Format not support");

        if (_is_opened)
            this->close();

        const char *dev = _device.empty() ? nullptr : _device.c_str();
        if (rk_vi_open(&priv->vi, dev) < 0) {
            log::error("rk_vi_open failed, device: %s", dev ? dev : RK_VI_DEFAULT_DEVICE);
            return err::ERR_RUNTIME;
        }

        static const uint32_t k_try_fmt[] = {
            RK_PIX_FMT_UYVY,
            RK_PIX_FMT_YUYV,
            RK_PIX_FMT_NV12,
            RK_PIX_FMT_NV21,
        };

        bool fmt_ok = false;
        for (size_t i = 0; i < sizeof(k_try_fmt) / sizeof(k_try_fmt[0]); ++i) {
            if (rk_vi_set_format(priv->vi, (uint32_t)width_tmp, (uint32_t)height_tmp, k_try_fmt[i]) == 0) {
                fmt_ok = true;
                break;
            }
        }
        if (!fmt_ok) {
            rk_vi_close(priv->vi);
            priv->vi = nullptr;
            log::error("rk_vi_set_format failed for all supported formats");
            return err::ERR_RUNTIME;
        }

        if (rk_vi_get_format(priv->vi, &priv->width, &priv->height, &priv->pixfmt) < 0) {
            rk_vi_close(priv->vi);
            priv->vi = nullptr;
            log::error("rk_vi_get_format failed");
            return err::ERR_RUNTIME;
        }

        priv->y_stride = rk_vi_y_stride_bytes(priv->vi);
        priv->uv_stride = rk_vi_uv_stride_bytes(priv->vi);
        if ((priv->pixfmt == RK_PIX_FMT_NV12 || priv->pixfmt == RK_PIX_FMT_NV21) && priv->y_stride == 0)
            priv->y_stride = priv->width;
        if ((priv->pixfmt == RK_PIX_FMT_NV12 || priv->pixfmt == RK_PIX_FMT_NV21) && priv->uv_stride == 0)
            priv->uv_stride = priv->width;
        if ((priv->pixfmt == RK_PIX_FMT_UYVY || priv->pixfmt == RK_PIX_FMT_YUYV) && priv->y_stride == 0)
            priv->y_stride = priv->width * 2;

        if (rk_vi_stream_on(priv->vi) < 0) {
            rk_vi_close(priv->vi);
            priv->vi = nullptr;
            log::error("rk_vi_stream_on failed");
            return err::ERR_RUNTIME;
        }

        priv->streaming = true;
        priv->rgb888.resize((size_t)priv->width * (size_t)priv->height * 3);

        _width = (int)priv->width;
        _height = (int)priv->height;
        _fps = fps_tmp;
        _buff_num = buff_num_tmp;
        _format = format_tmp;
        _format_impl = image::FMT_RGB888;
        _is_opened = true;

        return err::ERR_NONE;
    }

    image::Image *Camera::read(void *buff, size_t buff_size, bool block, int block_ms)
    {
        auto *priv = (RkCameraPriv *)_param;
        if (!priv)
            return nullptr;

        if (!this->is_opened()) {
            err::Err e = open(_width, _height, _format, _fps, _buff_num);
            err::check_raise(e, "open camera failed");
        }

        void *plane = nullptr;
        size_t len = 0;
        unsigned idx = 0;
        int timeout = block ? block_ms : 0;
        if (timeout < 0)
            timeout = 1000;

        int dr = rk_vi_dequeue(priv->vi, &plane, &len, &idx, timeout);
        if (dr == -ETIMEDOUT)
            return nullptr;
        if (dr == -EINTR)
            return nullptr;
        if (dr < 0) {
            log::error("rk_vi_dequeue failed: %d", dr);
            return nullptr;
        }
        (void)len;

        if (priv->pixfmt == RK_PIX_FMT_YUYV) {
            rk_vi_yuyv_to_rgb888((const uint8_t *)plane, priv->width, priv->height, priv->y_stride,
                                 priv->rgb888.data(), priv->width * 3);
        } else if (priv->pixfmt == RK_PIX_FMT_UYVY) {
            rk_vi_uyvy_to_rgb888((const uint8_t *)plane, priv->width, priv->height, priv->y_stride,
                                 priv->rgb888.data(), priv->width * 3);
        } else if (priv->pixfmt == RK_PIX_FMT_NV12) {
            uint8_t *uv = (uint8_t *)rk_vi_buffer_uv_plane(priv->vi, idx);
            if (!uv) {
                rk_vi_queue(priv->vi, idx);
                log::error("NV12 frame has no UV plane");
                return nullptr;
            }
            rk_vi_nv12_to_rgb888((const uint8_t *)plane, uv, priv->width, priv->height, priv->y_stride,
                                 priv->uv_stride, priv->rgb888.data(), priv->width * 3);
        } else if (priv->pixfmt == RK_PIX_FMT_NV21) {
            uint8_t *vu = (uint8_t *)rk_vi_buffer_uv_plane(priv->vi, idx);
            if (!vu) {
                rk_vi_queue(priv->vi, idx);
                log::error("NV21 frame has no VU plane");
                return nullptr;
            }
            rk_vi_nv21_to_rgb888((const uint8_t *)plane, vu, priv->width, priv->height, priv->y_stride,
                                 priv->uv_stride, priv->rgb888.data(), priv->width * 3);
        } else {
            rk_vi_queue(priv->vi, idx);
            log::error("unsupported negotiated pixfmt: 0x%08x", (unsigned)priv->pixfmt);
            return nullptr;
        }

        image::Image *img = new image::Image((int)priv->width, (int)priv->height, image::FMT_RGB888,
                                             priv->rgb888.data(), (int)priv->rgb888.size(), true);
        rk_vi_queue(priv->vi, idx);

        if (_format != image::FMT_RGB888) {
            image::Image *img_conv = img->to_format(_format);
            delete img;
            img = img_conv;
            if (!img) {
                log::error("camera read: convert image format failed");
                return nullptr;
            }
        }

        if (buff) {
            if (buff_size < (size_t)img->data_size()) {
                log::error("camera read: buff size not enough, need %d, but %d", img->data_size(),
                           (int)buff_size);
                delete img;
                return nullptr;
            }
            memcpy(buff, img->data(), img->data_size());
            image::Image *img_buff = new image::Image(img->width(), img->height(), img->format(),
                                                      (uint8_t *)buff, (int)buff_size, false);
            delete img;
            return img_buff;
        }

        _last_read_us = time::ticks_us();
        return img;
    }

    image::Image *Camera::read_raw()
    {
        log::warn("read_raw not support on rk3566 now");
        return nullptr;
    }

    void Camera::clear_buff()
    {
        if (!is_opened())
            return;

        for (int i = 0; i < 4; ++i) {
            image::Image *img = read(nullptr, 0, false, 0);
            if (!img)
                break;
            delete img;
        }
    }

    void Camera::skip_frames(int num)
    {
        if (num <= 0)
            return;
        for (int i = 0; i < num; ++i) {
            image::Image *img = read();
            if (img)
                delete img;
        }
    }

    void Camera::close()
    {
        auto *priv = (RkCameraPriv *)_param;
        if (!priv)
            return;

        if (priv->streaming && priv->vi) {
            rk_vi_stream_off(priv->vi);
            priv->streaming = false;
        }
        if (priv->vi) {
            rk_vi_close(priv->vi);
            priv->vi = nullptr;
        }
        _is_opened = false;
    }

    camera::Camera *Camera::add_channel(int width, int height, image::Format format, double fps, int buff_num,
                                        bool open)
    {
        (void)width;
        (void)height;
        (void)format;
        (void)fps;
        (void)buff_num;
        (void)open;
        err::check_raise(err::ERR_NOT_IMPL, "add_channel not support on rk3566 now");
        return nullptr;
    }

    bool Camera::is_opened()
    {
        return _is_opened;
    }

    int Camera::hmirror(int value)
    {
        if (value != -1)
            _hmirror = value ? 1 : 0;
        return _hmirror;
    }

    int Camera::vflip(int value)
    {
        if (value != -1)
            _vflip = value ? 1 : 0;
        return _vflip;
    }

    err::Err Camera::write_reg(int addr, int data, int bit_width)
    {
        (void)addr;
        (void)data;
        (void)bit_width;
        return err::ERR_NOT_IMPL;
    }

    int Camera::read_reg(int addr, int bit_width)
    {
        (void)addr;
        (void)bit_width;
        return -1;
    }

    err::Err Camera::show_colorbar(bool enable)
    {
        _show_colorbar = enable;
        return err::ERR_NOT_IMPL;
    }

    int Camera::get_channel()
    {
        return _ch;
    }

    err::Err Camera::set_resolution(int width, int height)
    {
        return this->open(width, height, _format, _fps, _buff_num);
    }

    err::Err Camera::set_fps(double fps)
    {
        return this->open(_width, _height, _format, fps, _buff_num);
    }

    int Camera::exposure(int value)
    {
        if (value != -1)
            _exposure = value;
        return (int)_exposure;
    }

    int Camera::gain(int value)
    {
        if (value != -1)
            _gain = value;
        return (int)_gain;
    }

    int Camera::luma(int value)
    {
        return value;
    }

    int Camera::constrast(int value)
    {
        return value;
    }

    int Camera::saturation(int value)
    {
        return value;
    }

    int Camera::awb_mode(int value)
    {
        return value;
    }

    int Camera::set_awb(int mode)
    {
        return mode;
    }

    int Camera::exp_mode(int value)
    {
        return value;
    }

    err::Err Camera::set_windowing(std::vector<int> roi)
    {
        (void)roi;
        return err::ERR_NOT_IMPL;
    }

    std::vector<int> Camera::get_sensor_size()
    {
        return {_width, _height};
    }

} // namespace maix::camera
