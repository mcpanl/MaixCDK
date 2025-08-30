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

        // 缓存小图
        maix::image::Image* deviceKeyImg;  // 固定文本
        maix::image::Image* fpsImg;        // 动态
        maix::image::Image* batImg;        // 动态
        maix::image::Image* freeImg;       // 动态

        std::unordered_map<char, maix::image::Image*> glyph_white;
        std::unordered_map<char, maix::image::Image*> glyph_green;
        void initGlyphCache() {
            const std::string chars = "FPS%GBMKT:0123456789.abcdefghijklmnopqrstuvwxyz[]/ ";

            for (char c : chars) {
                // 获取字符大小
                auto sz = maix::image::string_size(std::string(1, c).c_str(), 2, 2);
                int w = sz.width();
                int h = sz.height() + 6;

                // 白色版本
                maix::image::Image* img_white = new maix::image::Image(w, h, maix::image::FMT_BGRA8888);
                img_white->clear();
                img_white->draw_string(0, 6, std::string(1, c).c_str(), maix::image::COLOR_WHITE, 2, 2);
                glyph_white[c] = img_white;

                // 绿色版本
                maix::image::Image* img_green = new maix::image::Image(w, h, maix::image::FMT_BGRA8888);
                img_green->clear();
                img_green->draw_string(0, 6, std::string(1, c).c_str(), maix::image::COLOR_GREEN, 2, 2);
                glyph_green[c] = img_green;
            }

            printf("Glyph cache initialized, total %zu chars.\n", glyph_white.size());
        }

        void releaseGlyphCache() {
            for (auto& kv : glyph_white) delete kv.second;
            for (auto& kv : glyph_green) delete kv.second;
            glyph_white.clear();
            glyph_green.clear();
        }

        // 使用缓存绘制文本
        void draw_text_cached(maix::image::Image* target, int x, int y,
                              const std::string& text, bool green = false) {
            auto& cache = green ? glyph_green : glyph_white;

            int cursor_x = x;
            for (char c : text) {
                if (cache.find(c) == cache.end()) continue;  // 忽略未缓存字符

                maix::image::Image* glyph = cache[c];
                target->draw_image(cursor_x, y, *glyph);
                cursor_x += glyph->width();  // 按字宽推进
            }
        }
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
