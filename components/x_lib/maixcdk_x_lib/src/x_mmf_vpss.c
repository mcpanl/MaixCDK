#include "x_mmf_priv.h"

#if X_MMF_ENABLE_VPSS

void x_mmf_vpss_precleanup_hard(void)
{
    VPSS_CHN ch;
    int g;

    /*
     * 进程被 SIGINT 等打断时可能未走 X_MMF_Deinit，内核仍占用 VPSS。
     * 顺序：先解 VPSS0→VO，再解 VI→VPSS1，再销毁 grp1、grp0（与 z_mmf_vi.c 预清理一致）。
     */
#if X_MMF_ENABLE_VO
    (void)SAMPLE_COMM_VPSS_UnBind_VO(0, VPSS_CHN0, 0, 0);
#endif
#if X_MMF_ENABLE_VI
    (void)SAMPLE_COMM_VI_UnBind_VPSS(0, 0, 1);
#endif

    for (g = 1; g >= 0; --g) {
        for (ch = 0; ch < VPSS_MAX_PHY_CHN_NUM; ch++) {
            (void)CVI_VPSS_DisableChn((VPSS_GRP)g, ch);
        }
        (void)CVI_VPSS_StopGrp((VPSS_GRP)g);
        (void)CVI_VPSS_DestroyGrp((VPSS_GRP)g);
    }
    XLOGI("VPSS precleanup (grp1+0) done");
}

CVI_S32 x_mmf_vpss_init(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    CVI_U32 i;

    for (i = 0; i < ctx->cfg.vpss_grp_count && i < X_MMF_MAX_VPSS_GRP; ++i) {
        X_MMF_VPSS_GRP_CFG_S *grp_cfg = &ctx->cfg.vpss[i];
        if (!grp_cfg->grp_enable) {
            continue;
        }

        XLOGE(">> set VPSS mode to DUAL");

        CVI_SYS_SetVPSSMode(VPSS_MODE_DUAL);

        VI_VPSS_MODE_S stViVpssMode;
        stViVpssMode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
        stViVpssMode.aenMode[1] = VI_ONLINE_VPSS_OFFLINE;
    
        CVI_SYS_SetVIVPSSMode(&stViVpssMode);
    


        ret = SAMPLE_COMM_VPSS_Init(grp_cfg->grp, grp_cfg->chn_enable, &grp_cfg->grp_attr, grp_cfg->chn_attr);
        if (ret != CVI_SUCCESS) {
            XLOGE("SAMPLE_COMM_VPSS_Init grp=%d failed: 0x%x", grp_cfg->grp, ret);
            return ret;
        }

        ret = SAMPLE_COMM_VPSS_Start(grp_cfg->grp, grp_cfg->chn_enable, &grp_cfg->grp_attr, grp_cfg->chn_attr);
        if (ret != CVI_SUCCESS) {
            XLOGE("SAMPLE_COMM_VPSS_Start grp=%d failed: 0x%x", grp_cfg->grp, ret);
            return ret;
        }

        ctx->vpss_started[i] = CVI_TRUE;
        XLOGI("VPSS grp=%d started", grp_cfg->grp);
    }

    return CVI_SUCCESS;
}

CVI_S32 x_mmf_vpss_deinit(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    int i;

    for (i = (int)ctx->cfg.vpss_grp_count - 1; i >= 0; --i) {
        X_MMF_VPSS_GRP_CFG_S *grp_cfg;
        if (i >= X_MMF_MAX_VPSS_GRP || !ctx->vpss_started[i]) {
            continue;
        }
        grp_cfg = &ctx->cfg.vpss[i];
        ret = SAMPLE_COMM_VPSS_Stop(grp_cfg->grp, grp_cfg->chn_enable);
        if (ret != CVI_SUCCESS) {
            XLOGW("SAMPLE_COMM_VPSS_Stop grp=%d failed: 0x%x", grp_cfg->grp, ret);
        }
        ctx->vpss_started[i] = CVI_FALSE;
    }

    return CVI_SUCCESS;
}

CVI_S32 X_MMF_VPSS_GetFrame(VPSS_GRP grp, VPSS_CHN chn, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
    if (!frame) {
        return CVI_FAILURE;
    }
    return CVI_VPSS_GetChnFrame(grp, chn, frame, timeout_ms);
}

CVI_S32 X_MMF_VPSS_ReleaseFrame(VPSS_GRP grp, VPSS_CHN chn, VIDEO_FRAME_INFO_S *frame)
{
    if (!frame) {
        return CVI_FAILURE;
    }
    return CVI_VPSS_ReleaseChnFrame(grp, chn, frame);
}

#endif
