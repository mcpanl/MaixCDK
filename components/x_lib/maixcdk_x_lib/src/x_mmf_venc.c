#include "x_mmf_priv.h"

#if X_MMF_ENABLE_VENC

CVI_S32 x_mmf_venc_init(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    CVI_U32 i;

    for (i = 0; i < ctx->cfg.venc_chn_count && i < X_MMF_MAX_VENC_CHN; ++i) {
        X_MMF_VENC_CHN_CFG_S *chn_cfg = &ctx->cfg.venc[i];
        if (!chn_cfg->enable) {
            continue;
        }

        ret = CVI_VENC_CreateChn(chn_cfg->chn, &chn_cfg->chn_attr);
        if (ret != CVI_SUCCESS) {
            XLOGE("CVI_VENC_CreateChn chn=%d failed: 0x%x", chn_cfg->chn, ret);
            return ret;
        }

        ret = CVI_VENC_StartRecvFrame(chn_cfg->chn, &chn_cfg->recv_param);
        if (ret != CVI_SUCCESS) {
            XLOGE("CVI_VENC_StartRecvFrame chn=%d failed: 0x%x", chn_cfg->chn, ret);
            return ret;
        }

        ctx->venc_started[i] = CVI_TRUE;
        XLOGI("VENC chn=%d started", chn_cfg->chn);
    }

    return CVI_SUCCESS;
}

CVI_S32 x_mmf_venc_deinit(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    int i;

    for (i = (int)ctx->cfg.venc_chn_count - 1; i >= 0; --i) {
        X_MMF_VENC_CHN_CFG_S *chn_cfg;
        if (i >= X_MMF_MAX_VENC_CHN || !ctx->venc_started[i]) {
            continue;
        }

        chn_cfg = &ctx->cfg.venc[i];
        ret = CVI_VENC_StopRecvFrame(chn_cfg->chn);
        if (ret != CVI_SUCCESS) {
            XLOGW("CVI_VENC_StopRecvFrame chn=%d failed: 0x%x", chn_cfg->chn, ret);
        }

        ret = CVI_VENC_DestroyChn(chn_cfg->chn);
        if (ret != CVI_SUCCESS) {
            XLOGW("CVI_VENC_DestroyChn chn=%d failed: 0x%x", chn_cfg->chn, ret);
        }
    }

    return CVI_SUCCESS;
}

CVI_S32 X_MMF_BindVPSSToVENC(VPSS_GRP grp, VPSS_CHN chn, VENC_CHN venc_chn)
{
    return SAMPLE_COMM_VPSS_Bind_VENC(grp, chn, venc_chn);
}

CVI_S32 X_MMF_UnbindVPSSToVENC(VPSS_GRP grp, VPSS_CHN chn, VENC_CHN venc_chn)
{
    return SAMPLE_COMM_VPSS_UnBind_VENC(grp, chn, venc_chn);
}

#if X_MMF_ENABLE_VI
CVI_S32 X_MMF_BindVIToVENC(X_MMF_CTX_S *ctx, VENC_CHN venc_chn)
{
    if (!ctx) {
        return CVI_FAILURE;
    }
    return SAMPLE_COMM_VI_Bind_VENC(ctx->cfg.vi.vi_pipe, ctx->cfg.vi.vi_chn, venc_chn);
}

CVI_S32 X_MMF_UnbindVIToVENC(X_MMF_CTX_S *ctx, VENC_CHN venc_chn)
{
    if (!ctx) {
        return CVI_FAILURE;
    }
    return SAMPLE_COMM_VI_UnBind_VENC(ctx->cfg.vi.vi_pipe, ctx->cfg.vi.vi_chn, venc_chn);
}
#endif

CVI_S32 X_MMF_VENC_SendFrame(VENC_CHN chn, const VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
    if (!frame) {
        return CVI_FAILURE;
    }
    return CVI_VENC_SendFrame(chn, frame, timeout_ms);
}

CVI_S32 X_MMF_VENC_GetStream(VENC_CHN chn, VENC_STREAM_S *stream, CVI_S32 timeout_ms)
{
    if (!stream) {
        return CVI_FAILURE;
    }
    return CVI_VENC_GetStream(chn, stream, timeout_ms);
}

CVI_S32 X_MMF_VENC_ReleaseStream(VENC_CHN chn, VENC_STREAM_S *stream)
{
    if (!stream) {
        return CVI_FAILURE;
    }
    return CVI_VENC_ReleaseStream(chn, stream);
}

#endif
