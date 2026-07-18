#include "x_mmf_priv.h"

#if X_MMF_ENABLE_VDEC

CVI_S32 x_mmf_vdec_init(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    CVI_U32 i;

    for (i = 0; i < ctx->cfg.vdec_chn_count && i < X_MMF_MAX_VDEC_CHN; ++i) {
        X_MMF_VDEC_CHN_CFG_S *chn_cfg = &ctx->cfg.vdec[i];
        if (!chn_cfg->enable) {
            continue;
        }

        ret = CVI_VDEC_CreateChn(chn_cfg->chn, &chn_cfg->chn_attr);
        if (ret != CVI_SUCCESS) {
            XLOGE("CVI_VDEC_CreateChn chn=%d failed: 0x%x", chn_cfg->chn, ret);
            return ret;
        }

        ret = CVI_VDEC_StartRecvStream(chn_cfg->chn);
        if (ret != CVI_SUCCESS) {
            XLOGE("CVI_VDEC_StartRecvStream chn=%d failed: 0x%x", chn_cfg->chn, ret);
            return ret;
        }

        ctx->vdec_started[i] = CVI_TRUE;
        XLOGI("VDEC chn=%d started", chn_cfg->chn);
    }

    return CVI_SUCCESS;
}

CVI_S32 x_mmf_vdec_deinit(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    int i;

    for (i = (int)ctx->cfg.vdec_chn_count - 1; i >= 0; --i) {
        X_MMF_VDEC_CHN_CFG_S *chn_cfg;
        if (i >= X_MMF_MAX_VDEC_CHN || !ctx->vdec_started[i]) {
            continue;
        }
        chn_cfg = &ctx->cfg.vdec[i];

        ret = CVI_VDEC_StopRecvStream(chn_cfg->chn);
        if (ret != CVI_SUCCESS) {
            XLOGW("CVI_VDEC_StopRecvStream chn=%d failed: 0x%x", chn_cfg->chn, ret);
        }

        ret = CVI_VDEC_DestroyChn(chn_cfg->chn);
        if (ret != CVI_SUCCESS) {
            XLOGW("CVI_VDEC_DestroyChn chn=%d failed: 0x%x", chn_cfg->chn, ret);
        }
    }

    return CVI_SUCCESS;
}

CVI_S32 X_MMF_BindVDECToVPSS(VDEC_CHN vdec_chn, VPSS_GRP grp)
{
    return SAMPLE_COMM_VDEC_Bind_VPSS(vdec_chn, grp);
}

CVI_S32 X_MMF_UnbindVDECToVPSS(VDEC_CHN vdec_chn, VPSS_GRP grp)
{
    return SAMPLE_COMM_VDEC_UnBind_VPSS(vdec_chn, grp);
}

CVI_S32 X_MMF_VDEC_SendStream(VDEC_CHN chn, const VDEC_STREAM_S *stream, CVI_S32 timeout_ms)
{
    if (!stream) {
        return CVI_FAILURE;
    }
    return CVI_VDEC_SendStream(chn, stream, timeout_ms);
}

#endif
