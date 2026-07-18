#ifndef __X_MMF_H__
#define __X_MMF_H__

#include "sample_comm.h"

#ifndef X_MMF_ENABLE_VI
#define X_MMF_ENABLE_VI 1
#endif

#ifndef X_MMF_ENABLE_VPSS
#define X_MMF_ENABLE_VPSS 1
#endif

#ifndef X_MMF_ENABLE_VO
#define X_MMF_ENABLE_VO 1
#endif

#ifndef X_MMF_ENABLE_VENC
#define X_MMF_ENABLE_VENC 1
#endif

#ifndef X_MMF_ENABLE_VDEC
#define X_MMF_ENABLE_VDEC 1
#endif

#ifndef X_MMF_ENABLE_REGION
#define X_MMF_ENABLE_REGION 1
#endif

#define X_MMF_MAX_VPSS_GRP 4
#define X_MMF_MAX_VENC_CHN 4
#define X_MMF_MAX_VDEC_CHN 4

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    X_MMF_LOG_ERROR = 0,
    X_MMF_LOG_WARN  = 1,
    X_MMF_LOG_INFO  = 2,
    X_MMF_LOG_DEBUG = 3,
} X_MMF_LOG_LEVEL_E;

typedef struct {
    CVI_BOOL use_default_vb;
    VB_CONFIG_S vb_cfg;
} X_MMF_SYS_CFG_S;

#if X_MMF_ENABLE_VI
typedef struct {
    CVI_BOOL enable;
    CVI_BOOL use_default_ini;
    SAMPLE_INI_CFG_S ini_cfg;
    VI_DEV vi_dev;
    VI_PIPE vi_pipe;
    VI_CHN vi_chn;
    VI_PIPE_ATTR_S pipe_attr;
} X_MMF_VI_CFG_S;
#endif

#if X_MMF_ENABLE_VPSS
typedef struct {
    CVI_BOOL grp_enable;
    VPSS_GRP grp;
    VPSS_GRP_ATTR_S grp_attr;
    CVI_BOOL chn_enable[VPSS_MAX_PHY_CHN_NUM];
    VPSS_CHN_ATTR_S chn_attr[VPSS_MAX_PHY_CHN_NUM];
} X_MMF_VPSS_GRP_CFG_S;
#endif

#if X_MMF_ENABLE_VO
typedef struct {
    CVI_BOOL enable;
    SAMPLE_VO_CONFIG_S vo_cfg;
    CVI_BOOL set_rotation;
    ROTATION_E rotation;
    VO_LAYER vo_layer;
    VO_CHN vo_chn;
} X_MMF_VO_CFG_S;
#endif

#if X_MMF_ENABLE_VENC
typedef struct {
    CVI_BOOL enable;
    VENC_CHN chn;
    VENC_CHN_ATTR_S chn_attr;
    VENC_RECV_PIC_PARAM_S recv_param;
} X_MMF_VENC_CHN_CFG_S;
#endif

#if X_MMF_ENABLE_VDEC
typedef struct {
    CVI_BOOL enable;
    VDEC_CHN chn;
    VDEC_CHN_ATTR_S chn_attr;
} X_MMF_VDEC_CHN_CFG_S;
#endif

typedef struct {
    X_MMF_LOG_LEVEL_E log_level;
    X_MMF_SYS_CFG_S sys;
#if X_MMF_ENABLE_VI
    X_MMF_VI_CFG_S vi;
#endif
#if X_MMF_ENABLE_VPSS
    CVI_U32 vpss_grp_count;
    X_MMF_VPSS_GRP_CFG_S vpss[X_MMF_MAX_VPSS_GRP];
#endif
#if X_MMF_ENABLE_VO
    X_MMF_VO_CFG_S vo;
#endif
#if X_MMF_ENABLE_VENC
    CVI_U32 venc_chn_count;
    X_MMF_VENC_CHN_CFG_S venc[X_MMF_MAX_VENC_CHN];
#endif
#if X_MMF_ENABLE_VDEC
    CVI_U32 vdec_chn_count;
    X_MMF_VDEC_CHN_CFG_S vdec[X_MMF_MAX_VDEC_CHN];
#endif
} X_MMF_CONFIG_S;

typedef struct {
    CVI_BOOL inited;
    X_MMF_CONFIG_S cfg;
#if X_MMF_ENABLE_VI
    SAMPLE_VI_CONFIG_S vi_runtime_cfg;
    SIZE_S vi_size;
#endif
#if X_MMF_ENABLE_VPSS
    CVI_BOOL vpss_started[X_MMF_MAX_VPSS_GRP];
#endif
#if X_MMF_ENABLE_VO
    CVI_BOOL vo_started;
#endif
#if X_MMF_ENABLE_VENC
    CVI_BOOL venc_started[X_MMF_MAX_VENC_CHN];
#endif
#if X_MMF_ENABLE_VDEC
    CVI_BOOL vdec_started[X_MMF_MAX_VDEC_CHN];
#endif
} X_MMF_CTX_S;

void X_MMF_DefaultConfig(X_MMF_CONFIG_S *cfg);
CVI_S32 X_MMF_Init(X_MMF_CTX_S *ctx, const X_MMF_CONFIG_S *cfg);
CVI_S32 X_MMF_Deinit(X_MMF_CTX_S *ctx);

void X_MMF_SetLogLevel(X_MMF_LOG_LEVEL_E level);
X_MMF_LOG_LEVEL_E X_MMF_GetLogLevel(void);

/**
 * Hint for x_mmf_sys_init default VB pool block count when VI is enabled (sensor path).
 * Full-sensor pipelines default to 12 blocks; CVI NPU (cvimodel) ION can then fail with
 * "Out of memory". Call before X_MMF_Init / first MediaRuntime::acquire, or set env
 * MAIX_MMF_VB_BLK_CNT (4–24). Env wins over this hint if set. Pass 0 to clear the hint.
 *
 * After kill -9, set MAIX_MMF_SKIP_ORPHAN_PRECLEANUP=1 to skip the extra SYS/VI/VPSS/VO
 * teardown pass at X_MMF_Init (faster; may leave kernel resources until reboot).
 */
void X_MMF_SetVbBlkCntHint(CVI_U32 blk_cnt);

#if X_MMF_ENABLE_VI
CVI_S32 X_MMF_VI_GetFrame(X_MMF_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 X_MMF_VI_ReleaseFrame(X_MMF_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame);
CVI_S32 X_MMF_BindVIToVPSS(X_MMF_CTX_S *ctx, VPSS_GRP grp);
CVI_S32 X_MMF_UnbindVIFromVPSS(X_MMF_CTX_S *ctx, VPSS_GRP grp);

/* Backward-compatible API aliases. */
typedef X_MMF_CTX_S X_VI_CTX_S;
CVI_S32 X_VI_INIT(X_VI_CTX_S *ctx);
CVI_S32 X_VI_TAKE_FRAME(X_VI_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 X_VI_RELEASE_FRAME(X_VI_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame);
CVI_S32 X_VI_DEINIT(X_VI_CTX_S *ctx);
#endif

#if X_MMF_ENABLE_VPSS
CVI_S32 X_MMF_VPSS_GetFrame(VPSS_GRP grp, VPSS_CHN chn, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 X_MMF_VPSS_ReleaseFrame(VPSS_GRP grp, VPSS_CHN chn, VIDEO_FRAME_INFO_S *frame);
#endif

#if X_MMF_ENABLE_VO
CVI_S32 X_MMF_VO_SendFrame(X_MMF_CTX_S *ctx, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 X_MMF_BindVPSSToVO(VPSS_GRP grp, VPSS_CHN chn, VO_LAYER layer, VO_CHN vo_chn);
CVI_S32 X_MMF_UnbindVPSSToVO(VPSS_GRP grp, VPSS_CHN chn, VO_LAYER layer, VO_CHN vo_chn);
#endif

#if X_MMF_ENABLE_VENC
CVI_S32 X_MMF_BindVPSSToVENC(VPSS_GRP grp, VPSS_CHN chn, VENC_CHN venc_chn);
CVI_S32 X_MMF_UnbindVPSSToVENC(VPSS_GRP grp, VPSS_CHN chn, VENC_CHN venc_chn);
#if X_MMF_ENABLE_VI
CVI_S32 X_MMF_BindVIToVENC(X_MMF_CTX_S *ctx, VENC_CHN venc_chn);
CVI_S32 X_MMF_UnbindVIToVENC(X_MMF_CTX_S *ctx, VENC_CHN venc_chn);
#endif
CVI_S32 X_MMF_VENC_SendFrame(VENC_CHN chn, const VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 X_MMF_VENC_GetStream(VENC_CHN chn, VENC_STREAM_S *stream, CVI_S32 timeout_ms);
CVI_S32 X_MMF_VENC_ReleaseStream(VENC_CHN chn, VENC_STREAM_S *stream);
#endif

#if X_MMF_ENABLE_VDEC
CVI_S32 X_MMF_BindVDECToVPSS(VDEC_CHN vdec_chn, VPSS_GRP grp);
CVI_S32 X_MMF_UnbindVDECToVPSS(VDEC_CHN vdec_chn, VPSS_GRP grp);
CVI_S32 X_MMF_VDEC_SendStream(VDEC_CHN chn, const VDEC_STREAM_S *stream, CVI_S32 timeout_ms);
#endif

#if X_MMF_ENABLE_REGION
CVI_S32 X_MMF_REGION_Create(RGN_HANDLE handle, const RGN_ATTR_S *attr);
CVI_S32 X_MMF_REGION_Destroy(RGN_HANDLE handle);
CVI_S32 X_MMF_REGION_Attach(RGN_HANDLE handle, const MMF_CHN_S *mmf_chn, const RGN_CHN_ATTR_S *chn_attr);
CVI_S32 X_MMF_REGION_Detach(RGN_HANDLE handle, const MMF_CHN_S *mmf_chn);
CVI_S32 X_MMF_REGION_SetBitmap(RGN_HANDLE handle, const BITMAP_S *bitmap);
#endif

#ifdef __cplusplus
}
#endif

#endif