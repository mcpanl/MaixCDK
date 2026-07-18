#include "maix_basic.hpp"
#include "z_display.hpp"
#include "maix_camera.hpp"
#include "z_image.hpp"
#include "main.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace maix;

static constexpr int kPanelWidth = 172;
static constexpr int kPanelHeight = 320;
static constexpr const char *kDefaultFbDevice = "/dev/fb0";

/* Match sample_sensor_lcd HUD insets (rounded glass corners). */
static constexpr int kHudInsetX = 10;
static constexpr int kHudInsetY = 12;

static bool read_sysfs_int(const char *path, int &out)
{
    std::ifstream f(path);
    if (!f)
        return false;
    int v = 0;
    f >> v;
    if (!f)
        return false;
    out = v;
    return true;
}

static bool read_battery_pct(int &pct)
{
    int present = 0;
    if (!read_sysfs_int("/sys/class/power_supply/axp2101-battery/present", present) || !present)
        return false;
    int cap = 0;
    if (!read_sysfs_int("/sys/class/power_supply/axp2101-battery/capacity", cap))
        return false;
    if (cap > 100)
        cap = 100;
    if (cap < 0)
        cap = 0;
    pct = cap;
    return true;
}

static bool read_temp_c(int &temp_c)
{
    auto temps = sys::cpu_temp();
    auto it = temps.find("cpu");
    if (it == temps.end())
        return false;
    temp_c = (int)(it->second + 0.5f);
    return true;
}

static image::Color battery_color(int pct)
{
    if (pct <= 15)
        return image::COLOR_RED;
    if (pct <= 30)
        return image::COLOR_ORANGE;
    if (pct <= 60)
        return image::COLOR_YELLOW;
    return image::COLOR_GREEN;
}

static const image::Color kColorCyan = image::Color::from_rgb(0, 255, 255);

/**
 * sample_sensor_lcd layout:
 *   battery  top-right
 *   temp     bottom-left (cyan)
 * Plus corner tags to check FB / image orientation vs mirror.
 */
static void draw_status_hud(image::Image &img)
{
    const int w = img.width();
    const int h = img.height();
    const float scale = 1.0f;

    int temp_c = 0;
    bool temp_ok = read_temp_c(temp_c);
    char temp_text[16];
    if (temp_ok)
        snprintf(temp_text, sizeof(temp_text), "%dC", temp_c);
    else
        snprintf(temp_text, sizeof(temp_text), "--C");

    int bat_pct = 0;
    bool bat_ok = read_battery_pct(bat_pct);
    char bat_text[16];
    if (bat_ok)
        snprintf(bat_text, sizeof(bat_text), "%d%%", bat_pct);
    else
        snprintf(bat_text, sizeof(bat_text), "--");

    /* Approx glyph width for default font at scale 1 (~8px). */
    const int char_w = 8;
    const int char_h = 10;
    const int bat_tw = (int)strlen(bat_text) * char_w;
    const int temp_tw = (int)strlen(temp_text) * char_w;

    const int bat_x = w - kHudInsetX - bat_tw;
    const int bat_y = kHudInsetY;
    const int temp_x = kHudInsetX;
    const int temp_y = h - kHudInsetY - char_h;

    img.draw_rect(bat_x - 1, bat_y - 1, bat_tw + 2, char_h + 2, image::COLOR_BLACK, -1);
    img.draw_rect(temp_x - 1, temp_y - 1, temp_tw + 2, char_h + 2, image::COLOR_BLACK, -1);

    img.draw_string(bat_x, bat_y, bat_text,
                    bat_ok ? battery_color(bat_pct) : image::COLOR_WHITE, scale);
    img.draw_string(temp_x, temp_y, temp_text, kColorCyan, scale);

    /* Orientation markers: if mirror/flip wrong these swap sides. */
    img.draw_string(2, 2, "TL", image::COLOR_WHITE, scale);
    img.draw_string(w - 2 - 2 * char_w, 2, "TR", image::COLOR_WHITE, scale);
    img.draw_string(2, h - 2 - char_h, "BL", image::COLOR_WHITE, scale);
    img.draw_string(w - 2 - 2 * char_w, h - 2 - char_h, "BR", image::COLOR_WHITE, scale);
}

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

static void print_usage(const char *prog)
{
    printf("Usage: %s [fb_device] [-m] [-f]\n", prog);
    printf("  -m    Camera horizontal mirror (VPSS)\n");
    printf("  -f    Camera vertical flip (VPSS)\n");
    printf("HUD: battery top-right, temp bottom-left (sample_sensor_lcd layout).\n");
    printf("Corner tags TL/TR/BL/BR for orientation check.\n");
}

int _main(int argc, char *argv[])
{
    const char *fb_device = kDefaultFbDevice;
    bool mirror = false;
    bool flip = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-m") == 0) {
            mirror = true;
            continue;
        }
        if (strcmp(argv[i], "-f") == 0) {
            flip = true;
            continue;
        }
        if (argv[i][0] != '-')
            fb_device = argv[i];
    }

    log::info("FB display demo: device=%s expect %dx%d mirror=%d flip=%d\n",
              fb_device, kPanelWidth, kPanelHeight, (int)mirror, (int)flip);

    display::Display screen(kPanelWidth, kPanelHeight, image::FMT_RGB888, fb_device);
    log::info("screen opened: %dx%d format=%d\n", screen.width(), screen.height(), (int)screen.format());

    /* Zonhor: screen and camera share portrait orientation — width x height
     * matches how you hold the device (1080 wide, 1920 tall for full 1080p).
     * MaixCam used Camera(1920, 1080) because the display path rotated 90°. */
    static constexpr int kCamWidth = 1080;
    static constexpr int kCamHeight = 1920;
    camera::Camera cam(kCamWidth, kCamHeight, image::FMT_RGB888);
    log::info("camera opened: %dx%d (user coords)\n", cam.width(), cam.height());
    if (mirror)
        cam.hmirror(1);
    if (flip)
        cam.vflip(1);

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

        image::Image *cam_img = cam.read();
        if (cam_img) {
            image::Image *cam_img_small = cam_img->resize(108,192);
            img.draw_image(0, 0, *cam_img_small);
            delete cam_img_small;
            delete cam_img;
        }

        draw_status_hud(img);

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
