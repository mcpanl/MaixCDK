#ifndef __X_MMF_PRIV_H__
#define __X_MMF_PRIV_H__

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "x_mmf.h"
#include "x_mmf_log.h"

const char *x_mmf_log_level_name(X_MMF_LOG_LEVEL_E level);

CVI_S32 x_mmf_sys_init(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_sys_deinit(X_MMF_CTX_S *ctx);

#if X_MMF_ENABLE_VI
CVI_S32 x_mmf_vi_prepare(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_vi_init(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_vi_deinit(X_MMF_CTX_S *ctx);
#endif

#if X_MMF_ENABLE_VPSS
/* 与 z_lib Z_VI_INIT 前段一致：拆掉残留 VPSS/绑定，避免上一进程未 deinit 时二次 CreateGrp 失败 */
void x_mmf_vpss_precleanup_hard(void);
CVI_S32 x_mmf_vpss_init(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_vpss_deinit(X_MMF_CTX_S *ctx);
#endif

#if X_MMF_ENABLE_VO
CVI_S32 x_mmf_vo_init(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_vo_deinit(X_MMF_CTX_S *ctx);
#endif

#if X_MMF_ENABLE_VENC
CVI_S32 x_mmf_venc_init(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_venc_deinit(X_MMF_CTX_S *ctx);
#endif

#if X_MMF_ENABLE_VDEC
CVI_S32 x_mmf_vdec_init(X_MMF_CTX_S *ctx);
CVI_S32 x_mmf_vdec_deinit(X_MMF_CTX_S *ctx);
#endif

#endif
