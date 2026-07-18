#include "maix_basic.hpp"
#include "z_display.hpp"
#include "z_image.hpp"
#include "main.h"

#include <cmath>

using namespace maix;

static constexpr int kPanelWidth = 172;
static constexpr int kPanelHeight = 320;
static constexpr const char *kDefaultFbDevice = "/dev/fb0";

static void draw_color_bars(image::Image &img)
{
    const int bar_h = img.height() / 6;
    const image::Color colors[] = {
        image::COLOR_RED,
        image::COLOR_GREEN,
        image::COLOR_BLUE,
        image::COLOR_YELLOW,
        image::COLOR_PURPLE,
        image::COLOR_WHITE,
    };

    for (int i = 0; i < 6; ++i) {
        int y = i * bar_h;
        int h = (i == 5) ? (img.height() - y) : bar_h;
        img.draw_rect(0, y, img.width(), h, colors[i], -1);
    }
}

int _main(int argc, char *argv[])
{
    const char *fb_device = (argc > 1) ? argv[1] : kDefaultFbDevice;

    log::info("FB display demo: device=%s expect %dx%d\n", fb_device, kPanelWidth, kPanelHeight);

    display::Display screen(kPanelWidth, kPanelHeight, image::FMT_RGB888, fb_device);
    log::info("screen opened: %dx%d format=%d\n", screen.width(), screen.height(), (int)screen.format());

    image::Image img(screen.width(), screen.height(), image::FMT_RGB888);
    int frame = 0;

    while (!app::need_exit()) {
        draw_color_bars(img);

        int cx = img.width() / 2;
        int cy = img.height() / 2;
        int radius = img.width() / 6;
        double angle = frame * 6.0 * M_PI / 180.0;
        int px = cx + (int)(radius * std::cos(angle));
        int py = cy + (int)(radius * std::sin(angle));

        img.draw_circle(px, py, 8, image::COLOR_BLACK, -1);
        img.draw_line(cx, cy, px, py, image::COLOR_BLACK, 2);
        img.draw_rect(4, 4, img.width() - 8, 20, image::COLOR_WHITE, -1);

        err::Err e = screen.show(img);
        if (e != err::ERR_NONE) {
            log::error("screen.show failed: %d\n", (int)e);
            return -1;
        }

        ++frame;
        time::sleep_ms(50);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
