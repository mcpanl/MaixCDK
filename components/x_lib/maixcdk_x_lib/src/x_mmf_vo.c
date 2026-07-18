#include "x_mmf_priv.h"

#if X_MMF_ENABLE_VO

CVI_S32 x_mmf_vo_init(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;

    ret = SAMPLE_COMM_VO_StartVO(&ctx->cfg.vo.vo_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VO_StartVO failed: 0x%x", ret);
        return ret;
    }

    if (ctx->cfg.vo.set_rotation) {
        ret = CVI_VO_SetChnRotation(ctx->cfg.vo.vo_layer, ctx->cfg.vo.vo_chn, ctx->cfg.vo.rotation);
        if (ret != CVI_SUCCESS) {
            XLOGW("CVI_VO_SetChnRotation failed: 0x%x", ret);
        }
    }

    ctx->vo_started = CVI_TRUE;
    XLOGI("VO started");
    return CVI_SUCCESS;
}

CVI_S32 x_mmf_vo_deinit(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;

    if (!ctx->cfg.vo.enable || !ctx->vo_started) {
        return CVI_SUCCESS;
    }

    ret = SAMPLE_COMM_VO_StopVO(&ctx->cfg.vo.vo_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGW("SAMPLE_COMM_VO_StopVO failed: 0x%x", ret);
    }

    ctx->vo_started = CVI_FALSE;
    return CVI_SUCCESS;
}

CVI_S32 X_MMF_VO_SendFrame(X_MMF_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
    CVI_S32 ret;

    if (!ctx || !frame) {
        return CVI_FAILURE;
    }

    XLOGW(">>>>> CVI_VO_SendFrame <<<<<");

    ret = CVI_VO_SendFrame(ctx->cfg.vo.vo_layer, ctx->cfg.vo.vo_chn, frame, timeout_ms);
    if (ret != CVI_SUCCESS) {
        XLOGW("CVI_VO_SendFrame failed layer=%d chn=%d ret=0x%x",
              ctx->cfg.vo.vo_layer, ctx->cfg.vo.vo_chn, ret);
    }
    return ret;
}

CVI_S32 X_MMF_BindVPSSToVO(VPSS_GRP grp, VPSS_CHN chn, VO_LAYER layer, VO_CHN vo_chn)
{
    return SAMPLE_COMM_VPSS_Bind_VO(grp, chn, layer, vo_chn);
}

CVI_S32 X_MMF_UnbindVPSSToVO(VPSS_GRP grp, VPSS_CHN chn, VO_LAYER layer, VO_CHN vo_chn)
{
    return SAMPLE_COMM_VPSS_UnBind_VO(grp, chn, layer, vo_chn);
}

#endif
