#include "z_display_rk_x11.hpp"
#include "rk_lib.h"
#include "maix_app.hpp"
#include "maix_log.hpp"
#include "z_image.hpp"
#include <cstdlib>

using namespace maix;

namespace maix::display
{

    std::string DisplayRkX11::_resolve_window_title() const
    {
        const char *env_title = std::getenv("MAIX_X11_TITLE");
        if (env_title && env_title[0] != '\0')
            return std::string(env_title);

        if (_device_str.rfind("title=", 0) == 0 && _device_str.size() > 6)
            return _device_str.substr(6);

        if (!_device_str.empty()) {
            std::string key = "title=";
            size_t pos = _device_str.find(key);
            if (pos != std::string::npos) {
                size_t start = pos + key.size();
                size_t end = _device_str.find(',', start);
                return _device_str.substr(start, end == std::string::npos ? std::string::npos : end - start);
            }
        }

        std::string app_id = app::app_id();
        if (!app_id.empty())
            return app_id;
        return "";
    }

    DisplayRkX11::DisplayRkX11(const std::string &device, int width, int height, image::Format format)
        : _device_str(device), _vo(nullptr), _opened(false), _user_closed(false)
    {
        _window_title = _resolve_window_title();
        _width = width > 0 ? width : 800;
        _height = height > 0 ? height : 480;
        _format = (format == image::FMT_INVALID) ? image::FMT_RGB888 : format;
    }

    int DisplayRkX11::width()
    {
        return _width;
    }

    int DisplayRkX11::height()
    {
        return _height;
    }

    std::vector<int> DisplayRkX11::size()
    {
        return {_width, _height};
    }

    image::Format DisplayRkX11::format()
    {
        return _format;
    }

    err::Err DisplayRkX11::open(int width, int height, image::Format format)
    {
        if (_opened && _vo && !_user_closed)
            return err::ERR_NONE;

        if (width > 0)
            _width = width;
        if (height > 0)
            _height = height;
        if (format != image::FMT_INVALID)
            _format = format;

        if (_format != image::FMT_RGB888) {
            log::error("RK3566 X11 display requires image::FMT_RGB888\n");
            return err::ERR_ARGS;
        }

        if (_vo) {
            rk_vo_close(_vo);
            _vo = nullptr;
        }
        _user_closed = false;

        const char *title = _window_title.empty() ? nullptr : _window_title.c_str();
        if (rk_vo_open2(&_vo, nullptr, (unsigned)_width, (unsigned)_height, title) < 0) {
            log::error("rk_vo_open failed (set DISPLAY?)\n");
            _opened = false;
            return err::ERR_RUNTIME;
        }
        _opened = true;
        return err::ERR_NONE;
    }

    err::Err DisplayRkX11::close()
    {
        if (_vo) {
            rk_vo_close(_vo);
            _vo = nullptr;
        }
        _opened = false;
        _user_closed = false;
        return err::ERR_NONE;
    }

    display::DisplayBase *DisplayRkX11::add_channel(int, int, image::Format)
    {
        return nullptr;
    }

    bool DisplayRkX11::is_opened()
    {
        return _opened && _vo != nullptr && !_user_closed;
    }

    err::Err DisplayRkX11::poll_events()
    {
        if (!_vo)
            return err::ERR_NONE;
        if (rk_vo_poll_events(_vo, 0) == 0)
            _user_closed = true;
        return err::ERR_NONE;
    }

    err::Err DisplayRkX11::show(image::Image &img, image::Fit)
    {
        if (!is_opened())
            return err::ERR_NOT_INIT;

        poll_events();
        if (_user_closed)
            return err::ERR_RUNTIME;

        if (img.format() != image::FMT_RGB888) {
            log::error("DisplayRkX11::show expects RGB888\n");
            return err::ERR_ARGS;
        }

        int w = img.width();
        int h = img.height();
        if (w <= 0 || h <= 0)
            return err::ERR_ARGS;

        unsigned stride = (unsigned)(w * 3);
        if (rk_vo_put_rgb888(_vo, (const uint8_t *)img.data(), (unsigned)w, (unsigned)h, stride) < 0) {
            log::error("rk_vo_put_rgb888 failed\n");
            return err::ERR_RUNTIME;
        }
        return err::ERR_NONE;
    }

    void DisplayRkX11::set_backlight(float) {}

    float DisplayRkX11::get_backlight()
    {
        return 50.f;
    }

    int DisplayRkX11::get_ch_nums()
    {
        return 1;
    }

    err::Err DisplayRkX11::set_hmirror(bool)
    {
        return err::ERR_NONE;
    }

    err::Err DisplayRkX11::set_vflip(bool)
    {
        return err::ERR_NONE;
    }

} // namespace maix::display
