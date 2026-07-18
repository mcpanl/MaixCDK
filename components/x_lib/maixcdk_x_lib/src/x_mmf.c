#include <stdarg.h>
#include <stdlib.h>

#include "x_mmf_priv.h"

static X_MMF_LOG_LEVEL_E g_x_mmf_log_level = X_MMF_LOG_INFO;
/* -1: use default (12 for VI path); else 4..24 passed to X_MMF_SetVbBlkCntHint */
static int g_x_mmf_vb_blk_cnt_hint = -1;

void X_MMF_SetVbBlkCntHint(CVI_U32 blk_cnt)
{
    if (blk_cnt == 0 || blk_cnt < 4 || blk_cnt > 24)
        g_x_mmf_vb_blk_cnt_hint = -1;
    else
        g_x_mmf_vb_blk_cnt_hint = (int)blk_cnt;
}

const char *x_mmf_log_level_name(X_MMF_LOG_LEVEL_E level)
{
    switch (level) {
        case X_MMF_LOG_ERROR: return "ERR";
        case X_MMF_LOG_WARN: return "WRN";
        case X_MMF_LOG_INFO: return "INF";
        case X_MMF_LOG_DEBUG: return "DBG";
        default: return "UNK";
    }
}

void x_mmf_log_write(X_MMF_LOG_LEVEL_E level, const char *func, int line, const char *fmt, ...)
{
    va_list ap;

    if (level > g_x_mmf_log_level) {
        return;
    }

    printf("[X_MMF][%s][%s:%d] ", x_mmf_log_level_name(level), func, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void X_MMF_SetLogLevel(X_MMF_LOG_LEVEL_E level)
{
    g_x_mmf_log_level = level;
}

X_MMF_LOG_LEVEL_E X_MMF_GetLogLevel(void)
{
    return g_x_mmf_log_level;
}

void X_MMF_DefaultConfig(X_MMF_CONFIG_S *cfg)
{
    if (!cfg) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->log_level = X_MMF_LOG_INFO;
    cfg->sys.use_default_vb = CVI_TRUE;

#if X_MMF_ENABLE_VI
    cfg->vi.enable = CVI_TRUE;
    cfg->vi.use_default_ini = CVI_TRUE;
    cfg->vi.vi_dev = 0;
    cfg->vi.vi_pipe = 0;
    cfg->vi.vi_chn = 0;
    cfg->vi.pipe_attr.bYuvSkip = CVI_FALSE;
    cfg->vi.pipe_attr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP;
    cfg->vi.pipe_attr.enBitWidth = DATA_BITWIDTH_12;
    cfg->vi.pipe_attr.bNrEn = CVI_TRUE;
    cfg->vi.pipe_attr.bYuvBypassPath = CVI_FALSE;
    cfg->vi.pipe_attr.stFrameRate.s32SrcFrameRate = -1;
    cfg->vi.pipe_attr.stFrameRate.s32DstFrameRate = -1;
#endif

#if X_MMF_ENABLE_VPSS
    cfg->vpss_grp_count = 1;
    cfg->vpss[0].grp_enable = CVI_TRUE;
    cfg->vpss[0].grp = 0;
    cfg->vpss[0].grp_attr.stFrameRate.s32SrcFrameRate = -1;
    cfg->vpss[0].grp_attr.stFrameRate.s32DstFrameRate = -1;
    cfg->vpss[0].grp_attr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
    cfg->vpss[0].grp_attr.u32MaxW = 1920;
    cfg->vpss[0].grp_attr.u32MaxH = 1080;
    cfg->vpss[0].grp_attr.u8VpssDev = 0;
    cfg->vpss[0].chn_enable[0] = CVI_TRUE;
    cfg->vpss[0].chn_attr[0].u32Width = 640;
    cfg->vpss[0].chn_attr[0].u32Height = 480;
    cfg->vpss[0].chn_attr[0].enVideoFormat = VIDEO_FORMAT_LINEAR;
    cfg->vpss[0].chn_attr[0].enPixelFormat = PIXEL_FORMAT_RGB_888;
    cfg->vpss[0].chn_attr[0].stFrameRate.s32SrcFrameRate = -1;
    cfg->vpss[0].chn_attr[0].stFrameRate.s32DstFrameRate = -1;
    cfg->vpss[0].chn_attr[0].u32Depth = 1;
    cfg->vpss[0].chn_attr[0].stAspectRatio.enMode = ASPECT_RATIO_AUTO;
    cfg->vpss[0].chn_attr[0].stAspectRatio.bEnableBgColor = CVI_TRUE;
    cfg->vpss[0].chn_attr[0].stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
#endif

#if X_MMF_ENABLE_VO
    cfg->vo.enable = CVI_FALSE;
    cfg->vo.vo_layer = 0;
    cfg->vo.vo_chn = 0;
    cfg->vo.set_rotation = CVI_FALSE;
    cfg->vo.rotation = ROTATION_0;
#endif
}

CVI_S32 x_mmf_sys_init(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    VB_CONFIG_S vb_cfg;
    CVI_U32 blk_size = 1920 * 1080 * 3 / 2;
    CVI_U32 blk_cnt = 10;

    memset(&vb_cfg, 0, sizeof(vb_cfg));
    if (ctx->cfg.sys.use_default_vb) {
#if X_MMF_ENABLE_VI
        if (ctx->cfg.vi.enable && ctx->vi_size.u32Width > 0 && ctx->vi_size.u32Height > 0) {
            blk_size = COMMON_GetPicBufferSize(
                ctx->vi_size.u32Width,
                ctx->vi_size.u32Height,
                SAMPLE_PIXEL_FORMAT,
                DATA_BITWIDTH_8,
                COMPRESS_MODE_NONE,
                DEFAULT_ALIGN
            );
            /* Keep enough free blocks for VI/VPSS/VO pipeline buffering. */
            blk_cnt = 12;
            const char *env_bc = getenv("MAIX_MMF_VB_BLK_CNT");
            if (env_bc && env_bc[0] != '\0') {
                long v = strtol(env_bc, NULL, 10);
                if (v >= 4 && v <= 24)
                    blk_cnt = (CVI_U32)v;
            } else if (g_x_mmf_vb_blk_cnt_hint >= 4 && g_x_mmf_vb_blk_cnt_hint <= 24) {
                blk_cnt = (CVI_U32)g_x_mmf_vb_blk_cnt_hint;
            }
        }
#endif
        vb_cfg.u32MaxPoolCnt = 1;
        vb_cfg.astCommPool[0].u32BlkSize = blk_size;
        vb_cfg.astCommPool[0].u32BlkCnt = blk_cnt;
        XLOGI("SYS default VB: blk_size=%u blk_cnt=%u", blk_size, blk_cnt);
    } else {
        memcpy(&vb_cfg, &ctx->cfg.sys.vb_cfg, sizeof(vb_cfg));
    }

    SAMPLE_COMM_SYS_Exit();
    ret = SAMPLE_COMM_SYS_Init(&vb_cfg);
    if (ret != CVI_SUCCESS) {
        XLOGE("SAMPLE_COMM_SYS_Init failed: 0x%x", ret);
        return ret;
    }

    XLOGI("SYS set log level to DEBUG");

    LOG_LEVEL_CONF_S log_conf = {
		.enModId = CVI_ID_VPSS,       /* 要调哪个模块就写哪个，例如 VPSS */
		.s32Level = CVI_DBG_DEBUG,  /* 即 7，最详细 */
	};
	if (CVI_LOG_SetLevelConf(&log_conf) != CVI_SUCCESS) {
		/* 处理失败 */
        XLOGE("SYS set log level to DEBUG failed");
	}

    XLOGI("SYS init ok");
    return CVI_SUCCESS;
}

CVI_S32 x_mmf_sys_deinit(X_MMF_CTX_S *ctx)
{
    (void)ctx;
    SAMPLE_COMM_SYS_Exit();
    XLOGI("SYS exit done");
    return CVI_SUCCESS;
}

/*
 * Best-effort recovery when the previous process died (e.g. kill -9) without X_MMF_Deinit:
 * bring up SYS+VB, tear down VPSS/VI/VO in a safe order, exit SYS/VB, then the caller runs
 * a normal x_mmf_sys_init. This cannot reclaim ION held solely by CVI NPU runtime in another
 * dead process; it targets MMF (VI/VPSS/VO/VB) carveout pressure.
 */
static void x_mmf_orphan_media_precleanup(X_MMF_CTX_S *ctx)
{
    CVI_S32 ret;
    const char *skip = getenv("MAIX_MMF_SKIP_ORPHAN_PRECLEANUP");

    if (skip && skip[0] == '1') {
        return;
    }

    XLOGI("orphan precleanup: recover MMF state after possible unclean exit");

    SAMPLE_COMM_SYS_Exit();

    ret = x_mmf_sys_init(ctx);
    if (ret != CVI_SUCCESS) {
        XLOGW("orphan precleanup: bootstrap SYS init failed 0x%x (continuing)", ret);
    }

#if X_MMF_ENABLE_VPSS
    x_mmf_vpss_precleanup_hard();
#endif

#if X_MMF_ENABLE_VI
    if (ctx->cfg.vi.enable) {
        (void)SAMPLE_COMM_VI_DestroyIsp(&ctx->vi_runtime_cfg);
        (void)SAMPLE_COMM_VI_DestroyVi(&ctx->vi_runtime_cfg);
    }
#endif

#if X_MMF_ENABLE_VO
    SAMPLE_COMM_VO_Exit();
#endif

    SAMPLE_COMM_SYS_Exit();
    XLOGI("orphan precleanup done, SYS/VB released for re-init");
}

CVI_S32 X_MMF_Init(X_MMF_CTX_S *ctx, const X_MMF_CONFIG_S *cfg)
{
    CVI_S32 ret;

    if (!ctx || !cfg) {
        return CVI_FAILURE;
    }

    memset(ctx, 0, sizeof(*ctx));
    memcpy(&ctx->cfg, cfg, sizeof(*cfg));
    X_MMF_SetLogLevel(ctx->cfg.log_level);

#if X_MMF_ENABLE_VI
    if (ctx->cfg.vi.enable) {
        ret = x_mmf_vi_prepare(ctx);
        if (ret != CVI_SUCCESS) {
            return ret;
        }
    }
#endif

    x_mmf_orphan_media_precleanup(ctx);

    ret = x_mmf_sys_init(ctx);
    if (ret != CVI_SUCCESS) {
        return ret;
    }

#if X_MMF_ENABLE_VPSS
    /* 再清一次：orphan 路径失败或驱动在 SYS_Exit 后仍残留时，与 z_lib 行为一致 */
    x_mmf_vpss_precleanup_hard();
#endif

#if X_MMF_ENABLE_VI
    if (ctx->cfg.vi.enable) {
        ret = x_mmf_vi_init(ctx);
        if (ret != CVI_SUCCESS) {
            X_MMF_Deinit(ctx);
            return ret;
        }
    }
#endif

#if X_MMF_ENABLE_VPSS
    ret = x_mmf_vpss_init(ctx);
    if (ret != CVI_SUCCESS) {
        X_MMF_Deinit(ctx);
        return ret;
    }
#endif

#if X_MMF_ENABLE_VO
    if (ctx->cfg.vo.enable) {
        ret = x_mmf_vo_init(ctx);
        if (ret != CVI_SUCCESS) {
            X_MMF_Deinit(ctx);
            return ret;
        }
    }
#endif

#if X_MMF_ENABLE_VENC
    ret = x_mmf_venc_init(ctx);
    if (ret != CVI_SUCCESS) {
        X_MMF_Deinit(ctx);
        return ret;
    }
#endif

#if X_MMF_ENABLE_VDEC
    ret = x_mmf_vdec_init(ctx);
    if (ret != CVI_SUCCESS) {
        X_MMF_Deinit(ctx);
        return ret;
    }
#endif

    ctx->inited = CVI_TRUE;
    XLOGI("X_MMF init done");
    return CVI_SUCCESS;
}

CVI_S32 X_MMF_Deinit(X_MMF_CTX_S *ctx)
{
    if (!ctx) {
        return CVI_FAILURE;
    }

#if X_MMF_ENABLE_REGION
    /* REGION object lifecycle is managed by explicit API. */
#endif

#if X_MMF_ENABLE_VDEC
    x_mmf_vdec_deinit(ctx);
#endif

#if X_MMF_ENABLE_VENC
    x_mmf_venc_deinit(ctx);
#endif

#if X_MMF_ENABLE_VO
    x_mmf_vo_deinit(ctx);
#endif

#if X_MMF_ENABLE_VPSS
    x_mmf_vpss_deinit(ctx);
#endif

#if X_MMF_ENABLE_VI
    x_mmf_vi_deinit(ctx);
#endif

    x_mmf_sys_deinit(ctx);
    ctx->inited = CVI_FALSE;
    XLOGI("X_MMF deinit done");
    return CVI_SUCCESS;
}
