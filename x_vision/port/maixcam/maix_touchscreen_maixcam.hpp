/**
 * @author neucrack@sipeed
 * @copyright Sipeed Ltd 2023-
 * @license Apache 2.0
 * @update 2023.9.8: Add framework, create this file.
 */

#pragma once

#include <string>
#include <vector>
#include "maix_basic.hpp"
#include "maix_touchscreen_base.hpp"
#include <linux/input.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/epoll.h>

namespace maix::touchscreen
{

    class TouchScreen_MaixCam final : public TouchScreen_Base
    {
    public:
        TouchScreen_MaixCam(const std::string &device = "/dev/input/event0")
        {
            _opened = false;
            _fd = -1;
            _epoll_fd = -1;
            _device = device.empty() ? _default_device() : device;
            _x_abs_code = ABS_MT_POSITION_X;
            _y_abs_code = ABS_MT_POSITION_Y;
            if(device.empty())
            {
                log::info("touchscreen device not specified, fallback to default: %s", _device.c_str());
            }
        }

        err::Err open()
        {
            if(_fd > 0)
            {
                return err::ERR_NONE;
            }
            _opened = false;
            _fd = ::open(_device.c_str(), O_RDONLY);
            if(_fd < 0)
            {
                log::error("open touch screen failed: device=%s err=%s(%d)", _device.c_str(), strerror(errno), errno);
                return err::ERR_IO;
            }

            _x = 0;
            _y = 0;
            _pressed = false;

            // set unblock read
            int non_blocking = 1;
            ioctl(_fd, FIONBIO, &non_blocking);

            // char * absval[6] = { "Value", "Min", "Max", "Fuzz", "Flat", "Resolution" };
            int absX[6] = {};
            int absY[6] = {};

            bool has_mt_x = _query_abs_info(ABS_MT_POSITION_X, absX, "ABS_MT_POSITION_X");
            bool has_mt_y = _query_abs_info(ABS_MT_POSITION_Y, absY, "ABS_MT_POSITION_Y");
            bool has_x = has_mt_x || _query_abs_info(ABS_X, absX, "ABS_X");
            bool has_y = has_mt_y || _query_abs_info(ABS_Y, absY, "ABS_Y");
            if(has_x && has_y)
            {
                _x_abs_code = has_mt_x ? ABS_MT_POSITION_X : ABS_X;
                _y_abs_code = has_mt_y ? ABS_MT_POSITION_Y : ABS_Y;
            }

            _x_max = absX[2];
            _y_max = absY[2];
            if(_x_max <= 0 || _y_max <= 0)
            {
                log::error("get touchscreen resolution failed2");
                _x_max = 480;
                _y_max = 640;
            }

            _init_epoll(_fd);

            _opened = true;
            return err::ERR_NONE;
        }

        err::Err close()
        {
            if(_fd > 0)
            {
                ::close(_fd);
                _fd = -1;
            }
            if(_epoll_fd >= 0)
            {
                ::close(_epoll_fd);
                _epoll_fd = -1;
            }
            _opened = false;
            return err::ERR_NONE;
        }

        bool is_opened() {
            return _opened;
        }

        err::Err read(int &x, int &y, bool &pressed)
        {
            err::Err e = _read(true);
            if(e != err::ERR_NONE)
                return e;
            x = _x;
            y = _y;
            pressed = _pressed;
            return err::ERR_NONE;
        }

        std::vector<int> read()
        {
            _read(true);
            return {_x, _y, _pressed ? 1 : 0};
        }

        err::Err read0(int &x, int &y, bool &pressed)
        {
            err::Err e = _read(false);
            if(e != err::ERR_NONE)
                return e;
            x = _x;
            y = _y;
            pressed = _pressed;
            return err::ERR_NONE;
        }

        std::vector<int> read0()
        {
            _read(false);
            return {_x, _y, _pressed ? 1 : 0};
        }

        bool available(int timeout)
        {
            return _available(timeout);
        }


    private:
        static constexpr const char *_default_device()
        {
            return "/dev/input/event0";
        }

        int _x;
        int _y;
        bool _pressed;
        int _width;
        int _height;
        bool _opened;
        int _fd;
        std::string _device;
        int _x_max;
        int _y_max;
        int _epoll_fd;
        int _x_abs_code;
        int _y_abs_code;

        bool _query_abs_info(int code, int out[6], const char *name)
        {
            errno = 0;
            int ret = ioctl(_fd, EVIOCGABS(code), out);
            if(ret < 0)
            {
                log::debug("query touchscreen abs failed: device=%s code=%s err=%s(%d)", _device.c_str(), name, strerror(errno), errno);
                return false;
            }
            log::debug("query touchscreen abs success: device=%s code=%s min=%d max=%d", _device.c_str(), name, out[1], out[2]);
            return true;
        }

        void _init_epoll(int fd)
        {
            _epoll_fd = epoll_create(1);
            if(_epoll_fd < 0)
            {
                log::error("create epoll failed: %s", strerror(errno));
                return;
            }
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.fd = fd;
            if(epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
            {
                log::error("epoll_ctl add failed: %s", strerror(errno));
                return;
            }
        }

        bool _available(int timeout)
        {
            struct epoll_event events[1];
            int nfds = epoll_wait(_epoll_fd, events, 1, timeout);
            return nfds > 0;
        }

        err::Err _read(bool empty_buffer)
        {
            struct input_event event;
            // struct epoll_event events[1];
            bool event_press = false;
            bool event_move = false;
            while(1)
            {
                // int nfds = epoll_wait(_epoll_fd, events, 1, 0);
                // if(nfds <= 0)
                // {
                //     return err::ERR_NOT_READY;
                // }
                int ret = ::read(_fd, &event, sizeof(event));
                if(ret != sizeof(event))
                {
                    if(event_move)
                        return err::ERR_NONE;
                    if(errno == EAGAIN || errno == EWOULDBLOCK)
                        return err::ERR_NOT_READY;
                    log::error("read touch screen failed: %s, %d", strerror(errno), ret);
                    return err::ERR_IO;
                }
                // printf("event %d %d\n", event.type, event.code);
                if(event.type == EV_ABS)
                {
                    if(event.code == ABS_MT_TRACKING_ID)
                    {
                        _pressed = event.value >= 0;
                        event_press = true;
                    }
                    if(event.code == ABS_MT_POSITION_X || event.code == ABS_X)
                    {
                        // _x = event.value;
                        // anti-clockwise 90 degree
                        _y = event.value;
                        event_move = true;
                    }
                    else if(event.code == ABS_MT_POSITION_Y || event.code == ABS_Y)
                    {
                        // _y = event.value;
                        // anti-clockwise 90 degree
                        _x = _y_max - event.value - 1;
                        event_move = true;
                    }
                }
                else if(event.type == EV_KEY)
                {
                    if(event.code == BTN_TOUCH || event.code == BTN_TOOL_FINGER)
                    {
                        _pressed = event.value == 1;
                        event_press = true;
                    }
                }
                else if(event.type == EV_SYN)
                {
                    if(event_press)
                    {
                        break;
                    }
                    if(event_move && !empty_buffer)
                    {
                        break;
                    }
                }
            }
            return err::ERR_NONE;
        }
    };
} // namespace maix::touchscreen



