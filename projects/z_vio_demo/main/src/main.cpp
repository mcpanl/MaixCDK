#include "maix_basic.hpp"
#include "main.h"
#include "z_lib.hpp"
#include "z_vision_demo.hpp"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "z_display.hpp"
#include "z_camera.hpp"
#include "z_image.hpp"
#include <chrono>
#include <iostream>
#include "maix_key.hpp"

using namespace maix;
using namespace maix::peripheral;

// 模拟生成一张 RGB888 随机颜色图
static void fill_random_rgb(uint8_t *buf, uint32_t w, uint32_t h) {
    for (uint32_t i = 0; i < w * h * 3; i += 3) {
        buf[i + 0] = rand() % 256; // R
        buf[i + 1] = rand() % 256; // G
        buf[i + 2] = rand() % 256; // B
    }
}

void cb(int a, int b)
{
    printf("cb %d %d\n", a, b);
    if (b == 0)
    {
        app::set_exit_flag(true);
    }
}


int _main(int argc, char* argv[])
{
    log::info("Program start\n");

    key::Key key(cb);

    display::Display *disp = new display::Display(640, 480);
    printf("disp = %p\n", disp);

    image::Image* img = nullptr;
    img = new image::Image(640, 480);
    printf("img = %p\n", img);
    img->draw_string(28, 28, "VIO Demo", image::COLOR_RED, 3, 2);
    disp->show(*img);

    sleep(3);

    if(img) {
        delete img;
        img = nullptr;
    }

    camera::Camera *cam = new camera::Camera(640, 480);



    while(!app::need_exit()) {
        // 记录 read 开始时间
        auto t1 = std::chrono::high_resolution_clock::now();
        image::Image *cam_img = cam->read();
        // 记录 read 结束时间
        auto t2 = std::chrono::high_resolution_clock::now();

        // 计算 read 耗时
        auto read_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        long long show_ms = 0;
        if(cam_img) {
            auto s1 = std::chrono::high_resolution_clock::now();
            disp->show(*cam_img);
            auto s2 = std::chrono::high_resolution_clock::now();
            show_ms = std::chrono::duration_cast<std::chrono::milliseconds>(s2 - s1).count();
        }

        delete cam_img;

//        std::cout << "read耗时: " << read_ms << " ms, show耗时: " << show_ms << " ms" << std::endl;

        time::sleep_ms(5);
    }


//    while(!app::need_exit()) {
//        image::Image *cam_img = cam->read();
//
//        if(cam_img) {
//            disp->show(*cam_img);
//        }
//
//
//        delete cam_img;
//
//        time::sleep_ms(33);
//    }


//    image::Image *cam_img = cam->read();
//
//    printf("cam_img_w = %d, cam_img_h = %d\n", img->width(), img->height());
//
//    disp->show(*cam_img);

//    if(img) {
        delete img;
//    }

//    delete disp;

    log::info("loop end\n");

    z_lib_deinit();

    log::info("Program end\n");
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


