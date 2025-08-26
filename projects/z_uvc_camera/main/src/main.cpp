#include <bits/stdc++.h>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
using namespace std;

#include "main.h"
#include "maix_util.hpp"
#include "maix_image.hpp"
#include "maix_time.hpp"
#include "maix_display.hpp"
#include "maix_key.hpp"
#include "maix_camera.hpp"
#include "maix_uvc_stream.hpp"
#include "maix_basic.hpp"

using namespace maix;
using namespace maix::uvc;
using namespace maix::peripheral;

static int g_key = 0;
static int g_state = 0;

// 状态机
enum KeyStage {
    IDLE = 0,
    SHORT_PRESSED,
    LONG_PRESSING
};


static std::unique_ptr<camera::Camera> pCam = nullptr;
static std::unique_ptr<camera::Camera> pCam2 = nullptr;
static std::unique_ptr<display::Display> pDisp = nullptr;
static std::atomic<KeyStage> key_stage{IDLE};
static std::atomic<int> countdown_ms{0};
static std::chrono::steady_clock::time_point short_press_time;
static std::chrono::steady_clock::time_point press_start;

// 按键回调
void on_key(int key, int state)
{
    log::info("key: %d, state: %d\n", key, state);

    if (state == 1) { // 按下
        if (key_stage == SHORT_PRESSED) {
            auto now = std::chrono::steady_clock::now();
            int delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - short_press_time).count();
            if (delta <= 1500) {
                // 在1秒内按下，开始长按计时
                press_start = now;
                countdown_ms = 2500;
                key_stage = LONG_PRESSING;
            } else {
                // 超时，回到初始状态
                key_stage = IDLE;
            }
        }
    } else { // 松开
        if (key_stage == IDLE) {
            // 第一次短按 -> 记录时间
            short_press_time = std::chrono::steady_clock::now();
            key_stage = SHORT_PRESSED;
        } else if (key_stage == LONG_PRESSING) {
            // 中途松开则取消
            key_stage = IDLE;
            countdown_ms = 0;
        }
    }

    g_key = key;
    g_state = state;
}

void display_thread_func() {
    while (!app::need_exit()) {
        if (pDisp) {
            int w = pDisp->width();
            int h = pDisp->height();

            // 新建一张纯黑画面
            image::Image *img = new image::Image(w, h);

            if (key_stage == LONG_PRESSING) {
                auto now = std::chrono::steady_clock::now();
                int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - press_start).count();

                // 总时长 1500ms，分成4份，每份 375ms
                int section = 1800 / 6;

                int active_count = std::min(6, (elapsed + section - 1) / section); // 已经点亮几个矩形

                int rect_h = h;   // 画在中间区域
                int rect_w = w / 6;
                int rect_y = (h - rect_h) / 2;

                for (int i = 0; i < 6; i++) {
                    int rect_x = i * rect_w;
                    image::Color c = (i < active_count)
                                     ? image::Color::from_rgb(255, 0, 0)   // 已点亮：红色
                                     : image::Color::from_rgb(50, 50, 50); // 未点亮：
                    img->draw_rect(rect_x, rect_y, rect_w, rect_h, c, -1);
                }

                if (elapsed >= 1800) {
                    printf(">>> 触发成功！\n");
                    app::set_exit_flag(1);
                    key_stage = IDLE;
                }
            }
            else {
                // 非长按状态时，依旧显示摄像头画面
                if (pCam2) {
                    image::Image *img2 = pCam2->read();
                    if (img2) {
                        pDisp->show(*img2);
                        delete img2;
                        delete img; // 别忘了释放
                        std::this_thread::sleep_for(std::chrono::milliseconds(42));
                        continue;
                    }
                }
            }

            // 显示结果
            pDisp->show(*img);
            delete img;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(42));
    }
}


int my_uvc_video_fill_mjpg_buffer(void *buf, uint32_t *size) {
    static uint64_t frame_count = 0;
    static uint64_t last_ms;
    if (!pCam) return 1;

    image::Image *img = pCam->read();
    if (!img) return 1;
    uint64_t curr_ms_takeframe = time::ticks_ms();

    // std::ostringstream oss;
    // oss << "frame: " << frame_count;
    // img->draw_string(4, 32, oss.str(), image::Color::from_rgb(255, 0, 0), 1.3);

    uvc::helper_fill_mjpg_image(buf, size, img);
    uint64_t curr_ms_mjpgframe = time::ticks_ms();

    delete img;

    uint64_t curr_ms = time::ticks_ms();
#if 0
    static uint32_t flip = 1;
    if (flip ^= 1)
        log::info("[%llu]loop use %lld ms, %.2ffps, %d bytes\r\n"
                  "\ttake: %lld ms\r\n"
                  "\tto_mjpg: %lld ms\r\n",
                  frame_count++, curr_ms - last_ms, 1000.f/(curr_ms - last_ms), *size,
                  curr_ms_takeframe - last_ms, curr_ms_mjpgframe - curr_ms_takeframe);
#endif
    last_ms = curr_ms;
    return 0;
}

int _main(int argc, char* argv[])
{
    int cam_w = 640;
    int cam_h = 480;
    int cam_fps = 24;
    int cam_buffer_num = 5;

    log::info("Camera width:%d height:%d fps:%d buffer_num:%d", cam_w, cam_h, cam_fps, cam_buffer_num);
    key::Key key = key::Key(on_key);
    image::Format cam_fmt = image::Format::FMT_RGB888;
    camera::Camera cam = camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);
    if(!pCam) pCam = make_unique<camera::Camera>(std::move(cam));

    camera::Camera* cam2 = pCam->add_channel(cam_w, cam_h, cam_fmt, cam_fps, cam_buffer_num);
    if (!pCam2) pCam2.reset(cam2);

    display::Display disp = display::Display();
    if(!pDisp) pDisp = make_unique<display::Display>(std::move(disp));

    // 启动独立线程显示pCam2图像
    std::thread disp_thread(display_thread_func);

    std::unique_ptr<UvcServer> pUvc = std::make_unique<UvcServer>(my_uvc_video_fill_mjpg_buffer);
    pUvc->run();

    while (!app::need_exit()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    pUvc->stop();

    // 等待显示线程退出
    if (disp_thread.joinable()) disp_thread.join();

    return 0;
}

int main(int argc, char* argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
