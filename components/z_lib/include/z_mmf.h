#ifndef __Z_MMF_H__
#define __Z_MMF_H__


#include "sample_comm.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

/* VIO 管线统一输出分辨率 */
#define Z_WIDTH  640
#define Z_HEIGHT 480

typedef struct _Z_VI_CTX_S {
    VI_DEV ViDev;
    VI_PIPE ViPipe;
    VI_CHN ViChn;
    SAMPLE_VI_CONFIG_S stViConfig;
    VB_CONFIG_S stVbConf;
    SIZE_S stSize;
} Z_VI_CTX_S;

/* 函数声明 */
CVI_S32 Z_VI_INIT(Z_VI_CTX_S *pstViCtx);
CVI_S32 Z_VI_TAKE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec);
CVI_S32 Z_VI_RELEASE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo);
CVI_S32 Z_VO_PUSH_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo);
CVI_S32 Z_VO_PUSH_FRAME_WITH_RGB888(Z_VI_CTX_S *pstViCtx, const CVI_U8 *pRGB, CVI_U32 inW, CVI_U32 inH, VIDEO_FRAME_INFO_S *pstOutFrame);
CVI_S32 Z_VI_TAKE_FRAME_AS_RGB888(
        uint8_t** ppRGB,
        uint32_t* pWidth,
        uint32_t* pHeight,
        uint32_t* pStride,
        uint64_t* pPhyAddr,
        void** pVirAddr,
        uint32_t* pFrameSize
);

void Z_VPSS_FreeRGB888(uint64_t phyAddr, void* virAddr);

CVI_S32 Z_VPSS_TAKE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec);
CVI_S32 Z_VPSS_TAKE_FRAME1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec);
CVI_S32 Z_VPSS_TAKE_FRAME1_1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec);
CVI_S32 Z_VPSS_RELEASE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo);
CVI_S32 Z_VPSS_RELEASE_FRAME1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo);
CVI_S32 Z_VPSS_RELEASE_FRAME1_1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo);
CVI_S32 Z_SIMPLE_VPSS_ConvertRGB888(
        const CVI_U8 *pRGB,
        CVI_U32 inW, CVI_U32 inH,
        VIDEO_FRAME_INFO_S *pstOutFrame
);
//CVI_S32 Z_SIMPLE_VPSS_ReleaseFrame(VIDEO_FRAME_INFO_S *pstOutFrame);
CVI_S32 Z_SIMPLE_VPSS_FreeConvertedFrame(VIDEO_FRAME_INFO_S *pFrame);
CVI_S32 Z_VI_DEINIT(Z_VI_CTX_S *pstViCtx);

/**
 * @brief 在已启动的 VPSS 组上动态添加一个通道。
 *        用于在 VIO 管线运行期间为 NN 推理配置专用预处理通道。
 *
 * @param grp      目标 VPSS 组号
 * @param chn      目标 VPSS 通道号
 * @param coeff    缩放系数级别（来自 CVI_TDL_GetVpssChnConfig）
 * @param pChnAttr 通道属性（来自 CVI_TDL_GetVpssChnConfig，调用者可修改 u32Depth）
 * @return CVI_SUCCESS on success
 */
CVI_S32 Z_VPSS_AddChn(VPSS_GRP grp, VPSS_CHN chn, VPSS_SCALE_COEF_E coeff,
                      const VPSS_CHN_ATTR_S *pChnAttr);

/**
 * @brief 禁用并移除一个 VPSS 通道（与 Z_VPSS_AddChn 对应）。
 */
CVI_S32 Z_VPSS_RemoveChn(VPSS_GRP grp, VPSS_CHN chn);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* End of #ifndef __Z_MMF_H__ */