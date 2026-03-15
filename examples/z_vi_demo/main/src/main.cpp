
#include "maix_basic.hpp"
#include "main.h"
#include "z_mmf.h"
#include <stdio.h>
#include <inttypes.h>

using namespace maix;

Z_VI_CTX_S g_stViCtx;

static void print_frame_info(const VIDEO_FRAME_INFO_S *pstFrameInfo, int frame_idx)
{
    const VIDEO_FRAME_S *f = &pstFrameInfo->stVFrame;
    printf("--- Frame #%d ---\n", frame_idx);
    printf("  Size       : %u x %u\n",  f->u32Width, f->u32Height);
    printf("  PixelFmt   : %d\n",        (int)f->enPixelFormat);
    printf("  Stride     : [%u, %u, %u]\n",
           f->u32Stride[0], f->u32Stride[1], f->u32Stride[2]);
    printf("  PhyAddr    : [0x%08" PRIx64 ", 0x%08" PRIx64 ", 0x%08" PRIx64 "]\n",
           f->u64PhyAddr[0], f->u64PhyAddr[1], f->u64PhyAddr[2]);
    printf("  Length     : [%u, %u, %u]\n",
           f->u32Length[0], f->u32Length[1], f->u32Length[2]);
    printf("  PTS        : %" PRIu64 " us\n", f->u64PTS);
    printf("  TimeRef    : %u\n",         f->u32TimeRef);
    printf("  PoolId     : %u\n",         pstFrameInfo->u32PoolId);
}

int _main(int argc, char* argv[])
{
    uint64_t t = time::time_s();
    log::info("Program start");

    CVI_S32 s32Ret = Z_VI_INIT(&g_stViCtx);
    if (s32Ret != CVI_SUCCESS) {
        printf("[ERROR] X_VI_INIT failed: 0x%x\n", s32Ret);
        return -1;
    }

    int frame_idx = 0;

    // Run until app want to exit, for example app::switch_app API will set exit flag.
    // And you can also call app::set_exit_flag(true) to mark exit.
    while (!app::need_exit())
    {
        VIDEO_FRAME_INFO_S stFrameInfo;

        // 获取帧，超时 2000 ms
        s32Ret = Z_VI_TAKE_FRAME(&g_stViCtx, &stFrameInfo, 2000);
        if (s32Ret != CVI_SUCCESS) {
            printf("[WARN] X_VI_TAKE_FRAME failed (idx=%d): 0x%x, retry...\n", frame_idx, s32Ret);
            time::sleep_ms(100);
            continue;
        }

        // 打印帧信息
        // print_frame_info(&stFrameInfo, frame_idx);

        // 释放帧
        s32Ret = Z_VI_RELEASE_FRAME(&g_stViCtx, &stFrameInfo);
        if (s32Ret != CVI_SUCCESS) {
            printf("[WARN] X_VI_RELEASE_FRAME failed (idx=%d): 0x%x\n", frame_idx, s32Ret);
        }

        ++frame_idx;

        if (time::time_s() - t > 25)
            app::set_exit_flag(true);
    }
    log::info("Program exit, total frames captured: %d", frame_idx);

    Z_VI_DEINIT(&g_stViCtx);

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


