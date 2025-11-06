#pragma once
#include <z_mmf.h>

namespace z {
    void z_lib_init();
    void z_lib_vo_push_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vi_take_frame(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec);
    void z_lib_vi_release_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vpss_take_frame(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec);
    void z_lib_vpss_release_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_deinit();

    extern Z_VI_CTX_S g_stViCtx;
}