#pragma once
#include <z_mmf.h>

namespace maix {
    void z_lib_ping();

    void z_lib_init();
    void z_lib_vo_push_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vi_take_frame(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec);
    void z_lib_vi_release_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vpss_take_frame(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec);
    void z_lib_vpss_take_frame1(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec);
    void z_lib_vpss_take_frame1_1(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec);
    void z_lib_vpss_release_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vpss_release_frame1(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vpss_release_frame1_1(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_deinit();
    void z_lib_vpss_push_frame(VIDEO_FRAME_INFO_S *stFrameInfo);
    void z_lib_vpss_push_frame1(VIDEO_FRAME_INFO_S *stFrameInfo);

    extern Z_VI_CTX_S g_stViCtx;
}