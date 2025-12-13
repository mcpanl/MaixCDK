#include "z_lib.hpp"
#include <stdio.h>

namespace z {

    Z_VI_CTX_S g_stViCtx;

    void z_lib_ping()
    {
        printf("[z_lib] z_lib ping\n");
    }

    void z_lib_init()
    {
        printf("[z_lib] z_lib init\n");

        CVI_S32 s32Ret = Z_VI_INIT(&g_stViCtx);
        printf("[z_lib] Z_VI_INIT result: %d\n", s32Ret);
    }

    void z_lib_deinit()
    {
        printf("[z_lib] z_lib deinit\n");

        CVI_S32 s32Ret = Z_VI_DEINIT(&g_stViCtx);
        printf("[z_lib] Z_VI_DEINIT result: %d\n", s32Ret);
    }

    void z_lib_vo_push_frame(VIDEO_FRAME_INFO_S *stFrameInfo)
    {
        Z_VO_PUSH_FRAME(&g_stViCtx, stFrameInfo);
    }

    void z_lib_vi_take_frame(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec)
    {
        if (Z_VI_TAKE_FRAME(&g_stViCtx, stFrameInfo, s32MilliSec) == CVI_SUCCESS) {

        }
    }

    void z_lib_vi_release_frame(VIDEO_FRAME_INFO_S *stFrameInfo)
    {
        Z_VI_RELEASE_FRAME(&g_stViCtx, stFrameInfo);
    }

    void z_lib_vpss_push_frame(VIDEO_FRAME_INFO_S *stFrameInfo) {
        CVI_S32 ret = CVI_VPSS_SendFrame(0, stFrameInfo, -1);
        if (ret != CVI_SUCCESS) {
            printf("[ERR] Vpss SendFrame failed %x\n", ret);
        }
    }

    void z_lib_vpss_push_frame1(VIDEO_FRAME_INFO_S *stFrameInfo) {
        CVI_S32 ret = CVI_VPSS_SendFrame(1, stFrameInfo, -1);
        if (ret != CVI_SUCCESS) {
            printf("[ERR] Vpss SendFrame failed %x\n", ret);
        }
    }

    void z_lib_vpss_take_frame(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec)
    {
        if(Z_VPSS_TAKE_FRAME(&g_stViCtx, stFrameInfo, s32MilliSec) == CVI_SUCCESS)
        {
            printf("z_vpss_take_frame success\n");
        } else {
            printf("z_vpss_take_frame fail\n");
        }
    }

    void z_lib_vpss_take_frame1(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec)
    {
        if(Z_VPSS_TAKE_FRAME1(&g_stViCtx, stFrameInfo, s32MilliSec) == CVI_SUCCESS)
        {
            printf("z_vpss_take_frame success\n");
        } else {
            printf("z_vpss_take_frame fail\n");
        }
    }

    void z_lib_vpss_take_frame1_1(VIDEO_FRAME_INFO_S *stFrameInfo, CVI_S32 s32MilliSec)
    {
        if(Z_VPSS_TAKE_FRAME1_1(&g_stViCtx, stFrameInfo, s32MilliSec) == CVI_SUCCESS)
        {
            printf("z_vpss_take_frame success\n");
        } else {
            printf("z_vpss_take_frame fail\n");
        }
    }

    void z_lib_vpss_release_frame(VIDEO_FRAME_INFO_S *stFrameInfo)
    {
        Z_VPSS_RELEASE_FRAME(&g_stViCtx, stFrameInfo);
    }
    void z_lib_vpss_release_frame1(VIDEO_FRAME_INFO_S *stFrameInfo)
    {
        Z_VPSS_RELEASE_FRAME1(&g_stViCtx, stFrameInfo);
    }
    void z_lib_vpss_release_frame1_1(VIDEO_FRAME_INFO_S *stFrameInfo)
    {
        Z_VPSS_RELEASE_FRAME1_1(&g_stViCtx, stFrameInfo);
    }

} // namespace z
