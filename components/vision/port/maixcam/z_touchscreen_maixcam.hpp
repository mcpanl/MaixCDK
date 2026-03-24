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
#include "z_touchscreen_base.hpp"
#include <linux/input.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Switch: define TOUCHSCREEN_USE_POLL to force poll(), otherwise epoll is used.
// epoll_create returns ENOSYS on this platform (kernel stubs not implemented),
// so poll() is used by default.
// #define TOUCHSCREEN_USE_POLL

#include <poll.h>
#ifndef TOUCHSCREEN_USE_POLL
#include <sys/epoll.h>
#endif

using namespace maix;

namespace maix::touchscreen
{
    class TouchScreen_MaixCam final : public TouchScreen_Base
    {
    public:
        TouchScreen_MaixCam(const std::string &device = "/dev/input/event1")
            : _fd(-1), _opened(false), _x(0), _y(0), _pressed(false),
              _x_max(0), _y_max(0),
              _swap_xy(true), _invert_x(false), _invert_y(true)
#ifndef TOUCHSCREEN_USE_POLL
            , _epoll_fd(-1)
#endif
        {
            _device = device.empty() ? "/dev/input/event1" : device;
            if (device.empty())
                log::info("touchscreen device not specified, fallback to default: %s", _device.c_str());

#ifndef TOUCHSCREEN_USE_POLL
            log::info("touchscreen wait backend: epoll");
#else
            log::info("touchscreen wait backend: poll");
#endif
        }

        ~TouchScreen_MaixCam()
        {
            if (_opened)
                close();
        }

        err::Err open()
        {
            if (_fd >= 0)
                return err::ERR_NONE;

            log::info("open touchscreen start: device=%s", _device.c_str());

            _fd = ::open(_device.c_str(), O_RDONLY | O_NONBLOCK);
            if (_fd < 0)
            {
                log::error("open touchscreen failed: device=%s err=%s(%d)",
                           _device.c_str(), strerror(errno), errno);
                return err::ERR_IO;
            }
            log::info("open touchscreen success: device=%s fd=%d", _device.c_str(), _fd);

            _x = 0;
            _y = 0;
            _pressed = false;

            // Query absolute axis ranges
            int absX[6] = {};
            int absY[6] = {};
            bool has_mt_x = _query_abs(ABS_MT_POSITION_X, absX);
            bool has_mt_y = _query_abs(ABS_MT_POSITION_Y, absY);
            bool has_x    = has_mt_x || _query_abs(ABS_X, absX);
            bool has_y    = has_mt_y || _query_abs(ABS_Y, absY);

            if (!has_x || !has_y)
                log::warn("touchscreen abs range query failed: has_x=%d has_y=%d", has_x, has_y);
            else
                log::info("touchscreen abs source ready: mt_x=%d mt_y=%d x_max=%d y_max=%d",
                          has_mt_x, has_mt_y, absX[2], absY[2]);

            _x_max = (absX[2] > 0) ? absX[2] : 480;
            _y_max = (absY[2] > 0) ? absY[2] : 640;
            log::info("touchscreen resolution ready: x_max=%d y_max=%d", _x_max, _y_max);

#ifndef TOUCHSCREEN_USE_POLL
            _init_epoll(_fd);
#endif

            _opened = true;
            log::info("touchscreen open finished: device=%s x_max=%d y_max=%d",
                      _device.c_str(), _x_max, _y_max);
            return err::ERR_NONE;
        }

        err::Err close()
        {
            if (_fd >= 0)
            {
                ::close(_fd);
                _fd = -1;
            }
#ifndef TOUCHSCREEN_USE_POLL
            if (_epoll_fd >= 0)
            {
                ::close(_epoll_fd);
                _epoll_fd = -1;
            }
            log::info("touchscreen closed (epoll backend)");
#else
            log::info("touchscreen closed (poll backend)");
#endif
            _opened = false;
            return err::ERR_NONE;
        }

        bool is_opened()
        {
            return _opened;
        }

        // Drain the kernel buffer and return the latest state.
        err::Err read(int &x, int &y, bool &pressed)
        {
            err::Err e = _read(true);
            if (e != err::ERR_NONE)
                return e;
            x = _x;
            y = _y;
            _apply_transform(x, y);
            pressed = _pressed;
            return err::ERR_NONE;
        }

        std::vector<int> read()
        {
            _read(true);
            int x = _x, y = _y;
            _apply_transform(x, y);
            return {x, y, _pressed ? 1 : 0};
        }

        // Return after the first complete event frame.
        err::Err read0(int &x, int &y, bool &pressed)
        {
            err::Err e = _read(false);
            if (e != err::ERR_NONE)
                return e;
            x = _x;
            y = _y;
            _apply_transform(x, y);
            pressed = _pressed;
            return err::ERR_NONE;
        }

        std::vector<int> read0()
        {
            _read(false);
            int x = _x, y = _y;
            _apply_transform(x, y);
            return {x, y, _pressed ? 1 : 0};
        }

        /**
         * @brief Set whether to swap X and Y axes.
         * @param swap true to swap X/Y, false to keep original.
         */
        void set_swap_xy(bool swap) { _swap_xy = swap; }
        bool get_swap_xy() const    { return _swap_xy; }

        /**
         * @brief Set whether to invert the X axis (x = x_max - x).
         * @param invert true to invert X, false to keep original.
         */
        void set_invert_x(bool invert) { _invert_x = invert; }
        bool get_invert_x() const      { return _invert_x; }

        /**
         * @brief Set whether to invert the Y axis (y = y_max - y).
         * @param invert true to invert Y, false to keep original.
         */
        void set_invert_y(bool invert) { _invert_y = invert; }
        bool get_invert_y() const      { return _invert_y; }

        // timeout < 0: block; timeout == 0: non-blocking; timeout > 0: ms
        bool available(int timeout = 0)
        {
#ifndef TOUCHSCREEN_USE_POLL
            return _available_epoll(timeout);
#else
            return _available_poll(timeout);
#endif
        }

    private:
        int         _fd;
        bool        _opened;
        std::string _device;
        int         _x;
        int         _y;
        bool        _pressed;
        int         _x_max;
        int         _y_max;
        bool        _swap_xy;
        bool        _invert_x;
        bool        _invert_y;

#ifndef TOUCHSCREEN_USE_POLL
        int         _epoll_fd;

        void _init_epoll(int fd)
        {
            log::info("touchscreen epoll init start: fd=%d", fd);

            _epoll_fd = epoll_create(1);
            if (_epoll_fd < 0)
            {
                log::error("touchscreen epoll_create failed: fd=%d err=%s(%d)",
                           fd, strerror(errno), errno);
                return;
            }
            log::info("touchscreen epoll_create success: epoll_fd=%d", _epoll_fd);

            struct epoll_event ev;
            ev.events  = EPOLLIN;
            ev.data.fd = fd;
            int ret = epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
            if (ret < 0)
            {
                log::error("touchscreen epoll_ctl(ADD) failed: fd=%d epoll_fd=%d err=%s(%d)",
                           fd, _epoll_fd, strerror(errno), errno);
                ::close(_epoll_fd);
                _epoll_fd = -1;
                return;
            }
            log::info("touchscreen epoll_ctl(ADD) success: fd=%d epoll_fd=%d events=EPOLLIN",
                      fd, _epoll_fd);
        }

        bool _available_epoll(int timeout)
        {
            if (_epoll_fd < 0)
            {
                log::error("touchscreen available(epoll): epoll_fd invalid (%d), epoll init failed earlier", _epoll_fd);
                return false;
            }
            struct epoll_event events[1];
            int nfds = epoll_wait(_epoll_fd, events, 1, timeout);
            if (nfds < 0)
            {
                log::error("touchscreen epoll_wait failed: epoll_fd=%d timeout=%d err=%s(%d)",
                           _epoll_fd, timeout, strerror(errno), errno);
                return false;
            }
            // log::debug("touchscreen epoll_wait: epoll_fd=%d timeout=%d nfds=%d", _epoll_fd, timeout, nfds);
            return nfds > 0;
        }
#endif // !TOUCHSCREEN_USE_POLL

#ifdef TOUCHSCREEN_USE_POLL
        bool _available_poll(int timeout)
        {
            if (_fd < 0)
            {
                log::error("touchscreen available(poll): fd invalid (%d)", _fd);
                return false;
            }
            struct pollfd pfd;
            pfd.fd      = _fd;
            pfd.events  = POLLIN;
            pfd.revents = 0;
            int ret = poll(&pfd, 1, timeout);
            if (ret < 0)
            {
                log::error("touchscreen poll failed: fd=%d timeout=%d err=%s(%d)",
                           _fd, timeout, strerror(errno), errno);
                return false;
            }
            log::debug("touchscreen poll: fd=%d timeout=%d ret=%d revents=0x%x",
                       _fd, timeout, ret, pfd.revents);
            return ret > 0 && (pfd.revents & POLLIN);
        }
#endif // TOUCHSCREEN_USE_POLL

        // Apply swap/invert transformations to a coordinate pair.
        void _apply_transform(int &x, int &y)
        {
            if (_invert_x) x = _x_max - x;
            if (_invert_y) y = _y_max - y;
            if (_swap_xy)  std::swap(x, y);
        }

        bool _query_abs(int code, int out[6])
        {
            int ret = ioctl(_fd, EVIOCGABS(code), out);
            if (ret < 0)
            {
                log::debug("touchscreen EVIOCGABS(0x%x) failed: fd=%d err=%s(%d)",
                           code, _fd, strerror(errno), errno);
                return false;
            }
            log::debug("touchscreen EVIOCGABS(0x%x): val=%d min=%d max=%d fuzz=%d flat=%d res=%d",
                       code, out[0], out[1], out[2], out[3], out[4], out[5]);
            return true;
        }

        // empty_buffer=true  -> drain all, keep latest state (used by read())
        // empty_buffer=false -> stop at first EV_SYN frame (used by read0())
        err::Err _read(bool empty_buffer)
        {
            if (_fd < 0)
            {
                log::error("touchscreen _read: fd invalid (%d)", _fd);
                return err::ERR_IO;
            }

            struct input_event ev;
            bool got_syn = false;

            while (true)
            {
                ssize_t ret = ::read(_fd, &ev, sizeof(ev));
                if (ret != (ssize_t)sizeof(ev))
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // No more data in kernel buffer.
                        // If we already processed at least one complete frame, report success.
                        log::debug("touchscreen _read: buffer drained, got_syn=%d", got_syn ? 1 : 0);
                        return got_syn ? err::ERR_NONE : err::ERR_NOT_READY;
                    }
                    log::error("touchscreen _read: read failed: fd=%d ret=%zd err=%s(%d)",
                               _fd, ret, strerror(errno), errno);
                    return err::ERR_IO;
                }

                log::debug("touchscreen event: type=%u code=%u value=%d", ev.type, ev.code, ev.value);

                switch (ev.type)
                {
                case EV_ABS:
                    if (ev.code == ABS_MT_TRACKING_ID)
                    {
                        bool new_pressed = (ev.value != -1);
                        log::debug("touchscreen ABS_MT_TRACKING_ID: value=%d -> pressed=%d->%d",
                                   ev.value, _pressed ? 1 : 0, new_pressed ? 1 : 0);
                        _pressed = new_pressed;
                    }
                    else if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
                    {
                        log::debug("touchscreen X: code=0x%x value=%d", ev.code, ev.value);
                        _x = ev.value;
                    }
                    else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
                    {
                        log::debug("touchscreen Y: code=0x%x value=%d", ev.code, ev.value);
                        _y = ev.value;
                    }
                    break;

                case EV_KEY:
                    if (ev.code == BTN_TOUCH || ev.code == BTN_TOOL_FINGER)
                    {
                        log::debug("touchscreen EV_KEY: code=0x%x value=%d -> pressed=%d",
                                   ev.code, ev.value, ev.value == 1 ? 1 : 0);
                        _pressed = (ev.value == 1);
                    }
                    break;

                case EV_SYN:
                    log::debug("touchscreen EV_SYN: x=%d y=%d pressed=%d", _x, _y, _pressed ? 1 : 0);
                    got_syn = true;
                    if (!empty_buffer)
                        return err::ERR_NONE; // stop at first complete frame
                    break;

                default:
                    log::debug("touchscreen unknown event: type=%u code=%u value=%d",
                               ev.type, ev.code, ev.value);
                    break;
                }
            }
        }
    };
} // namespace maix::touchscreen
