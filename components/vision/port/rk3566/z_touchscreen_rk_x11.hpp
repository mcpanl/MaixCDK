#pragma once

#include <cerrno>
#include <string>
#include <vector>

#include "maix_basic.hpp"
#include "rk_lib.h"
#include "z_touchscreen_base.hpp"

using namespace maix;

namespace maix::touchscreen
{
    class TouchScreen_RkX11 final : public TouchScreen_Base
    {
    public:
        explicit TouchScreen_RkX11(const std::string &device = "")
            : _opened(false), _device(device), _x(0), _y(0), _pressed(false)
        {
        }

        ~TouchScreen_RkX11()
        {
            if (_opened)
                close();
        }

        err::Err open() override
        {
            _opened = true;
            return err::ERR_NONE;
        }

        err::Err close() override
        {
            _opened = false;
            return err::ERR_NONE;
        }

        err::Err read(int &x, int &y, bool &pressed) override
        {
            return _read_impl(x, y, pressed, true);
        }

        std::vector<int> read() override
        {
            int x = _x;
            int y = _y;
            bool pressed = _pressed;
            _read_impl(x, y, pressed, true);
            return {x, y, pressed ? 1 : 0};
        }

        err::Err read0(int &x, int &y, bool &pressed) override
        {
            return _read_impl(x, y, pressed, false);
        }

        std::vector<int> read0() override
        {
            int x = _x;
            int y = _y;
            bool pressed = _pressed;
            _read_impl(x, y, pressed, false);
            return {x, y, pressed ? 1 : 0};
        }

        bool available(int timeout = 0) override
        {
            if (!_opened)
                return false;
            return rk_vo_touch_available(nullptr, timeout) > 0;
        }

        bool is_opened() override
        {
            return _opened;
        }

    private:
        err::Err _read_impl(int &x, int &y, bool &pressed, bool drain_nonkey)
        {
            if (!_opened)
                return err::ERR_NOT_INIT;

            int px = 0;
            int py = 0;
            int p = 0;
            int ret = rk_vo_touch_read(nullptr, &px, &py, &p, drain_nonkey ? 1 : 0);
            if (ret == 0) {
                _x = px;
                _y = py;
                _pressed = (p != 0);
                x = _x;
                y = _y;
                pressed = _pressed;
                return err::ERR_NONE;
            }

            if (ret == -EAGAIN || ret == -ENODEV)
                return err::ERR_NOT_READY;
            return err::ERR_RUNTIME;
        }

    private:
        bool _opened;
        std::string _device;
        int _x;
        int _y;
        bool _pressed;
    };
} // namespace maix::touchscreen
