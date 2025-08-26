#pragma once

#include "maix_image.hpp"
#include "maix_display.hpp"
#include "maix_pmu.hpp"
#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <iomanip>


namespace z {
    class Display {
    private:
        maix::display::Display *disp;
        maix::display::Display *disp2;
        maix::ext_dev::pmu::PMU *pmu;

        int width;
        int height;
        int centerX;
        int centerY;

        int bat_percent;
        bool is_charging;
        bool is_vbus_in;

        std::string device_key;
        long disk_total;
        long disk_used;

        double _fps;
        std::thread _thread;
        std::atomic<bool> _running;
        void frameLoop();

    public:
        Display();
        ~Display();

        std::string getFreeSpaceString();

        // 获取屏幕参数
        int w() const { return width; }
        int h() const { return height; }
        int cX() const { return centerX; }
        int cY() const { return centerY; }

        void runFrame();
        void setFps(double fps) { _fps = fps; }

        // 显示 logo
        void showLogo(const std::string &path);
    };
}
