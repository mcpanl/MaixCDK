#include "z_mmf_priv.h"

CVI_S32 Z_VPSS_TAKE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32GetRet = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 0;
    VPSS_CHN VpssChn = VPSS_CHN0;

    (void)pstViCtx;

    s32GetRet = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, pstFrameInfo, s32MilliSec);
    if (s32GetRet != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_GetChnFrame failed: 0x%x\n", s32GetRet);
        return s32GetRet;
    }

    return s32GetRet;
}

CVI_S32 Z_VPSS_RELEASE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 0;
    VPSS_CHN VpssChn = VPSS_CHN0;

    (void)pstViCtx;

    s32Ret = CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_ReleaseChnFrame failed: 0x%x\n", s32Ret);
    }

    return s32Ret;
}

CVI_S32 Z_VPSS_TAKE_FRAME1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32GetRet = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 1;
    VPSS_CHN VpssChn = VPSS_CHN0;

    (void)pstViCtx;

    s32GetRet = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, pstFrameInfo, s32MilliSec);
    if (s32GetRet != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_GetChnFrame failed: 0x%x\n", s32GetRet);
        return s32GetRet;
    }

    return s32GetRet;
}

CVI_S32 Z_VPSS_RELEASE_FRAME1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 1;
    VPSS_CHN VpssChn = VPSS_CHN0;

    (void)pstViCtx;

    s32Ret = CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_ReleaseChnFrame failed: 0x%x\n", s32Ret);
    }

    return s32Ret;
}

CVI_S32 Z_VPSS_TAKE_FRAME1_1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32GetRet = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 1;
    VPSS_CHN VpssChn = VPSS_CHN1;

    (void)pstViCtx;

    s32GetRet = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, pstFrameInfo, s32MilliSec);
    if (s32GetRet != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_GetChnFrame failed: 0x%x\n", s32GetRet);
        return s32GetRet;
    }

    return s32GetRet;
}

CVI_S32 Z_VPSS_RELEASE_FRAME1_1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 1;
    VPSS_CHN VpssChn = VPSS_CHN1;

    (void)pstViCtx;

    s32Ret = CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_ReleaseChnFrame failed: 0x%x\n", s32Ret);
    }

    return s32Ret;
}

CVI_S32 Z_VPSS_AddChn(VPSS_GRP grp, VPSS_CHN chn, VPSS_SCALE_COEF_E coeff,
                      const VPSS_CHN_ATTR_S *pChnAttr)
{
    if (!pChnAttr) {
        SAMPLE_PRT("[Z_VPSS_AddChn] pChnAttr is NULL\n");
        return CVI_FAILURE;
    }

    CVI_S32 ret = CVI_VPSS_SetChnAttr(grp, chn, pChnAttr);
    if (ret != CVI_SUCCESS) {
        SAMPLE_PRT("[Z_VPSS_AddChn] SetChnAttr grp=%d chn=%d failed: 0x%x\n", grp, chn, ret);
        return ret;
    }

    ret = CVI_VPSS_SetChnScaleCoefLevel(grp, chn, coeff);
    if (ret != CVI_SUCCESS) {
        SAMPLE_PRT("[Z_VPSS_AddChn] SetChnScaleCoefLevel grp=%d chn=%d failed: 0x%x (ignored)\n",
                   grp, chn, ret);
    }

    ret = CVI_VPSS_EnableChn(grp, chn);
    if (ret != CVI_SUCCESS) {
        SAMPLE_PRT("[Z_VPSS_AddChn] EnableChn grp=%d chn=%d failed: 0x%x\n", grp, chn, ret);
        return ret;
    }

    SAMPLE_PRT("[Z_VPSS_AddChn] grp=%d chn=%d ok. fmt=%d size=%ux%u normalize=%d\n",
               grp, chn,
               pChnAttr->enPixelFormat,
               pChnAttr->u32Width, pChnAttr->u32Height,
               pChnAttr->stNormalize.bEnable);
    return CVI_SUCCESS;
}

CVI_S32 Z_VPSS_RemoveChn(VPSS_GRP grp, VPSS_CHN chn)
{
    CVI_S32 ret = CVI_VPSS_DisableChn(grp, chn);
    if (ret != CVI_SUCCESS) {
        SAMPLE_PRT("[Z_VPSS_RemoveChn] DisableChn grp=%d chn=%d failed: 0x%x\n", grp, chn, ret);
    } else {
        SAMPLE_PRT("[Z_VPSS_RemoveChn] grp=%d chn=%d disabled\n", grp, chn);
    }
    return ret;
}