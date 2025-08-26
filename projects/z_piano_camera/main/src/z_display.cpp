#include "z_display.hpp"
#include "maix_basic.hpp"

using namespace maix;
using namespace maix::ext_dev;

namespace z {

    Display::Display() {
        printf("==== Display ====\n");

        disp = new display::Display(640, 480, image::FMT_RGB888);
        width = disp->width();
        height = disp->height();
        centerX = width / 2;
        centerY = height / 2;

        printf("W: %d, H: %d\n", width, height);

        disp2 = disp->add_channel(640, 480, image::FMT_BGRA8888);

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

        std::map<std::string, unsigned long long> disk_usage = sys::disk_usage("/");
        if (disk_usage.find("total") != disk_usage.end()) {
            disk_total = disk_usage["total"];
        }

        if (disk_usage.find("used") != disk_usage.end()) {
            disk_used = disk_usage["used"];
        }

        printf("DISK %ld / %ld\n", disk_used, disk_total);

        std::cout << "剩余空间: " << getFreeSpaceString() << std::endl;

        while (_running && !app::need_exit()) {
            curr_ms = time::ticks_ms();
            if (curr_ms - last_pmu_ms > 10000) {
                if (pmu) {
                    bat_percent = pmu->get_bat_percent();
                    is_charging = pmu->is_charging();
                    is_vbus_in = pmu->is_vbus_in();
                }

                std::map<std::string, unsigned long long> disk_usage = sys::disk_usage("/");
                if (disk_usage.find("total") != disk_usage.end()) {
                    disk_total = disk_usage["total"];
                }

                if (disk_usage.find("used") != disk_usage.end()) {
                    disk_used = disk_usage["used"];
                }

                last_pmu_ms = curr_ms;
            }

            runFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    }

    void Display::runFrame() {
        // image::Image *dispImage = new image::Image(width, height, image::FMT_BGRA8888);
        image::Image *dispImage2 = new image::Image(640, 480, image::FMT_BGRA8888);
        // std::string str = "FPS=" + std::to_string(_fps);
        // dispImage->draw_string(0, 0, str.c_str(), image::COLOR_WHITE);
        // dispImage2->draw_rect(0,0,640, 480, image::COLOR_RED, -1);

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


        // disp->show(*dispImage);
        disp2->show(*dispImage2, image::FIT_COVER);
        // delete dispImage;
        delete dispImage2;
    }

    void Display::showLogo(const std::string &path) {
        image::Image *dispImage = new image::Image(width, height, image::Format::FMT_RGBA8888);
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
