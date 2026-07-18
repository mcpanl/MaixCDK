#include "main.h"
#include "maix_basic.hpp"
#include "z_camera.hpp"
#include "z_display.hpp"
#include "z_image.hpp"
#include "z_image_color.hpp"
#include "z_touchscreen.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace maix;

struct TouchSharedState {
    std::atomic<bool> running{true};
    std::atomic<bool> click_pending{false};
    std::atomic<int> last_x{0};
    std::atomic<int> last_y{0};
    std::atomic<bool> last_pressed{false};
    int btn_x{0};
    int btn_y{0};
    int btn_w{0};
    int btn_h{0};
};

struct TouchThreadArgs {
    touchscreen::TouchScreen *touch;
    TouchSharedState *state;
};

struct SysStats {
    unsigned long long mem_total{0};
    unsigned long long mem_used{0};
    float cpu_percent{0.0f};
    float cpu_temp_c{-1.0f};
    uint64_t last_update_ms{0};
    unsigned long long prev_cpu_total{0};
    unsigned long long prev_cpu_idle{0};
    bool has_prev_cpu{false};
};

static bool read_meminfo(unsigned long long &total_bytes, unsigned long long &used_bytes)
{
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open())
        return false;

    std::string key;
    unsigned long long value_kb = 0;
    std::string unit;
    unsigned long long total_kb = 0;
    unsigned long long avail_kb = 0;

    while (meminfo >> key >> value_kb >> unit) {
        if (key == "MemTotal:")
            total_kb = value_kb;
        else if (key == "MemAvailable:")
            avail_kb = value_kb;
    }

    if (total_kb == 0)
        return false;

    total_bytes = total_kb * 1024ULL;
    used_bytes = (total_kb > avail_kb) ? (total_kb - avail_kb) * 1024ULL : 0ULL;
    return true;
}

static bool read_cpu_stat(unsigned long long &total, unsigned long long &idle)
{
    std::ifstream proc_stat("/proc/stat");
    if (!proc_stat.is_open())
        return false;

    std::string line;
    if (!std::getline(proc_stat, line))
        return false;

    std::istringstream iss(line);
    std::string label;
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle_v = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;

    iss >> label >> user >> nice >> system >> idle_v >> iowait >> irq >> softirq >> steal;
    if (label != "cpu")
        return false;

    idle = idle_v + iowait;
    total = user + nice + system + idle_v + iowait + irq + softirq + steal;
    return total > 0;
}

static float sample_cpu_usage_percent(SysStats &stats)
{
    unsigned long long total = 0;
    unsigned long long idle = 0;
    if (!read_cpu_stat(total, idle)) {
        auto usage = sys::cpu_usage();
        auto it = usage.find("cpu");
        return (it == usage.end()) ? stats.cpu_percent : it->second;
    }

    if (!stats.has_prev_cpu) {
        stats.prev_cpu_total = total;
        stats.prev_cpu_idle = idle;
        stats.has_prev_cpu = true;
        return stats.cpu_percent;
    }

    unsigned long long delta_total = total - stats.prev_cpu_total;
    unsigned long long delta_idle = idle - stats.prev_cpu_idle;
    stats.prev_cpu_total = total;
    stats.prev_cpu_idle = idle;
    if (delta_total == 0)
        return stats.cpu_percent;

    float busy = 100.0f * (float)(delta_total - delta_idle) / (float)delta_total;
    if (busy < 0.0f)
        busy = 0.0f;
    if (busy > 100.0f)
        busy = 100.0f;
    return busy;
}

static void update_sys_stats(SysStats &stats, uint64_t now_ms)
{
    if (stats.last_update_ms != 0 && now_ms - stats.last_update_ms < 500)
        return;

    unsigned long long total = 0;
    unsigned long long used = 0;
    if (read_meminfo(total, used)) {
        stats.mem_total = total;
        stats.mem_used = used;
    } else {
        auto mem = sys::memory_info();
        if (mem.count("total"))
            stats.mem_total = (unsigned long long)mem["total"];
        if (mem.count("used"))
            stats.mem_used = (unsigned long long)mem["used"];
    }

    auto temp = sys::cpu_temp();
    auto t = temp.find("cpu");
    stats.cpu_temp_c = (t == temp.end()) ? -1.0f : t->second;
    stats.cpu_percent = sample_cpu_usage_percent(stats);
    stats.last_update_ms = now_ms;
}

static void draw_sys_stats_panel(image::Image &img, const SysStats &stats)
{
    const int panel_w = 240;
    const int panel_h = 88;
    const int margin = 12;
    int x = img.width() - panel_w - margin;
    int y = img.height() - panel_h - margin;

    img.draw_rect(x, y, panel_w, panel_h, image::Color::from_rgb(18, 18, 18), -1);
    img.draw_rect(x, y, panel_w, panel_h, image::COLOR_WHITE, 2);

    std::string mem_line = "Mem: N/A";
    if (stats.mem_total > 0) {
        mem_line = "Mem: " + sys::bytes_to_human(stats.mem_used, 1) + " / " +
                   sys::bytes_to_human(stats.mem_total, 1);
    }

    char cpu_line[64];
    std::snprintf(cpu_line, sizeof(cpu_line), "CPU: %.1f%%", stats.cpu_percent);
    char temp_line[64];
    if (stats.cpu_temp_c >= 0.0f)
        std::snprintf(temp_line, sizeof(temp_line), "Temp: %.1f C", stats.cpu_temp_c);
    else
        std::snprintf(temp_line, sizeof(temp_line), "Temp: N/A");

    img.draw_string(x + 10, y + 10, mem_line, image::COLOR_WHITE, 0.8f, 1);
    img.draw_string(x + 10, y + 34, cpu_line, image::COLOR_WHITE, 0.8f, 1);
    img.draw_string(x + 10, y + 58, temp_line, image::COLOR_WHITE, 0.8f, 1);
}

static bool point_in_button(int x, int y, const TouchSharedState &state)
{
    return x >= state.btn_x && x < state.btn_x + state.btn_w &&
           y >= state.btn_y && y < state.btn_y + state.btn_h;
}

static void touch_thread_entry(void *args)
{
    TouchThreadArgs *ctx = (TouchThreadArgs *)args;
    touchscreen::TouchScreen *touch = ctx->touch;
    TouchSharedState *state = ctx->state;
    bool prev_pressed = false;

    while (state->running.load()) {
        if (!touch->is_opened()) {
            touch->open();
            thread::sleep_ms(100);
            continue;
        }

        if (!touch->available(0)) {
            thread::sleep_ms(2);
            continue;
        }

        int x = 0;
        int y = 0;
        bool pressed = false;
        err::Err e = touch->read0(x, y, pressed);
        if (e != err::ERR_NONE)
            continue;

        state->last_x.store(x);
        state->last_y.store(y);
        state->last_pressed.store(pressed);

        if (pressed && !prev_pressed && point_in_button(x, y, *state)) {
            state->click_pending.store(true);
        }
        prev_pressed = pressed;
    }
}

static void draw_touch_button(image::Image &img, const TouchSharedState &state)
{
    int x = state.btn_x;
    int y = state.btn_y;
    int w = state.btn_w;
    int h = state.btn_h;
    bool touching = state.last_pressed.load() &&
                    point_in_button(state.last_x.load(), state.last_y.load(), state);

    image::Color fill = touching ? image::Color::from_rgb(36, 136, 72) : image::Color::from_rgb(58, 58, 62);
    image::Color border = image::COLOR_WHITE;
    image::Color text = image::COLOR_WHITE;

    img.draw_rect(x, y, w, h, fill, -1);
    img.draw_rect(x, y, w, h, border, 2);
    img.draw_string(x + 16, y + 14, "Click Me", text, 1.0f, 1);
}

static void handle_touch_click(TouchSharedState &state)
{
    if (state.click_pending.exchange(false)) {
        log::info("Touch button clicked!");
    }
}

static void fill_scene_image(image::Image &img, int scene_idx, int w, int h)
{
    switch (scene_idx % 6) {
    case 0:
        img.draw_rect(0, 0, w, h, image::COLOR_RED, -1);
        img.draw_string(28, 28, "Full RED\nRK3566 + MaixCDK\nmaix.display + image::Image", image::COLOR_WHITE,
                        1.2f);
        break;
    case 1:
        img.draw_rect(0, 0, w, h, image::Color::from_rgb(40, 200, 60), -1);
        img.draw_string(28, 28, "Full GREEN\n(close window to exit)", image::COLOR_WHITE, 1.2f);
        break;
    case 2:
        img.draw_rect(0, 0, w, h, image::Color::from_rgb(45, 75, 230), -1);
        img.draw_string(28, 28, "Full BLUE", image::COLOR_WHITE, 1.2f);
        break;
    case 3:
        img.draw_rect(0, 0, w, h, image::COLOR_YELLOW, -1);
        img.draw_string(28, 28, "YELLOW\n(black text)", image::COLOR_BLACK, 1.2f);
        break;
    case 4:
        img.draw_rect(0, 0, w, h, image::Color::from_rgb(32, 32, 36), -1);
        img.draw_string(28, 28, "Dark gray\nwhite text", image::COLOR_WHITE, 1.2f);
        break;
    default: {
        int x0 = 0, x1 = w / 3, x2 = (2 * w) / 3;
        img.draw_rect(x0, 0, x1 - x0, h, image::Color::from_rgb(210, 60, 60), -1);
        img.draw_rect(x1, 0, x2 - x1, h, image::Color::from_rgb(60, 200, 80), -1);
        img.draw_rect(x2, 0, w - x2, h, image::Color::from_rgb(55, 95, 220), -1);
        img.draw_string(28, 28, "Three bands\n(--demo, no camera)", image::COLOR_WHITE, 1.2f);
        break;
    }
    }
}

static int run_display_image_demo()
{
    const int w = 640, h = 480;
    display::Display disp(w, h, image::FMT_RGB888, "", true);
    touchscreen::TouchScreen touch("", true);
    image::Image img(w, h, image::FMT_RGB888);
    TouchSharedState touch_state;
    SysStats sys_stats;
    touch_state.btn_w = 140;
    touch_state.btn_h = 56;
    touch_state.btn_x = w - touch_state.btn_w - 16;
    touch_state.btn_y = 16;
    TouchThreadArgs touch_args{&touch, &touch_state};
    thread::Thread *touch_worker = new thread::Thread(touch_thread_entry, &touch_args);

    log::info("Synthetic Image + Display (--demo). Close X window to exit.");

    while (!app::need_exit()) {
        for (int si = 0; si < 6; si++) {
            if (app::need_exit())
                break;
            fill_scene_image(img, si, w, h);
            draw_touch_button(img, touch_state);
            update_sys_stats(sys_stats, time::ticks_ms());
            draw_sys_stats_panel(img, sys_stats);
            handle_touch_click(touch_state);
            err::Err e = disp.show(img);
            if (e != err::ERR_NONE) {
                log::error("disp.show failed: %d", (int)e);
                app::set_exit_flag(true);
                break;
            }
            for (int k = 0; k < 20; k++) {
                time::sleep_ms(100);
                disp.poll_events();
                if (!disp.is_opened()) {
                    log::info("Window closed");
                    app::set_exit_flag(true);
                    break;
                }
            }
        }
    }

    touch_state.running.store(false);
    touch_worker->join();
    delete touch_worker;
    touch.close();
    return 0;
}

static int run_camera_display_demo(const char *device)
{
    const char *cam_dev = (device && device[0] != '\0') ? device : nullptr;
    camera::Camera cam(640, 480, image::FMT_RGB888, cam_dev, 30, 3, true, false);
    display::Display disp(cam.width(), cam.height(), image::FMT_RGB888, "", true);
    touchscreen::TouchScreen touch("", true);
    TouchSharedState touch_state;
    SysStats sys_stats;
    touch_state.btn_w = 140;
    touch_state.btn_h = 56;
    touch_state.btn_x = disp.width() - touch_state.btn_w - 16;
    touch_state.btn_y = 16;
    TouchThreadArgs touch_args{&touch, &touch_state};
    thread::Thread *touch_worker = new thread::Thread(touch_thread_entry, &touch_args);

    log::info("Camera -> maix.camera::Camera -> maix.display::Display. Close X window to exit.");
    while (!app::need_exit()) {
        image::Image *frame = cam.read(nullptr, 0, true, 500);
        if (!frame)
            continue;

        draw_touch_button(*frame, touch_state);
        update_sys_stats(sys_stats, time::ticks_ms());
        draw_sys_stats_panel(*frame, sys_stats);
        handle_touch_click(touch_state);
        frame->draw_string(28, 28, "Hello, world!", image::COLOR_RED, 3, 2);

        err::Err e = disp.show(*frame);
        delete frame;
        if (e != err::ERR_NONE) {
            log::error("disp.show failed: %d", (int)e);
            break;
        }

        disp.poll_events();
        if (!disp.is_opened()) {
            log::info("Window closed");
            break;
        }
    }

    touch_state.running.store(false);
    touch_worker->join();
    delete touch_worker;
    touch.close();
    cam.close();
    return 0;
}

static void print_usage(const char *prog)
{
    log::info("Usage: %s [--demo | DEVICE]", prog ? prog : "rk3566_rk_lib_demo");
    log::info("  (no args)     use maix.camera + maix.display default camera");
    log::info("  DEVICE        e.g. /dev/video0");
    log::info("  --demo        Synthetic scenes, no camera");
}

int _main(int argc, char *argv[])
{
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : "rk3566_rk_lib_demo";

    if (argc >= 2) {
        if (std::strcmp(argv[1], "--demo") == 0)
            return run_display_image_demo();
        if (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0) {
            print_usage(prog);
            return 0;
        }
    }

    const char *dev = nullptr;
    if (argc >= 2 && argv[1][0] != '-')
        dev = argv[1];

    return run_camera_display_demo(dev);
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
