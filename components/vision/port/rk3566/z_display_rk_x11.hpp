/**
 * RK3566: X11 window via rk_lib (bundled libX11). Used by maix.display.Display.
 */
#pragma once

#include "z_display_base.hpp"
#include "rk_lib.h"
#include <string>
#include <vector>

namespace maix::display
{

    class DisplayRkX11 final : public DisplayBase
    {
    public:
        DisplayRkX11(const std::string &device, int width, int height, image::Format format);

        int width() override;
        int height() override;
        std::vector<int> size() override;
        image::Format format() override;

        err::Err open(int width = -1, int height = -1, image::Format format = image::FMT_RGB888) override;
        err::Err close() override;
        display::DisplayBase *add_channel(int width = -1, int height = -1,
                                          image::Format format = image::FMT_RGB888) override;
        bool is_opened() override;
        err::Err show(image::Image &img, image::Fit fit = image::FIT_CONTAIN) override;
        void set_backlight(float value) override;
        float get_backlight() override;
        int get_ch_nums() override;
        err::Err set_hmirror(bool en) override;
        err::Err set_vflip(bool en) override;
        err::Err poll_events() override;

    private:
        std::string _resolve_window_title() const;
        std::string _device_str;
        std::string _window_title;
        rk_vo_ctx_t *_vo;
        int _width;
        int _height;
        image::Format _format;
        bool _opened;
        bool _user_closed;
    };

} // namespace maix::display
