
#include "maix_basic.hpp"
#include "main.h"
#include "z_lib.hpp"
#include "z_display.hpp"
#include "z_camera.hpp"
#include "z_image.hpp"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <chrono>
#include <iostream>

#include "z_nn_test.hpp"


using namespace maix;

int _main(int argc, char* argv[])
{
    uint64_t t = time::time_s();
    log::info("Program start");

    z::display::Display *disp = new z::display::Display(552, 368);
    printf("disp = %p\n", disp);

    z::image::Image* img = nullptr;
    img = new z::image::Image(552, 368);
    img->draw_string(28, 28, "Hello, NN!", z::image::COLOR_RED, 3, 2);
    disp->show(*img);

    sleep(1);

    if(img) {
        delete img;
        img = nullptr;
    }

    z::camera::Camera *cam = new z::camera::Camera(552, 368);

    cvitdl_handle_t tdl_handle = NULL;

    if (od_init(&tdl_handle) != CVI_SUCCESS) {
        printf("OD init failed\n");
    }

    while(!app::need_exit())
    {
        VIDEO_FRAME_INFO_S bg2;

        log::info("%d", time::time_s());
        z::image::Image *cam_img = cam->read();
        printf("cam_img = %p, w=%d, h=%d\n", cam_img, cam_img->width(), cam_img->height());

        cvtdl_object_t obj_meta;
        memset(&obj_meta, 0, sizeof(obj_meta));
        z::z_lib_vpss_take_frame1_1(&bg2, 2000);
        od_detect(tdl_handle, &bg2, &obj_meta);

        printf("obj num = %d\n", obj_meta.size);
        for (uint32_t i = 0; i < obj_meta.size; i++) {
            printf("[%f,%f,%f,%f,%d,%f]\n",
                obj_meta.info[i].bbox.x1,
                obj_meta.info[i].bbox.y1,
                obj_meta.info[i].bbox.x2,
                obj_meta.info[i].bbox.y2,
                obj_meta.info[i].classes,
                obj_meta.info[i].bbox.score
            );
        }



        for (uint32_t i = 0; i < obj_meta.size; i++) {

            float x1 = obj_meta.info[i].bbox.x1;
            float y1 = obj_meta.info[i].bbox.y1;
            float x2 = obj_meta.info[i].bbox.x2;
            float y2 = obj_meta.info[i].bbox.y2;

            float w = x2 - x1;
            float h = y2 - y1;

            // 防止异常框
            if (w <= 0 || h <= 0) continue;

            cam_img->draw_rect(
                (int)x1,
                (int)y1,
                (int)w,
                (int)h,
                z::image::COLOR_RED,
                3
            );
        }

        // ⚠️ 一定要释放
        CVI_TDL_Free(&obj_meta);

        // cam_img->draw_rect(2, 3, 100, 50, z::image::COLOR_RED, 3);

        if(cam_img) {
            disp->show(*cam_img);
        }

        delete cam_img;
        z::z_lib_vpss_release_frame1_1(&bg2);

        time::sleep_ms(50);
    }

    od_deinit(tdl_handle);
    tdl_handle = NULL;


    z::z_lib_deinit();

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


