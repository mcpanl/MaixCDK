#include "pointing_device.hpp"


#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdbool.h>
#include <dirent.h>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <filesystem>


#include "maix_basic.hpp"
#include "z_lvgl.hpp"
#include "monitor.h"
#include "mouse.h"



LV_IMAGE_DECLARE(cursor_96);
LV_IMAGE_DECLARE(cursor_48);


struct PointingData {
    int x;
    int y;
    bool pressed;
    bool continue_reading;
};

enum class PointingType {
    REL,
    ABS
};

class PointingDevice {
public:
    virtual std::string name() const noexcept = 0;
    virtual PointingType type() const noexcept = 0;
    virtual bool need_cursor() const noexcept = 0;
    virtual bool check_initialized() const noexcept = 0;
    virtual bool try_init() = 0;
    virtual PointingData read() = 0;

    virtual ~PointingDevice() {}
};


namespace pointing_private {
    inline static std::vector<std::unique_ptr<PointingDevice>> device_list;
    inline static lv_obj_t *cursor_rect = nullptr;
    inline static int windows_w{0};
    inline static int windows_h{0};


    /**
     * @brief USB Mouse IMPL
     *
     */
    class UsbMouseDevice final : public PointingDevice {
    public:
        virtual std::string name() const noexcept override {
            return "USB Mouse";
        }
        virtual PointingType type() const noexcept override {
            return PointingType::REL;
        }
        virtual bool check_initialized() const noexcept override {
            return this->_init;
        }
        virtual bool need_cursor() const noexcept override {
            return true;
        }
        virtual bool try_init() override {
            if (this->_init)
                return true;

            auto now = maix::time::ticks_ms();
            if (now - this->_prev_time_ms < this->_TIME_INTERVAL_MS)
                return false;

            // maix::log::info("try_init... %llu", now);
            this->_prev_time_ms = now;

            const auto dev = this->find_usb_mice();
            if (dev.empty())
                return false;
            this->_mouse_fd = ::open(dev.c_str(), O_RDONLY | O_NONBLOCK);
            if (this->_mouse_fd < 0) {
                maix::log::error("Unable to open mouse device: %s", dev.c_str());
                return false;
            }

            ::monitor_rect(&this->_w, &this->_h);
            this->_data.x = 0;
            this->_data.y = 0;
            this->_data.pressed = false;
            this->_data.continue_reading = false;

            this->_dev_path = dev;
            this->_init = true;
            maix::log::info("USB Mouse<%s> connected!", dev.c_str());

            this->_empty_data.x = this->_empty_data.y = -1;
            this->_empty_data.pressed = this->_empty_data.continue_reading = false;
            return true;
        }
        virtual PointingData read() override {
            if (!std::filesystem::exists(this->_dev_path)) {
                maix::log::info("USB Mouse<%s> disconnected!", this->_dev_path.c_str());
                this->_init = false;
                this->_blacklist.clear();
                if (this->_mouse_fd >= 0)
                    ::close(this->_mouse_fd);
                this->_mouse_fd = -1;
                this->_dev_path.clear();
                this->_prev_cnt = 0;
                this->_prev_time_ms = 0;

                return this->_empty_data;
            }

            struct input_event ie;
            ssize_t n = ::read(this->_mouse_fd, &ie, sizeof(struct input_event));

            if (n != (ssize_t)sizeof(struct input_event)) {
                PointingData data2;
                data2.x = -1;
                data2.y = -1;
                data2.pressed = false;
                data2.continue_reading = false;
                return data2;
            }

            this->_data.continue_reading = true;
            if (ie.type == EV_REL) {
                if (ie.code == REL_X) {
                    this->_data.x = ie.value/2;
                } else if (ie.code == REL_Y) {
                    this->_data.y = ie.value/2;
                }
                // } else if (ie.code == REL_WHEEL) {
                //     const int wheel_data = ie.value * (h/10);
                //     lv_obj_t* child = lv_obj_get_child(lv_scr_act(), 0);
                //     lv_obj_scroll_by(child, 0, wheel_data, LV_ANIM_ON);
                // }
            } else if (ie.type == EV_KEY) {
                if (ie.code == BTN_LEFT){
                    this->_data.pressed = (ie.value == 1);
                }
            }
            // else return this->_empty_data;

            return this->_data;
        }
    private:
        bool is_usb_mouse(const std::string& device) {
            // maix::log::info("input device: %s", device.c_str());
            // maix::time::sleep_ms(10);
            std::string command = "udevadm info --name=" + device + " 2>/dev/null";
            FILE* pipe = ::popen(command.c_str(), "r");
            if (!pipe) {
                return false;
            }

            char buffer[256];
            std::ostringstream result;
            while (::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result << buffer;
            }

            ::pclose(pipe);

            std::string output = result.str();

            // maix::log::info("input device info:\n%s", output.c_str());

            if (output.find("ID_INPUT_MOUSE=1") != std::string::npos) {
                // maix::log::info("input device<%s> is an USB Mouse!", device.c_str());
                return true;
            }
            // maix::log::info("input device<%s> is not a USB Mouse!", device.c_str());
            return false;
        }
        std::string find_usb_mice() {
            DIR* dir;
            struct dirent* ent;
            static bool need_recheck_by_deley = false;
            static const uint64_t DELAY_MS = 500;
            static uint64_t ltime = 0;

            if ((dir = ::opendir("/dev/input")) == nullptr) {
                maix::log::error("Could not open /dev/input");
                return "";
            }

            int cnt = 0;
            while ((ent = ::readdir(dir)) != nullptr) {
                ++cnt;
                if (::strncmp(ent->d_name, "event", 5) != 0 && ::strcmp(ent->d_name, "mice") != 0)
                    continue;
                std::string device_path = "/dev/input/" + std::string(ent->d_name);
                if (std::find(this->_blacklist.begin(), this->_blacklist.end(), device_path) != this->_blacklist.end())
                    continue;
                // maix::log::info("device: %s", device_path.c_str());
                if (this->is_usb_mouse(device_path))
                    return device_path;
                this->_blacklist.push_back(device_path);
            }
            ::closedir(dir);

            if (cnt > this->_prev_cnt) {
                // maix::log::info("need recheck input device!");
                need_recheck_by_deley = true;
                ltime = maix::time::ticks_ms();
            }

            if (need_recheck_by_deley && maix::time::ticks_ms()-ltime>=DELAY_MS) {
                // maix::log::info("recheck input device start!");
                need_recheck_by_deley = false;
                this->_blacklist.clear();
            }

            // maix::log::info("device cnt: %d", cnt);
            this->_prev_cnt = cnt;
            return "";
        }
    private:
        bool _init{false};
        int _mouse_fd{-1};
        std::vector<std::string> _blacklist;
        int _w{0};
        int _h{0};
        PointingData _data;
        std::filesystem::path _dev_path;
        int _prev_cnt{0};
        uint64_t _prev_time_ms{0};
        static const uint64_t _TIME_INTERVAL_MS = 1000;
        PointingData _empty_data;
    };

    /**
     * @brief TouchScreen IMPL
     *
     */
    class TouchScreenDevice : public PointingDevice {
    public:
        TouchScreenDevice() = default;
        TouchScreenDevice(const TouchScreenDevice&) = delete;
        TouchScreenDevice& operator=(const TouchScreenDevice&) = delete;

        virtual std::string name() const noexcept override {
            return "TouchScreen";
        }
        virtual PointingType type() const noexcept override {
            return PointingType::ABS;
        }
        virtual bool check_initialized() const noexcept override {
            return z_touchscreen != nullptr;
        }
        virtual bool try_init() override {
            if (!z_touchscreen) {
                const auto now = maix::time::ticks_ms();
                if (now - this->_last_null_log_ms >= this->_LOG_INTERVAL_MS) {
                    maix::log::info("TouchScreenDevice try_init failed: z_touchscreen is nullptr");
                    this->_last_null_log_ms = now;
                }
            } else if (!this->_logged_init_ready) {
                maix::log::info("TouchScreenDevice try_init success: z_touchscreen is ready");
                this->_logged_init_ready = true;
            }
            return z_touchscreen != nullptr;
        }
        virtual bool need_cursor() const noexcept override {
            return false;
        }
        virtual PointingData read() {
            if (!z_touchscreen) {
                const auto now = maix::time::ticks_ms();
                if (now - this->_last_null_log_ms >= this->_LOG_INTERVAL_MS) {
                    maix::log::info("TouchScreenDevice read skipped: z_touchscreen is nullptr");
                    this->_last_null_log_ms = now;
                }
                PointingData data2;
                data2.x = -1;
                data2.y = -1;
                data2.pressed = false;
                data2.continue_reading = false;
                return data2;
            }

            int x = 0;
            int y = 0;
            bool pressed = false;
            if(z_touchscreen->read0(x, y, pressed) == maix::err::ERR_NOT_READY) {
                const auto now = maix::time::ticks_ms();
                if (now - this->_last_not_ready_log_ms >= this->_LOG_INTERVAL_MS) {
                    maix::log::info("TouchScreenDevice read0 not ready");
                    this->_last_not_ready_log_ms = now;
                }
                PointingData data2;
                data2.x = -1;
                data2.y = -1;
                data2.pressed = false;
                data2.continue_reading = false;
                return data2;
            }
            this->_data.continue_reading = z_touchscreen->available();

            this->_data.x = x;
            this->_data.y = y;
            this->_data.pressed = pressed;

            const auto now = maix::time::ticks_ms();
            if (!this->_logged_first_read) {
                maix::log::info("TouchScreenDevice first valid read: x=%d, y=%d, pressed=%d, available=%d",
                                x, y, pressed, this->_data.continue_reading);
                this->_logged_first_read = true;
                this->_last_state_log_ms = now;
            } else if ((pressed != this->_last_pressed_state || this->_data.continue_reading != this->_last_available_state)
                       && now - this->_last_state_log_ms >= 100) {
                maix::log::info("TouchScreenDevice state changed: x=%d, y=%d, pressed=%d, available=%d",
                                x, y, pressed, this->_data.continue_reading);
                this->_last_state_log_ms = now;
            } else if (now - this->_last_state_log_ms >= this->_LOG_INTERVAL_MS) {
                maix::log::info("TouchScreenDevice heartbeat: x=%d, y=%d, pressed=%d, available=%d",
                                x, y, pressed, this->_data.continue_reading);
                this->_last_state_log_ms = now;
            }

            this->_last_pressed_state = pressed;
            this->_last_available_state = this->_data.continue_reading;

            return this->_data;
        }
    private:
        PointingData _data;
        uint64_t _last_null_log_ms{0};
        uint64_t _last_not_ready_log_ms{0};
        uint64_t _last_state_log_ms{0};
        bool _logged_init_ready{false};
        bool _logged_first_read{false};
        bool _last_pressed_state{false};
        bool _last_available_state{false};
        static const uint64_t _LOG_INTERVAL_MS = 1000;
    };

    /* Add your device */
    // class YourDevice : public PointingDevice {
    // };
}

void pointing_device_init(lv_indev_t * indev_drv)
{
    using namespace pointing_private;
    if (z_touchscreen) {
        maix::log::info("pointing_device_init: touchscreen detected, register TouchScreenDevice");
        device_list.emplace_back(std::make_unique<TouchScreenDevice>());
    } else {
        maix::log::info("pointing_device_init: z_touchscreen is nullptr, touchscreen device not registered");
    }
    device_list.emplace_back(std::make_unique<UsbMouseDevice>());
    /* add your device */
    // device_list.emplace_back(std::make_unique<YourDevice>());

    cursor_rect = lv_img_create(lv_scr_act());

    ::monitor_rect(&windows_w, &windows_h);
    if (windows_w > 640) {
        lv_img_set_src(cursor_rect, &cursor_96);
        maix::log::info("use cursor size 96x96");
    } else {
        lv_img_set_src(cursor_rect, &cursor_48);
        maix::log::info("use cursor size 48x48");
    }
    lv_obj_set_style_bg_color(cursor_rect, lv_color_hex(0xFF0000), 0);
    lv_indev_set_cursor(indev_drv, cursor_rect);
    lv_obj_add_flag(cursor_rect, LV_OBJ_FLAG_HIDDEN);
}

void pointing_device_read([[maybe_unused]]lv_indev_t * indev_drv, lv_indev_data_t * data)
{
    using namespace pointing_private;
    static PointingData d {
        .x = 0,
        .y = 0,
        .pressed = false,
        .continue_reading = false
    };
    static bool prev_cursor_sta = false;

    d.continue_reading = false;
    for (auto& i : device_list) {
        if (!i->check_initialized()) {
            if (!i->try_init())
            continue;
        }
        auto __d__ = i->read();
        if (__d__.x==-1 && __d__.y==-1)
            continue;
        if (i->type() == PointingType::ABS) {
            d.x = __d__.x;
            d.y = __d__.y;
            d.pressed = __d__.pressed;
        } else {
            d.x += __d__.x;
            d.x = (d.x>windows_w) ? windows_w : d.x;
            d.x = (d.x<0) ? 0 : d.x;
            d.y += __d__.y;
            d.y = (d.y>windows_h) ? windows_h : d.y;
            d.y = (d.y<0) ? 0 : d.y;
            d.pressed = __d__.pressed;
        }
        d.continue_reading = __d__.continue_reading;
        if (i->need_cursor() && !prev_cursor_sta) {
            lv_obj_remove_flag(cursor_rect, LV_OBJ_FLAG_HIDDEN);
            prev_cursor_sta = true;
        } else if (!i->need_cursor() && prev_cursor_sta) {
            lv_obj_add_flag(cursor_rect, LV_OBJ_FLAG_HIDDEN);
            prev_cursor_sta = false;
        }
        break;
    }

    data->point.x = (lv_coord_t)d.x;
    data->point.y = (lv_coord_t)d.y;
    data->state = d.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = d.continue_reading;

}