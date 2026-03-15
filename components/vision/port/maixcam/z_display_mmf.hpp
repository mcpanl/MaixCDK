/**
 * @author lxowalle@sipeed
 * @copyright Sipeed Ltd 2023-
 * @license Apache 2.0
 * @update 2023.9.8: Add framework, create this file.
 */


#pragma once

#include "z_display_base.hpp"
#include "maix_thread.hpp"
#include "z_image.hpp"
#include "maix_time.hpp"
#include <unistd.h>
//#include "sophgo_middleware.hpp"
//#include "maix_pwm.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include "maix_fs.hpp"
#include <iostream>
#include <string>
#include "z_lib.hpp"

using namespace std;

//using namespace maix::peripheral;
using namespace maix;

namespace maix::display
{
    enum class PanelType {
        NORMAL,
        LT9611,
        UNKNOWN,
    };
    inline static PanelType __g_panel_type = PanelType::UNKNOWN;

    __attribute__((unused)) static int _get_vo_max_size(int *width, int *height, int rotate)
    {
        int w = Z_HEIGHT;
        int h = Z_WIDTH;

        if (rotate) {
            *width = h;
            *height = w;
        } else {
            *width = w;
            *height = h;
        }
        return 0;
    }


    static void _get_disp_configs(bool &flip, bool &mirror, float &max_backlight) {
        flip = false;
        mirror = false;
        max_backlight = 50.0;
//        std::string flip_str;
//        bool flip_is_found = false;
//
//        auto device_configs = sys::device_configs();
//        auto it = device_configs.find("disp_flip");
//        if (it != device_configs.end()) {
//            flip_str = it->second;
//            flip_is_found = true;
//        }
//        auto it2 = device_configs.find("disp_mirror");
//        if (it2 != device_configs.end()) {
//            if (it2->second.size() > 0)
//                mirror = atoi(it2->second.c_str());
//        }
//        auto it3 = device_configs.find("disp_max_backlight");
//        if (it3 != device_configs.end()) {
//            if (it3->second.size() > 0)
//                max_backlight = atof(it3->second.c_str());
//        }
//
//        if (flip_is_found && flip_str.size() > 0) {
//            flip = atoi(flip_str.c_str());
//        } else {
//            std::string board_id = sys::device_id();
//            if (board_id == "maixcam_pro") {
//                flip = true;
//            }
//        }

        // log::info("disp config flip: %d, mirror: %d, max_backlight: %.1f", flip, mirror, max_backlight);
    }


    class DisplayCviMmf final : public DisplayBase
    {
    public:
        DisplayCviMmf(const string &device, int width, int height, image::Format format)
        {
            printf("=== DisplayCviMmf init\n");

            err::check_bool_raise(!_get_vo_max_size(&_max_width, &_max_height, 1), "get vo max size failed");
            width = width <= 0 ? _max_width : width;
            height = height <= 0 ? _max_height : height;
            this->_width = width > _max_width ? _max_width : width;
            this->_height = height > _max_height ? _max_height : height;
            this->_format = format;
            this->_opened = false;
            this->_format = format;
            this->_invert_flip = false;
            this->_invert_mirror = false;
            this->_layer = 0;       // layer 0 means vedio layer
            err::check_bool_raise(_format == image::FMT_RGB888
                                || _format == image::FMT_YVU420SP
                                || _format == image::FMT_BGRA8888, "Format not support");

            if (this->_layer == 0) {
                bool flip = false;
                bool mirror = false;
                _max_backlight = 50.0;
                _get_disp_configs(flip, mirror, _max_backlight);
//                mmf_set_vo_video_hmirror(0, mirror);
//                mmf_set_vo_video_flip(0, flip);
                this->_invert_flip = flip;
                this->_invert_mirror = mirror;
            }

            maix::z_lib_init();


//            if (0 != mmf_init_v2(true)) {
//                err::check_raise(err::ERR_RUNTIME, "mmf init failed");
//            }
//            int pwm_id = 10;
//            _bl_pwm = new pwm::PWM(pwm_id, 100000, 20);
        }

        DisplayCviMmf(int layer, int width, int height, image::Format format)
        {
            err::check_bool_raise(!_get_vo_max_size(&_max_width, &_max_height, 1), "get vo max size failed");
            width = width <= 0 ? _max_width : width;
            height = height <= 0 ? _max_height : height;
            this->_width = width > _max_width ? _max_width : width;
            this->_height = height > _max_height ? _max_height : height;
            this->_format = format;
            this->_opened = false;
            this->_format = format;
            this->_invert_flip = false;
            this->_invert_mirror = false;
            this->_max_backlight = 50.0;
            this->_layer = layer;       // layer 0 means vedio layer
                                        // layer 1 means osd layer
            err::check_bool_raise(_format == image::FMT_BGRA8888, "Format not support");

            maix::z_lib_init();

//            if (0 != mmf_init_v2(true)) {
//                err::check_raise(err::ERR_RUNTIME, "mmf init failed");
//            }
//            int pwm_id = 10;
//            _bl_pwm = new pwm::PWM(pwm_id, 100000, 20);
        }

        ~DisplayCviMmf()
        {
            maix::z_lib_deinit();

//            mmf_del_vo_channel(this->_layer, this->_ch);
//            mmf_deinit_v2(false);
//            if(_bl_pwm && this->_layer == 0)    // _layer = 0, means video layer
//            {
//                delete _bl_pwm;
//            }
        }

        int width()
        {
            return this->_width;
        }

        int height()
        {
            return this->_height;
        }

        std::vector<int> size()
        {
            return {this->_width, this->_height};
        }

        image::Format format()
        {
            return this->_format;
        }

        err::Err open(int width, int height, image::Format format)
        {
            width = width > _max_width ? _max_width : width;
            height = height > _max_height ? _max_height : height;
            if(this->_opened)
            {
                return err::ERR_NONE;
            }

            // TODO: open vo channel
            int ch = 0;
//            int ch = mmf_get_vo_unused_channel(this->_layer);
//            if (ch < 0) {
//                log::error("mmf_get_vo_unused_channel failed\n");
//                return err::ERR_RUNTIME;
//            }

//            int mmf_format_out = PIXEL_FORMAT_NV21;
//            image::Format format_out = (image::Format)mmf_invert_format_to_maix(mmf_format_out);
//            size_t image_size = image::fmt_size[format_out];
//            int pool_num_out = 1;
//            if (width * height * image_size > 1920 * 1080 * image_size || __g_panel_type == PanelType::LT9611) {
//                pool_num_out = 2;
//            }
//            int rotate = 90;
//            if (__g_panel_type == PanelType::LT9611)
//                rotate = 0;
//            if (0 != mmf_add_vo_channel_v2(this->_layer, ch, width, height, mmf_invert_format_to_mmf(format), mmf_format_out, -1, 0, -1, -1, 0, rotate, 2, pool_num_out)) {
//                log::error("mmf_add_vo_channel_v2 failed\n");
//                return err::ERR_RUNTIME;
//            }

            this->_ch = ch;
            this->_opened = true;
            return err::ERR_NONE;
        }

        err::Err close()
        {
            if (!this->_opened)
                return err::ERR_NONE;

            // TODO: close vo channel
//            if (mmf_vo_channel_is_open(this->_layer, this->_ch) == true) {
//                if (0 != mmf_del_vo_channel(this->_layer, this->_ch)) {
//                    log::error("mmf del vo channel failed\n");
//                    return err::ERR_RUNTIME;
//                }
//            }
            this->_opened = false;
            return err::ERR_NONE;
        }

        display::DisplayCviMmf *add_channel(int width, int height, image::Format format)
        {
            int new_width = 0;
            int new_height = 0;
            image::Format new_format = format;
            if (width == -1) {
                new_width = this->_width;
            } else {
                new_width = width > this->_width ? this->_width : width;
            }
            if (height == -1) {
                new_height = this->_height;
            } else {
                new_height = height > this->_height ? this->_height : height;
            }

            _format = new_format;
            DisplayCviMmf *disp = new DisplayCviMmf(1, new_width, new_height, new_format);
            return disp;
        }

        bool is_opened()
        {
            return this->_opened;
        }

        err::Err show(image::Image &img, image::Fit fit)
        {
            err::check_bool_raise((img.width() % 2 == 0 && img.height() % 2 == 0), "Image width and height must be a multiple of 2.");
            if (img.format() != image::FMT_RGB888) {
                err::check_raise(err::ERR_ARGS, "Display::show currently only supports RGB888 in z_lib path");
            }

            VIDEO_FRAME_INFO_S outFrame;

            CVI_S32 ret = Z_VO_PUSH_FRAME_WITH_RGB888(nullptr, (const CVI_U8 *)img.data(), img.width(), img.height(), &outFrame);
            if (ret != CVI_SUCCESS) {
                log::error("display show failed: 0x%x, size=%dx%d, format=%d\n", ret, img.width(), img.height(), img.format());
                return err::ERR_RUNTIME;
            }

            return err::ERR_NONE;
        }

        void set_backlight(float value)
        {
//            _bl_pwm->duty(value * _max_backlight / 100.0);
//            _bl_pwm->disable();
//            if(value == 0)
//                return;
//            _bl_pwm->enable();
        }

        float get_backlight()
        {
            return 50;
//            return _bl_pwm->duty() / _max_backlight * 100;
        }

        int get_ch_nums()
        {
            return 2;
        }

        err::Err set_hmirror(bool en) {
//            en = _invert_mirror ? !en : en;
//
//            bool need_open = false;
//            if (this->_opened) {
//                this->close();
//                need_open = true;
//            }
//
//            if (this->_layer == 0) {
//                mmf_set_vo_video_hmirror(this->_ch, en);
//            } else {
//                err::check_raise(err::ERR_RUNTIME, "Not support layer");
//            }
//
//            if (need_open) {
//                err::check_raise(this->open(this->_width, this->_height, this->_format), "Open failed");
//            }
            return err::ERR_NONE;
        }

        err::Err set_vflip(bool en) {
//            en = _invert_flip ? !en : en;
//
//            bool need_open = false;
//            if (this->_opened) {
//                this->close();
//                need_open = true;
//            }
//
//            if (this->_layer == 0) {
//                mmf_set_vo_video_flip(this->_ch, en);
//            } else {
//                err::check_raise(err::ERR_RUNTIME, "Not support layer");
//            }
//
//            if (need_open) {
//                err::check_raise(this->open(this->_width, this->_height, this->_format), "Open failed");
//            }
            return err::ERR_NONE;
        }

    private:
        int _width;
        int _height;
        int _max_width;
        int _max_height;
        image::Format _format;
        int _layer;
        int _ch;
        bool _opened;
        bool _invert_flip;
        bool _invert_mirror;
        float _max_backlight;
//        pwm::PWM *_bl_pwm;
    };
}
