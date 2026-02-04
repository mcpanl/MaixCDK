
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
#include <string>
#include "z_nn_test.hpp"
#include "z_nn_object.hpp"
#include "maix_key.hpp"

std::string coco80IdToName(int id)
{
    static const std::string coco80Names[80] = {
        "person",
        "bicycle",
        "car",
        "motorcycle",
        "airplane",
        "bus",
        "train",
        "truck",
        "boat",
        "trafficlight",
        "firehydrant",
        "stopsign",
        "parkingmeter",
        "bench",
        "bird",
        "cat",
        "dog",
        "horse",
        "sheep",
        "cow",
        "elephant",
        "bear",
        "zebra",
        "giraffe",
        "backpack",
        "umbrella",
        "handbag",
        "tie",
        "suitcase",
        "frisbee",
        "skis",
        "snowboard",
        "sportsball",
        "kite",
        "baseballbat",
        "baseballglove",
        "skateboard",
        "surfboard",
        "tennisracket",
        "bottle",
        "wineglass",
        "cup",
        "fork",
        "knife",
        "spoon",
        "bowl",
        "banana",
        "apple",
        "sandwich",
        "orange",
        "broccoli",
        "carrot",
        "hotdog",
        "pizza",
        "donut",
        "cake",
        "chair",
        "couch",
        "pottedplant",
        "bed",
        "diningtable",
        "toilet",
        "tv",
        "laptop",
        "mouse",
        "remote",
        "keyboard",
        "cellphone",
        "microwave",
        "oven",
        "toaster",
        "sink",
        "refrigerator",
        "book",
        "clock",
        "vase",
        "scissors",
        "teddybear",
        "hairdrier",
        "toothbrush"
    };

    if (id < 0 || id >= 80)
        return "";

    return coco80Names[id];
}
enum TimeIndex {
    T_CAM_READ = 0,
    T_VPSS_TAKE,
    T_OD_DETECT,
    T_PRINT_RESULT,
    T_DRAW_RECT,
    T_TDL_FREE,
    T_DISPLAY,
    T_RELEASE,
    T_SLEEP,
    T_LOOP_TOTAL,
    T_MAX
};

static double time_sum[T_MAX] = {0};   // 累计耗时(ms)
static uint32_t sample_cnt = 0;

#define PRINT_INTERVAL 90   // 每 30 帧输出一次

using namespace maix;
using namespace maix::peripheral;


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
    uint64_t t = time::time_s();
    log::info("Program start");

    key::Key key(cb);

    z::nn::Object obj(10, 20, 100, 50);

    printf("Object = %s\n", obj.to_str().c_str());

    z::display::Display *disp = new z::display::Display(552, 368);
    printf("disp = %p\n", disp);

    z::image::Image* img = nullptr;
    img = new z::image::Image(552, 368);
    img->draw_string(28, 28, "YOLO Demo", z::image::COLOR_RED, 3, 2);
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

        z::image::Image *cam_img = cam->read();

        cvtdl_object_t obj_meta;
        memset(&obj_meta, 0, sizeof(obj_meta));
        z::z_lib_vpss_take_frame1_1(&bg2, 2000);
        od_detect(tdl_handle, &bg2, &obj_meta);

        // printf("obj num = %d\n", obj_meta.size);
        // for (uint32_t i = 0; i < obj_meta.size; i++) {
        //     printf("[%f,%f,%f,%f,%d,%f]\n",
        //         obj_meta.info[i].bbox.x1,
        //         obj_meta.info[i].bbox.y1,
        //         obj_meta.info[i].bbox.x2,
        //         obj_meta.info[i].bbox.y2,
        //         obj_meta.info[i].classes,
        //         obj_meta.info[i].bbox.score
        //     );
        // }

        for (uint32_t i = 0; i < obj_meta.size; i++) {
            if (obj_meta.info[i].classes == 0)
            {
                // 暂时忽略类型0
                // continue;
            }

            float x1 = obj_meta.info[i].bbox.x1;
            float y1 = obj_meta.info[i].bbox.y1;
            float x2 = obj_meta.info[i].bbox.x2;
            float y2 = obj_meta.info[i].bbox.y2;

            float w = x2 - x1;
            float h = y2 - y1;
            if (w <= 0 || h <= 0) continue;

            std::string class_name = coco80IdToName(obj_meta.info[i].classes);

            cam_img->draw_rect(
                (int)x1, (int)y1,
                (int)w,  (int)h,
                z::image::COLOR_RED, 3
            );

            cam_img->draw_string((int)x1, (int)y1 - 26, class_name, z::image::COLOR_RED, 2, 2);
        }

        // ⚠️ 一定要释放
        CVI_TDL_Free(&obj_meta);

        if(cam_img) {
            disp->show(*cam_img);
        }

        delete cam_img;
        z::z_lib_vpss_release_frame1_1(&bg2);

        time::sleep_ms(33);
    }
//
// using Clock = std::chrono::steady_clock;
// using Ms = std::chrono::duration<double, std::milli>;
//
// while (!app::need_exit())
// {
//     auto t_loop_start = Clock::now();
//
//     VIDEO_FRAME_INFO_S bg2;
//
//     auto t0 = Clock::now();
//     z::image::Image* cam_img = cam->read();
//     auto t1 = Clock::now();
//     time_sum[T_CAM_READ] += Ms(t1 - t0).count();
//
//     cvtdl_object_t obj_meta;
//     memset(&obj_meta, 0, sizeof(obj_meta));
//
//     auto t2 = Clock::now();
//     z::z_lib_vpss_take_frame1_1(&bg2, 2000);
//     auto t3 = Clock::now();
//     time_sum[T_VPSS_TAKE] += Ms(t3 - t2).count();
//
//     auto t4 = Clock::now();
//     od_detect(tdl_handle, &bg2, &obj_meta);
//     auto t5 = Clock::now();
//     time_sum[T_OD_DETECT] += Ms(t5 - t4).count();
//
//     auto t6 = Clock::now();
//     for (uint32_t i = 0; i < obj_meta.size; i++) {
//         if (obj_meta.info[i].classes == 0)
//         {
//             // 暂时忽略类型0
//             continue;
//         }
//
//         printf("[%f,%f,%f,%f,%d,%f]\n",
//             obj_meta.info[i].bbox.x1,
//             obj_meta.info[i].bbox.y1,
//             obj_meta.info[i].bbox.x2,
//             obj_meta.info[i].bbox.y2,
//             obj_meta.info[i].classes,
//             obj_meta.info[i].bbox.score
//         );
//     }
//     auto t7 = Clock::now();
//     time_sum[T_PRINT_RESULT] += Ms(t7 - t6).count();
//
//     auto t8 = Clock::now();
//     for (uint32_t i = 0; i < obj_meta.size; i++) {
//         if (obj_meta.info[i].classes == 0)
//         {
//             // 暂时忽略类型0
//             continue;
//         }
//
//         float x1 = obj_meta.info[i].bbox.x1;
//         float y1 = obj_meta.info[i].bbox.y1;
//         float x2 = obj_meta.info[i].bbox.x2;
//         float y2 = obj_meta.info[i].bbox.y2;
//
//         float w = x2 - x1;
//         float h = y2 - y1;
//         if (w <= 0 || h <= 0) continue;
//
//         std::string class_name = coco80IdToName(obj_meta.info[i].classes);
//
//         cam_img->draw_rect(
//             (int)x1, (int)y1,
//             (int)w,  (int)h,
//             z::image::COLOR_RED, 3
//         );
//
//         cam_img->draw_string((int)x1, (int)y1 - 26, class_name, z::image::COLOR_RED, 2, 2);
//     }
//     auto t9 = Clock::now();
//     time_sum[T_DRAW_RECT] += Ms(t9 - t8).count();
//
//     auto t10 = Clock::now();
//     CVI_TDL_Free(&obj_meta);
//     auto t11 = Clock::now();
//     time_sum[T_TDL_FREE] += Ms(t11 - t10).count();
//
//     auto t12 = Clock::now();
//     if (cam_img) {
//         disp->show(*cam_img);
//     }
//     auto t13 = Clock::now();
//     time_sum[T_DISPLAY] += Ms(t13 - t12).count();
//
//     auto t14 = Clock::now();
//     delete cam_img;
//     z::z_lib_vpss_release_frame1_1(&bg2);
//     auto t15 = Clock::now();
//     time_sum[T_RELEASE] += Ms(t15 - t14).count();
//
//     auto t16 = Clock::now();
//     time::sleep_ms(1);
//     auto t17 = Clock::now();
//     time_sum[T_SLEEP] += Ms(t17 - t16).count();
//
//     auto t_loop_end = Clock::now();
//     time_sum[T_LOOP_TOTAL] += Ms(t_loop_end - t_loop_start).count();
//
//     sample_cnt++;
//
//     if (sample_cnt >= PRINT_INTERVAL)
//     {
//         printf(
//             "\n[Avg TimeCost over %u frames] (ms)\n"
//             " cam->read        : %.2f\n"
//             " vpss_take_frame  : %.2f\n"
//             " od_detect        : %.2f\n"
//             " print_result     : %.2f\n"
//             " draw_rect        : %.2f\n"
//             " CVI_TDL_Free     : %.2f\n"
//             " display          : %.2f\n"
//             " release_resource : %.2f\n"
//             " sleep            : %.2f\n"
//             " ------------------------------\n"
//             " loop total       : %.2f\n\n",
//             sample_cnt,
//             time_sum[T_CAM_READ] / sample_cnt,
//             time_sum[T_VPSS_TAKE] / sample_cnt,
//             time_sum[T_OD_DETECT] / sample_cnt,
//             time_sum[T_PRINT_RESULT] / sample_cnt,
//             time_sum[T_DRAW_RECT] / sample_cnt,
//             time_sum[T_TDL_FREE] / sample_cnt,
//             time_sum[T_DISPLAY] / sample_cnt,
//             time_sum[T_RELEASE] / sample_cnt,
//             time_sum[T_SLEEP] / sample_cnt,
//             time_sum[T_LOOP_TOTAL] / sample_cnt
//         );
//
//         /* 清零，重新统计 */
//         memset(time_sum, 0, sizeof(time_sum));
//         sample_cnt = 0;
//     }
// }



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


