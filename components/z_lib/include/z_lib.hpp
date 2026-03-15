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

    /**
     * @brief 在已启动的 VPSS 组上动态添加一个通道（用于 NN 预处理）。
     *        参数直接来自 CVI_TDL_GetVpssChnConfig 的返回值，
     *        调用者在传入前将 pChnAttr->u32Depth 置为 1。
     */
    CVI_S32 z_lib_vpss_add_nn_chn(VPSS_GRP grp, VPSS_CHN chn,
                                   VPSS_SCALE_COEF_E coeff,
                                   const VPSS_CHN_ATTR_S *pChnAttr);

    /**
     * @brief 禁用 VPSS 通道，与 z_lib_vpss_add_nn_chn 对应。
     */
    CVI_S32 z_lib_vpss_remove_nn_chn(VPSS_GRP grp, VPSS_CHN chn);

    /**
     * @brief 通用 VPSS 取帧（指定 grp/chn）。
     * @return CVI_SUCCESS on success
     */
    CVI_S32 z_lib_vpss_take_frame_generic(VPSS_GRP grp, VPSS_CHN chn,
                                           VIDEO_FRAME_INFO_S *pFrame,
                                           CVI_S32 s32MilliSec);

    /**
     * @brief 通用 VPSS 释放帧（指定 grp/chn）。
     */
    void z_lib_vpss_release_frame_generic(VPSS_GRP grp, VPSS_CHN chn,
                                           VIDEO_FRAME_INFO_S *pFrame);

    extern Z_VI_CTX_S g_stViCtx;
}