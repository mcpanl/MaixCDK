#include "x_mmf_priv.h"

#include <stdlib.h>

#if X_MMF_ENABLE_VI

static void x_mmf_vi_default_ini(SAMPLE_INI_CFG_S *ini)
{
    memset(ini, 0, sizeof(*ini));
    ini->enSource = VI_PIPE_FRAME_SOURCE_DEV;
    ini->devNum = 1;
    ini->enSnsType[0] = SONY_IMX327_2L_MIPI_2M_30FPS_12BIT;
    ini->enWDRMode[0] = WDR_MODE_NONE;
    ini->s32BusId[0] = 3;
    ini->MipiDev[0] = 0xff;
}

/**
 * Apply optional sensor ini path before ParseIni.
 * Env MAIX_SENSOR_CFG_INI wins over X_MMF_SetSensorIniPath.
 */
static void x_mmf_vi_apply_ini_path_override(void)
{
    const char *path = getenv("MAIX_SENSOR_CFG_INI");
    if (!path || path[0] == '\0')
        path = X_MMF_GetSensorIniPath();
    if (!path || path[0] == '\0')
        return;

    CVI_S32 ret = SAMPLE_COMM_VI_SetIniPath(path);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_SetIniPath(%s) failed: 0x%x", path, ret);
    } else {
        XLOGI("VI sensor ini path override: %s", path);
    }
}

CVI_S32 x_mmf_vi_prepare(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    PIC_SIZE_E pic_size;
    SAMPLE_INI_CFG_S ini;

    x_mmf_vi_apply_ini_path_override();

    if (ctx->cfg.vi.use_default_ini) {
        x_mmf_vi_default_ini(&ini);
    } else {
        memcpy(&ini, &ctx->cfg.vi.ini_cfg, sizeof(ini));
    }

    ret = SAMPLE_COMM_VI_ParseIni(&ini);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_ParseIni failed: 0x%x", ret);
        return ret;
    }

    CVI_VI_SetDevNum(ini.devNum);
    ret = SAMPLE_COMM_VI_IniToViCfg(&ini, &ctx->vi_runtime_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_IniToViCfg failed: 0x%x", ret);
        return ret;
    }

    ret = SAMPLE_COMM_VI_GetSizeBySensor(ctx->vi_runtime_cfg.astViInfo[0].stSnsInfo.enSnsType, &pic_size);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_GetSizeBySensor failed: 0x%x", ret);
        return ret;
    }

    ret = SAMPLE_COMM_SYS_GetPicSize(pic_size, &ctx->vi_size);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_SYS_GetPicSize failed: 0x%x", ret);
        return ret;
    }

    XLOGI("VI prepare ok, sensor size=%ux%u", ctx->vi_size.u32Width, ctx->vi_size.u32Height);
    return CVI_SUCCESS;
}

CVI_S32 x_mmf_vi_init(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    VI_PIPE_ATTR_S pipe_attr;

    ret = SAMPLE_COMM_VI_StartSensor(&ctx->vi_runtime_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_StartSensor failed: 0x%x", ret);
        return ret;
    }

    ret = SAMPLE_COMM_VI_StartDev(&ctx->vi_runtime_cfg.astViInfo[0]);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_StartDev failed: 0x%x", ret);
        return ret;
    }

    ret = SAMPLE_COMM_VI_StartMIPI(&ctx->vi_runtime_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_StartMIPI failed: 0x%x", ret);
        return ret;
    }

    memcpy(&pipe_attr, &ctx->cfg.vi.pipe_attr, sizeof(pipe_attr));
    pipe_attr.u32MaxW = ctx->vi_size.u32Width;
    pipe_attr.u32MaxH = ctx->vi_size.u32Height;
    pipe_attr.enCompressMode = ctx->vi_runtime_cfg.astViInfo[0].stChnInfo.enCompressMode;

    ret = CVI_VI_CreatePipe(ctx->cfg.vi.vi_pipe, &pipe_attr);
    if (ret != CVI_SUCCESS) {
        XLOGE("CVI_VI_CreatePipe failed: 0x%x", ret);
        return ret;
    }

    ret = CVI_VI_StartPipe(ctx->cfg.vi.vi_pipe);
    if (ret != CVI_SUCCESS) {
        XLOGE("CVI_VI_StartPipe failed: 0x%x", ret);
        return ret;
    }

    ret = SAMPLE_COMM_VI_CreateIsp(&ctx->vi_runtime_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_CreateIsp failed: 0x%x", ret);
        return ret;
    }

    ret = SAMPLE_COMM_VI_StartViChn(&ctx->vi_runtime_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_VI_StartViChn failed: 0x%x", ret);
        return ret;
    }

    /* DUAL mode: VPSS device 0 handles display (user-push), device 1 handles VI capture.
     * Must be set after VI starts and before VPSS groups are created. */
    CVI_SYS_SetVPSSMode(VPSS_MODE_DUAL);
    VI_VPSS_MODE_S stViVpssMode;
    stViVpssMode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
    stViVpssMode.aenMode[1] = VI_ONLINE_VPSS_OFFLINE;
    CVI_SYS_SetVIVPSSMode(&stViVpssMode);
    XLOGI("VI_VPSS mode set to DUAL");

    XLOGI("VI init ok, size=%ux%u", ctx->vi_size.u32Width, ctx->vi_size.u32Height);
    return CVI_SUCCESS;
}

CVI_S32 x_mmf_vi_deinit(X_MMF_CTX_S *ctx)
{
    if (!ctx->cfg.vi.enable) {
        return CVI_SUCCESS;
    }

    SAMPLE_COMM_VI_DestroyIsp(&ctx->vi_runtime_cfg);
    SAMPLE_COMM_VI_DestroyVi(&ctx->vi_runtime_cfg);
    XLOGI("VI deinit done");
    return CVI_SUCCESS;
}

CVI_S32 X_MMF_VI_GetFrame(X_MMF_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
    if (!ctx || !frame) {
        return CVI_FAILURE;
    }
    return CVI_VI_GetChnFrame(ctx->cfg.vi.vi_pipe, ctx->cfg.vi.vi_chn, frame, timeout_ms);
}

CVI_S32 X_MMF_VI_ReleaseFrame(X_MMF_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame)
{
    if (!ctx || !frame) {
        return CVI_FAILURE;
    }
    return CVI_VI_ReleaseChnFrame(ctx->cfg.vi.vi_pipe, ctx->cfg.vi.vi_chn, frame);
}

CVI_S32 X_MMF_BindVIToVPSS(X_MMF_CTX_S *ctx, VPSS_GRP grp)
{
    if (!ctx) {
        return CVI_FAILURE;
    }
    return SAMPLE_COMM_VI_Bind_VPSS(ctx->cfg.vi.vi_pipe, ctx->cfg.vi.vi_chn, grp);
}

CVI_S32 X_MMF_UnbindVIFromVPSS(X_MMF_CTX_S *ctx, VPSS_GRP grp)
{
    if (!ctx) {
        return CVI_FAILURE;
    }
    return SAMPLE_COMM_VI_UnBind_VPSS(ctx->cfg.vi.vi_pipe, ctx->cfg.vi.vi_chn, grp);
}

CVI_S32 X_VI_INIT(X_VI_CTX_S *ctx)
{
    X_MMF_CONFIG_S cfg;

    X_MMF_DefaultConfig(&cfg);
#if X_MMF_ENABLE_VPSS
    cfg.vpss_grp_count = 0;
#endif
#if X_MMF_ENABLE_VO
    cfg.vo.enable = CVI_FALSE;
#endif
    return X_MMF_Init(ctx, &cfg);
}

CVI_S32 X_VI_TAKE_FRAME(X_VI_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
    return X_MMF_VI_GetFrame(ctx, frame, timeout_ms);
}

CVI_S32 X_VI_RELEASE_FRAME(X_VI_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame)
{
    return X_MMF_VI_ReleaseFrame(ctx, frame);
}

CVI_S32 X_VI_DEINIT(X_VI_CTX_S *ctx)
{
    return X_MMF_Deinit(ctx);
}

#endif
