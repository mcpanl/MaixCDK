
#include "maix_basic.hpp"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include "z_camera.hpp"
#include "z_display.hpp"
#include "z_image.hpp"
#include "z_nn_classifier.hpp"
#include "z_touchscreen.hpp"
#include "maix_key.hpp"
#include "main.h"

using namespace maix;
using namespace maix::peripheral;

static constexpr int DISP_W = 640;
static constexpr int DISP_H = 480;
static constexpr int HEADER_H = 60;

static constexpr int USER_KEY_CODE = 352;
static const char *MODEL_PATH = "/root/models/mobilenetv2.mud";

static constexpr int BOTTOM_H = 60;


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

    // Back button
    img.draw_rect(back_x, back_y, back_w, back_h, image::COLOR_RED, -1);
    img.draw_string(back_x + 10, back_y + 16, "Close", image::COLOR_WHITE, 2.0, -1);

    // Title
    const float scale = 2.0;
    const int thickness = 2;
    image::Size tsz = image::string_size(title, scale, thickness);
    const int tx = _clamp_int((img.width() - tsz.width()) / 2, 0, img.width() - tsz.width());
    img.draw_string(tx, 20, title, image::COLOR_WHITE, scale, -1);
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

static void draw_bottom_results(
    image::Image &img,
    int class_id,
    float score,
    const std::vector<std::string> &labels)
{
    const int bottom_y = img.height() - BOTTOM_H;

    // Bottom panel background
    img.draw_rect(0, bottom_y, img.width(), BOTTOM_H, image::COLOR_BLACK, -1);

    const int margin_x = 0;
    const int bg_w = img.width() - margin_x * 2;

    // Single line, centered
    img.draw_rect(margin_x, bottom_y, bg_w, BOTTOM_H, image::COLOR_RED, -1);

    const float scale = 2.0f;
    const int thickness = 2;
    const std::string label =
        (class_id >= 0 && (size_t)class_id < labels.size())
            ? labels[class_id]
            : std::string("...");

    char buf[96] = {0};
    snprintf(buf, sizeof(buf), "%s", label.c_str());

    image::Size tsz = image::string_size(buf, scale, thickness);
    int tx = margin_x + (bg_w - tsz.width()) / 2;
    tx = _clamp_int(tx, margin_x, margin_x + std::max(0, bg_w - tsz.width()));
    int ty = bottom_y + (BOTTOM_H - tsz.height()) / 2;
    ty = _clamp_int(ty, bottom_y, bottom_y + BOTTOM_H - tsz.height());

    img.draw_string(tx, ty, buf, image::COLOR_WHITE, scale, -1);
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

    nn::Classifier classifier("", false);
    err::Err e = classifier.load(MODEL_PATH);
    err::check_raise(e, "load classifier model failed");
    log::info("load classifier model %s success", MODEL_PATH);

    image::Image *img = nullptr;
    // Multi-frame smoothing (majority vote on top-1 class)
    static constexpr int SMOOTH_WINDOW = 7;
    std::deque<std::pair<int, float>> recent_top1; // {class_id, score}
    int smoothed_class_id = -1;
    float smoothed_score = 0.0f;
    while (!app::need_exit())
    {
        if ((img = cam->read()) == nullptr)
        {
            time::sleep_ms(10);
            continue;
        }

        draw_header(*img, "NN Classifier");

        std::vector<std::pair<int, float>> *result = classifier.classify(*img);
        if (result)
        {
            // dual_buff mode not-ready: returns a dummy vector of size 1
            if (result->size() > 1)
            {
                const int top_id = result->at(0).first;
                const float top_score = result->at(0).second;

                recent_top1.emplace_back(top_id, top_score);
                while ((int)recent_top1.size() > SMOOTH_WINDOW)
                    recent_top1.pop_front();

                std::map<int, std::pair<int, float>> stat; // id -> {count, sum_score}
                for (const auto &p : recent_top1)
                {
                    auto &it = stat[p.first];
                    it.first += 1;
                    it.second += p.second;
                }

                int best_id = smoothed_class_id;
                int best_cnt = -1;
                float best_avg = -1.0f;
                for (const auto &kv : stat)
                {
                    const int id = kv.first;
                    const int cnt = kv.second.first;
                    const float avg = kv.second.second / (float)cnt;
                    if (cnt > best_cnt || (cnt == best_cnt && avg > best_avg))
                    {
                        best_cnt = cnt;
                        best_avg = avg;
                        best_id = id;
                    }
                }
                smoothed_class_id = best_id;
                smoothed_score = best_avg;
            }

            // Always draw a single smoothed result in bottom 60px
            draw_bottom_results(*img, smoothed_class_id, smoothed_score, classifier.labels);
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


