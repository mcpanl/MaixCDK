#include "z_display.hpp"
#include "maix_basic.hpp"
#include "z_record_control.hpp"
#include "priv.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <unistd.h>
#include <limits.h>
#include <iostream>
#include <string>


using namespace maix;
using namespace maix::ext_dev;

bool find_max_record_device_(int &card, int &device) {
    // log::info("Find other record device");
    std::ifstream pcm_file("/proc/asound/pcm");
    if (!pcm_file.is_open()) {
        std::cerr << "Failed to open /proc/asound/pcm" << std::endl;
        return false;
    }

    std::string line;
    std::regex regex_line(R"((\d+)-(\d+):.*capture)");
    // Example match: "02-00: USB Audio : ... : capture 1"

    int max_card = -1, max_device = -1;

    while (std::getline(pcm_file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, regex_line)) {
            int c = std::stoi(match[1].str());
            int d = std::stoi(match[2].str());

            if (c > max_card || (c == max_card && d > max_device)) {
                max_card = c;
                max_device = d;
            }
        }
    }

    pcm_file.close();

    if (max_card >= 0) {
        card = max_card;
        device = max_device;
        return true;
    } else {
        std::cerr << "No capture devices found in /proc/asound/pcm" << std::endl;
        return false;
    }
}

// 获取当前日期时间字符串，格式为 YYYY-MM-DD HH:MM:SS
std::string get_current_datetime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    return ss.str();
}

std::array<uint8_t, 8> hexStringToBytesArray(const std::string& str) {
    if (str.size() != 16) {
        throw std::invalid_argument("device_key must be 16 hex chars");
    }
    std::array<uint8_t, 8> out{};
    for (int i = 0; i < 8; i++) {
        auto hexCharToValue = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::invalid_argument("Invalid hex character");
        };
        uint8_t high = hexCharToValue(str[i * 2]);
        uint8_t low  = hexCharToValue(str[i * 2 + 1]);
        out[i] = (high << 4) | low;
    }
    return out;
}



namespace z {

    Display::Display() {
        printf("==== Display ====\n");

        disp = new display::Display(-1, -1, image::FMT_RGB888);
        width = disp->width();
        height = disp->height();
        centerX = width / 2;
        centerY = height / 2;

        printf("W: %d, H: %d\n", width, height);

        disp2 = disp->add_channel(-1, -1, image::FMT_BGRA8888);

        _running = true;
        _thread = std::thread(&Display::frameLoop, this);
    }

    Display::~Display() {
        printf("~~~~ Display ~~~~\n");

        _running = false;
        if (_thread.joinable()) {
            _thread.join();
        }


        delete disp;
        delete disp2;

        delete deviceKeyImg;
        delete fpsImg;
        delete batImg;
        delete freeImg;
        releaseGlyphCache();
    }

    void Display::open_backlight() {
        disp->set_backlight(50);
    }

    void Display::close_backlight() {
        disp->set_backlight(0);
    }

    void Display::toggle_backlight() {
        if (disp->get_backlight() == 0) {
            open_backlight();
        } else {
            close_backlight();
        }
    }

    std::string Display::getFreeSpaceString() {
        long free_bytes = disk_total - disk_used;
        double size = static_cast<double>(free_bytes);
        std::string unit = "B";

        if (size >= 1024) {
            size /= 1024;
            unit = "KB";
        }
        if (size >= 1024) {
            size /= 1024;
            unit = "MB";
        }
        if (size >= 1024) {
            size /= 1024;
            unit = "GB";
        }
        if (size >= 1024) {
            size /= 1024;
            unit = "TB";
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << unit;
        return oss.str();
    }

    void Display::frameLoop() {
        uint64_t last_pmu_ms = 0;
        uint64_t curr_ms = 0;

        pmu = new pmu::PMU("axp2101");
        device_key = sys::device_key();
        printf("DEVICE KEY = %s\n", device_key.c_str());

        device_key_binary = hexStringToBytesArray(device_key);

        std::map<std::string, unsigned long long> disk_usage = sys::disk_usage("/");
        if (disk_usage.find("total") != disk_usage.end()) {
            disk_total = disk_usage["total"];
        }

        if (disk_usage.find("used") != disk_usage.end()) {
            disk_used = disk_usage["used"];
        }

        printf("DISK %ld / %ld\n", disk_used, disk_total);

        std::cout << "剩余空间: " << getFreeSpaceString() << std::endl;


        // 预分配小图对象
        deviceKeyImg = new image::Image(200, 40, image::FMT_BGRA8888);
        fpsImg       = new image::Image(120, 40, image::FMT_BGRA8888);
        batImg       = new image::Image(120, 40, image::FMT_BGRA8888);
        freeImg      = new image::Image(200, 40, image::FMT_BGRA8888);
        wifiOffImg   = image::load("assets/wifi_off.png", image::Format::FMT_RGBA8888)->resize(55, 55);
        networkOffImg   = image::load("assets/network_off.png", image::Format::FMT_RGBA8888)->resize(55, 55);
        micOffImg   = image::load("assets/mic_off.png", image::Format::FMT_RGBA8888)->resize(55, 55);

        // 渲染固定 device_key，一次即可
        deviceKeyImg->clear();
        deviceKeyImg->draw_string(0, 0, device_key.c_str(), image::COLOR_WHITE, 2, 2);
        initGlyphCache();

        while (_running && !app::need_exit()) {
            curr_ms = time::ticks_ms();
            if (curr_ms - last_pmu_ms > 5000) {
                if (pmu) {
                    bat_percent = pmu->get_bat_percent();
                    is_charging = pmu->is_charging();
                    is_vbus_in = pmu->is_vbus_in();
                    pmu->set_bat_charging_cur(200);
                }

                std::map<std::string, unsigned long long> disk_usage = sys::disk_usage("/");
                if (disk_usage.find("total") != disk_usage.end()) {
                    disk_total = disk_usage["total"];
                }

                if (disk_usage.find("used") != disk_usage.end()) {
                    disk_used = disk_usage["used"];
                }

                int selected_card = 0, selected_device = 0;

                find_max_record_device_(selected_card, selected_device);

                if (selected_card == 0) {
                    micOff = true;
                } else {
                    micOff = false;
                }

                // printf("*** pushed video = %lu, pushed audio = %lu, total_record_ms = %lu\n", priv.video_push_count, priv.audio_push_count, priv.total_record_ms);

                last_pmu_ms = curr_ms;
            }

            runFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    void Display::runFrame() {
        static uint8_t counter = 0;
        counter++;
        if (counter > 8) {
            counter = 0;
        }

        image::Image* dispImage2 = new image::Image(disp->width(), disp->height(), image::FMT_BGRA8888);

        if (priv.key) {
            z::KeyStage stage = priv.key->get_stage();
            if (stage == z::LONG_PRESSING) {
                dispImage2->draw_string(220, 110, "Exit APP", image::COLOR_RED, 3, 3);
                int elapsed = priv.key->get_long_press_ms();

                int section = 3200 / 8;
                int active_count = std::min(8, (elapsed + section - 1) / section);

                int w = disp->width();
                int h = disp->height();
                int rect_h = h / 4;   // 画在整个高度（你也可以改小）
                int rect_w = w / 8;
                int rect_y = (h - rect_h) / 2;

                for (int i = 0; i < 8; i++) {
                    int rect_x = i * rect_w;
                    image::Color c = (i < active_count)
                                     ? image::Color::from_rgb(255, 0, 0)   // 已点亮：红色
                                     : image::Color::from_rgb(50, 50, 50); // 未点亮：灰色
                    dispImage2->draw_rect(rect_x, rect_y, rect_w, rect_h, c, -1);
                }

                if (elapsed >= 3200) {
                    printf(">>> 触发成功！\n");
                    app::set_exit_flag(1);
                }

                disp2->show(*dispImage2, image::FIT_COVER);
                delete dispImage2;
                return;
            }
        }

        if (priv.recordControl) {
            if (priv.recordControl->state() == RecordControl::State::Recording) {
                int totalSec = static_cast<int>(priv.recordControl->duration());
                int minutes = totalSec / 60;
                int seconds = totalSec % 60;

                char recTime[16];
                if (minutes < 100) {
                    // 前面留一个空格 + 两位补零
                    snprintf(recTime, sizeof(recTime), " %02d:%02d", minutes, seconds);
                } else {
                    // 直接正常显示
                    snprintf(recTime, sizeof(recTime), "%d:%02d", minutes, seconds);
                }

                dispImage2->draw_rect(20 - 6, 18 - 6 , 132, 40, maix::image::COLOR_RED, -1);
                draw_text_cached(dispImage2, 20, 18, recTime, false);
            } else {
                draw_text_cached(dispImage2, 20, 18, getFreeSpaceString(), false);
            }
        }

        std::string datetime_str = get_current_datetime();
        draw_text_cached(dispImage2, 260, 18, datetime_str, false);

        // 电池
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "[%3d]", (int)bat_percent);
        bool green = is_charging;
        draw_text_cached(dispImage2, 530, 18, buffer, green);

        // FPS
        std::string str2 = std::to_string((int)_fps) + "FPS";

        draw_text_cached(dispImage2, 510, 72, str2, false);

        draw_text_cached(dispImage2, 20, 72, device_key, false);

        if (counter > 1) {
            if (priv.network) {
                if (priv.network->get_lan_state() == LanState::DISCONNECTED) {
                    dispImage2->draw_image(580, 112, *wifiOffImg);
                }

                if (priv.network->get_wan_state() == WanState::DISCONNECTED) {
                    dispImage2->draw_image(580, 178, *networkOffImg);
                }
            }

            if (micOff) {
                dispImage2->draw_image(580, 242, *micOffImg);
            }
        }

        disp2->show(*dispImage2, image::FIT_COVER);

        delete dispImage2;
    }
#if 0
    void Display::runFrame() {
        image::Image *dispImage2 = new image::Image(640, 480, image::FMT_BGRA8888);

        std::string str2 = std::to_string((int)_fps) + "FPS";
        dispImage2->draw_string(20, 82, getFreeSpaceString().c_str(), image::COLOR_WHITE, 2, 2);

        // 格式化字符串，固定3位宽度，前面用空格补齐
        char buffer[8]; // 足够存 "[100]"
        snprintf(buffer, sizeof(buffer), "[%3d]", (int)bat_percent);

        // 根据是否充电选择颜色
        auto color = is_charging ? image::COLOR_GREEN : image::COLOR_WHITE;

        // 绘制
        dispImage2->draw_string(550, 22, buffer, color, 2, 2);
        dispImage2->draw_string(520, 82, str2.c_str(), image::COLOR_WHITE, 2, 2);

        dispImage2->draw_string(20, 22, device_key.c_str(), image::COLOR_WHITE, 2, 2);

        disp2->show(*dispImage2, image::FIT_COVER);

        delete dispImage2;
    }
#endif

    void Display::showLogo() {
        std::string path = "assets/logo.png";

        image::Image *dispImage = new image::Image(width, height, image::Format::FMT_RGBA8888);
        // image::Image* dispImage2 = new image::Image(disp->width(), disp->height(), image::FMT_BGRA8888);

        image::Image *img = image::load(path.c_str(), image::Format::FMT_RGBA8888);

        if (!img) {
            std::cerr << "Failed to load logo: " << path << std::endl;
            delete dispImage;
            return;
        }

        // 居中绘制 logo
        int x = centerX - img->width() / 2;
        int y = centerY - img->height() / 2;
        dispImage->draw_image(x, y, *img);


        disp->show(*dispImage);

        delete img;
        delete dispImage;
    }

}
