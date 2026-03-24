
#include "maix_basic.hpp"
#include "z_camera.hpp"
#include "z_display.hpp"
#include "z_image.hpp"
#include "z_touchscreen.hpp"
#include "maix_key.hpp"
#include "main.h"

using namespace maix;
using namespace maix::peripheral;

static constexpr int DISP_W = 640;
static constexpr int DISP_H = 480;
static constexpr int HEADER_H = 60;

// "User" key is board-specific. On some boards it's input-event code 352.
static constexpr int USER_KEY_CODE = 352;

const int back_x = 530;
const int back_y = 0;
const int back_w = 110;
const int back_h = 60;

static void on_key(int key, int state)
{
    if (state != maix::peripheral::key::KEY_PRESSED)
        return;

    if (key == USER_KEY_CODE)
    {
        app::set_exit_flag(true);
    }
}

static inline int _clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void draw_header(image::Image &img, const std::string &title)
{
    // Header background (dark) to keep contrast.
    img.draw_rect(0, 0, img.width(), HEADER_H, image::COLOR_BLACK, -1);

    // Close button (red background, white text)
    img.draw_rect(back_x, back_y, back_w, back_h, image::COLOR_RED, -1);
    img.draw_string(back_x + 10, back_y + 16, "Close", image::COLOR_WHITE, 2.0, -1);

    // Title (white text)
    const float scale = 2.0;
    const int thickness = 2;
    image::Size tsz = image::string_size(title, scale, thickness);
    const int tx = _clamp_int((img.width() - tsz.width()) / 2, 0, img.width() - tsz.width());
    img.draw_string(tx, 20, title, image::COLOR_WHITE, scale, -1);
}

static bool is_in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static bool should_exit_by_back_touch(touchscreen::TouchScreen &touch)
{
    if (!touch.is_opened())
        return false;

    int x = 0, y = 0;
    bool pressed = false;
    err::Err e = touch.read(x, y, pressed);
    if (e != err::ERR_NONE || !pressed)
        return false;

    // Must match draw_header's close rect.
    return is_in_rect(x, y, back_x, back_y, back_w, back_h);
}

int _main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    log::info("Program start");

    display::Display *disp = new display::Display(DISP_W, DISP_H);
    camera::Camera *cam = new camera::Camera(DISP_W, DISP_H);

    key::Key key(on_key);

    touchscreen::TouchScreen touch("", false);
    if (touch.open() != err::ERR_NONE)
    {
        log::warn("touchscreen open failed, back button by touch disabled");
    }

    image::Image *img = nullptr;
    while (!app::need_exit())
    {
        if ((img = cam->read()) != nullptr)
        {
            draw_header(*img, "Camera");
            if (should_exit_by_back_touch(touch))
            {
                app::set_exit_flag(true);
            }
            disp->show(*img);
            delete img;
            img = nullptr;
        }
        time::sleep_ms(33);
    }

    if (cam) delete cam;
    if (disp) delete disp;

    log::info("Program exit");
    return 0;
}

int main(int argc, char* argv[])
{
    // Catch signal and process
    sys::register_default_signal_handle();

    // Use CATCH_EXCEPTION_RUN_RETURN to catch exception,
    // if we don't catch exception, when program throw exception, the objects will not be destructed.
    // So we catch exception here to let resources be released(call objects' destructor) before exit.
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}


