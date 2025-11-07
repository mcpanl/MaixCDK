#include "maix_basic.hpp"
#include "main.h"
#include "z_lib.hpp"
#include "z_vision_demo.hpp"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "z_display.hpp"
#include "z_image.hpp"

using namespace maix;

// 模拟生成一张 RGB888 随机颜色图
static void fill_random_rgb(uint8_t *buf, uint32_t w, uint32_t h) {
    for (uint32_t i = 0; i < w * h * 3; i += 3) {
        buf[i + 0] = rand() % 256; // R
        buf[i + 1] = rand() % 256; // G
        buf[i + 2] = rand() % 256; // B
    }
}

int _main(int argc, char* argv[])
{
    log::info("Program start\n");

    z::display::Display *disp = new z::display::Display(552, 368);
    printf("disp = %p\n", disp);

    z::image::Image* img = new z::image::Image(552, 368);
    printf("img = %p\n", img);
    img->draw_string(28, 28, "Hello, world!", z::image::COLOR_RED, 3, 2);

    disp->show(*img);

    delete img;
    sleep(3);

    delete disp;

    log::info("Program end\n");
}

int _main2(int argc, char* argv[])
{

    uint64_t t = time::time_s();
    log::info("Program start 3");

    auto *disp = new z::display::Display(552, 368);

    printf("disp = %p\n", disp);


    z::z_lib_init();
    // 参数
    int width = 552;
    int height = 368;
    int bufSize = width * height * 3;

    z::image::Image* img = new z::image::Image(552, 368);
    img->draw_string(28, 28, "Hello, world!", z::image::COLOR_RED, 3, 2);
    Bytes *img_bytes = img->to_bytes();

    printf("Image bytes: %d\n", img_bytes->data_len);

    VIDEO_FRAME_INFO_S outFrame1;
    Z_SIMPLE_VPSS_ConvertRGB888(
            img_bytes->data,
            width,
            height,
            &outFrame1
    );
    z::z_lib_vo_push_frame(&outFrame1);

    // 释放 VPSS 输出 frame（不释放输入 buffers）
    Z_SIMPLE_VPSS_FreeConvertedFrame(&outFrame1);

    sleep(3);

    const int FRAME_NUM = 3;
    const int LOOP_COUNT = 10;

// 预分配 3 帧 buffer
    uint8_t *frames[FRAME_NUM];
    for (int i = 0; i < FRAME_NUM; i++) {
        frames[i] = (uint8_t*)malloc(bufSize);
        if (!frames[i]) {
            return -1;
        }
    }

// ---- 填充 3 张不同的 RGB 测试图片 ----
// Frame 0: 红色
    for (int i = 0; i < width * height; i++) {
        frames[0][i*3+0] = 255;  // R
        frames[0][i*3+1] = 0;    // G
        frames[0][i*3+2] = 0;    // B
    }
// Frame 1: 绿色
    for (int i = 0; i < width * height; i++) {
        frames[1][i*3+0] = 0;
        frames[1][i*3+1] = 255;
        frames[1][i*3+2] = 0;
    }
// Frame 2: 蓝色
    for (int i = 0; i < width * height; i++) {
        frames[2][i*3+0] = 0;
        frames[2][i*3+1] = 0;
        frames[2][i*3+2] = 255;
    }

// ---- 主循环 ----
    for (int loop = 0; loop < LOOP_COUNT; loop++) {

        uint8_t *pRGB = frames[loop % FRAME_NUM];

        VIDEO_FRAME_INFO_S outFrame;
        CVI_S32 ret = Z_SIMPLE_VPSS_ConvertRGB888(
                pRGB,
                width,
                height,
                &outFrame
        );

        if (ret != CVI_SUCCESS) {
            // 失败就退出，这里可记录，但性能测试不打印
            break;
        }

        // 输出到 VO
        z::z_lib_vo_push_frame(&outFrame);

        // 释放 VPSS 输出 frame（不释放输入 buffers）
        Z_SIMPLE_VPSS_FreeConvertedFrame(&outFrame);
        maix::time::sleep_ms(50);
    }

// ---- 结束释放 ----
    for (int i = 0; i < FRAME_NUM; i++) {
        free(frames[i]);
    }

#if 0
    int width = 552, height = 368;
    int bufSize = width * height * 3;

    for (int loop = 0; loop < 200; loop++) {

//        printf("\n========== LOOP %d ==========\n", loop + 1);

        uint8_t *pRGB = (uint8_t*)malloc(bufSize);
        if (!pRGB) return -1;

        fill_random_rgb(pRGB, width, height);

        // ---- 打印 RGB 输入 ----
//        printf("=== INPUT RGB888 First 5 pixels ===\n");
//        for (int i = 0; i < 5; i++) {
//            int idx = i * 3;
//            printf("RGB[%d] = R=%3u G=%3u B=%3u\n", i, pRGB[idx], pRGB[idx+1], pRGB[idx+2]);
//        }

//        printf("=== INPUT RGB888 Last 5 pixels ===\n");
//        for (int i = width*height - 5; i < width*height; i++) {
//            int idx = i * 3;
//            printf("RGB[%d] = R=%3u G=%3u B=%3u\n", i, pRGB[idx], pRGB[idx+1], pRGB[idx+2]);
//        }

        // ---- 调用转换 ----
        VIDEO_FRAME_INFO_S outFrame;
        CVI_S32 ret = Z_SIMPLE_VPSS_ConvertRGB888(pRGB, width, height, &outFrame);
        if (ret != CVI_SUCCESS) {
            printf("[TEST ERR] Z_SIMPLE_VPSS_ConvertRGB888 failed 0x%x\n", ret);
            free(pRGB);
            return -1;
        }

//        printf("[TEST] VPSS convert success ✅\n");

        VIDEO_FRAME_S *vo = &outFrame.stVFrame;

//        printf("[TEST] Output format=%d width=%d height=%d stride=%d\n",
//               vo->enPixelFormat, vo->u32Width, vo->u32Height, vo->u32Stride[0]);

        // ---- 推到 VO ----
//        printf("[TEST] Push frame to VO\n");
        z::z_lib_vo_push_frame(&outFrame);

        // 可以开启这个观察泄露：
        // sleep(1);

        // ---- 释放 VPSS 输出 + ION输入内存 ----
//        printf("[TEST] Release VPSS frame\n");
        Z_SIMPLE_VPSS_FreeConvertedFrame(&outFrame);

//        printf("[TEST] Free input RGB buffer\n");
        free(pRGB);

//        printf("[TEST] ✅ Loop %d done.\n", loop + 1);
    }

    printf("All 200 loops done ✅\n");
#endif
    sleep(2);

//    z::z_vision_demo_init();

//    display::Display *disp = new display::Display(552, 368, image::FMT_BGRA8888);

//    log::info("disp: %p", disp);

    // Run until app want to exit, for example app::switch_app API will set exit flag.
    // And you can also call app::set_exit_flag(true) to mark exit.
    VIDEO_FRAME_INFO_S stFrameInfo;

    while(!app::need_exit())
    {
        log::info("%d", time::time_s());

        z::z_lib_vpss_take_frame(&stFrameInfo, 1000);
//        z::z_lib_vi_take_frame(&stFrameInfo, 1000);

        printf("Got frame: %ux%u\n", stFrameInfo.stVFrame.u32Width, stFrameInfo.stVFrame.u32Height);

        z::z_lib_vo_push_frame(&stFrameInfo);

//        z::z_lib_vi_release_frame(&stFrameInfo);
        z::z_lib_vpss_release_frame(&stFrameInfo);

        time::sleep(1);

        if(time::time_s() - t > 10)
            app::set_exit_flag(true);
    }

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


