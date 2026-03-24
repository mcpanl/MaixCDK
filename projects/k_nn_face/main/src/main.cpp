
#include "maix_basic.hpp"
#include <algorithm>
#include <cstdio>
#include "z_camera.hpp"
#include "z_display.hpp"
#include "z_image.hpp"
#include "z_nn_face_detector.hpp"
#include "z_touchscreen.hpp"
#include "maix_key.hpp"
#include "main.h"

using namespace maix;
using namespace maix::peripheral;

static constexpr int DISP_W = 640;
static constexpr int DISP_H = 480;
static constexpr int HEADER_H = 60;

static constexpr int USER_KEY_CODE = 352;
static const char *MODEL_PATH = "/root/models/face_detector.mud";

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
    img.draw_rect(0, 0, img.width(), HEADER_H, image::COLOR_BLACK, -1);

    // Close button
    img.draw_rect(back_x, back_y, back_w, back_h, image::COLOR_RED, -1);
    img.draw_string(back_x + 10, back_y + 16, "Close", image::COLOR_WHITE, 2.0, -1);

    // Title
    const float scale = 2.0;
    const int thickness = 2;
    image::Size tsz = image::string_size(title, scale, thickness);
    const int tx = _clamp_int((img.width() - tsz.width()) / 2, 0, img.width() - tsz.width());
    img.draw_string(tx, 20, title, image::COLOR_WHITE, scale, -1);
}

static void draw_label_tag(
    image::Image &img,
    int x,
    int y,
    const std::string &text,
    float scale = 2.0f)
{
    // Background (red)
    const int pad_x = 4;
    const int pad_y = 2;
    const int bg_radius_thickness = -1; // kept for readability: use filled rect

    (void)bg_radius_thickness;

    const int thickness = 2;
    image::Size tsz = image::string_size(text, scale, thickness);

    int bg_w = tsz.width() + pad_x * 2;
    int bg_h = tsz.height() + pad_y * 2;
    bg_w = std::max(1, bg_w);
    bg_h = std::max(1, bg_h);

    x = _clamp_int(x, 0, img.width() - bg_w);
    y = _clamp_int(y, 0, img.height() - bg_h);

    img.draw_rect(x, y, bg_w, bg_h, image::COLOR_RED, -1);
    img.draw_string(x + pad_x, y + pad_y, text, image::COLOR_WHITE, scale, -1);
}

static inline bool is_in_rect(int x, int y, int rx, int ry, int rw, int rh)
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

    return is_in_rect(x, y, back_x, back_y, back_w, back_h);
}

static bool clip_box_to_image(int &x, int &y, int &w, int &h, int min_y, int max_w, int max_h)
{
    // Keep rect within [0, width/height] and below header.
    if (w <= 0 || h <= 0)
        return false;

    if (y < min_y)
    {
        int diff = min_y - y;
        y = min_y;
        h -= diff;
    }
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > max_w)
        w = max_w - x;
    if (y + h > max_h)
        h = max_h - y;

    return w > 0 && h > 0;
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

    nn::FaceDetector faceDetector("", false);
    err::Err e = faceDetector.load(MODEL_PATH);
    err::check_raise(e, "load face detector model failed");

    image::Image *img = nullptr;
    while (!app::need_exit())
    {
        if ((img = cam->read()) == nullptr)
        {
            time::sleep_ms(10);
            continue;
        }

        draw_header(*img, "NN Face");

        std::vector<nn::Object> *result = faceDetector.detect(*img);
        if (result)
        {
            for (auto &obj : *result)
            {
                int x = obj.x;
                int y = obj.y;
                int w = obj.w;
                int h = obj.h;

                if (!clip_box_to_image(x, y, w, h, HEADER_H, img->width(), img->height()))
                    continue;

                // Red detection box
                img->draw_rect(x, y, w, h, image::COLOR_RED, 2);

                char buf[96] = {0};
                snprintf(buf, sizeof(buf), "face %.2f", obj.score);
                draw_label_tag(*img, x, y, buf, 2.0f);
            }
        }

        if (should_exit_by_back_touch(touch))
        {
            app::set_exit_flag(true);
        }

        disp->show(*img);
        delete result;
        delete img;
        img = nullptr;
        time::sleep_ms(33);
    }

    if (cam) delete cam;
    if (disp) delete disp;

    log::info("Program exit");
    return 0;
}

int main(int argc, char* argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}


