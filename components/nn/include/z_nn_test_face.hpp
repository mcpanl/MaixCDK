#pragma once

#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include "z_mmf.h"   /* VPSS_GRP, VPSS_CHN */

/**
 * @brief NN 人脸检测上下文。
 *        持有 TDL handle 以及外部 VPSS 预处理通道的 grp/chn，
 *        生命周期由 fd_init / fd_deinit 管理。
 */
typedef struct {
    cvitdl_handle_t handle;   /**< TDL SDK handle（CreateHandle2 创建，vpss 懒初始化占位）*/
    VPSS_GRP        nn_grp;   /**< NN 预处理通道所在的 VPSS 组 */
    VPSS_CHN        nn_chn;   /**< NN 预处理通道号 */
} fd_ctx_t;

/**
 * @brief 初始化人脸检测：
 *        CreateHandle2 → SetSkipVpssPreprocess(true) → OpenModel →
 *        GetVpssChnConfig → 动态添加 VPSS 通道（grp=1, chn=VPSS_CHN1）
 *
 *        SetSkipVpssPreprocess 必须在 OpenModel 之前调用，这样 model instance
 *        创建时就携带 skip 标记，后续所有推理中 initVPSSIfNeeded 均提前返回，
 *        SDK 内部不会创建任何新的 VPSS Group。
 */
CVI_S32 fd_init(fd_ctx_t *ctx);

/**
 * @brief 推理一帧：内部自动从 VPSS NN 通道取帧、推理、释放帧。
 *        调用前请 memset(obj_meta, 0, sizeof(*obj_meta))。
 */
CVI_S32 fd_detect(fd_ctx_t *ctx, cvtdl_face_t *obj_meta);

/**
 * @brief 反初始化：移除 VPSS 通道 + 销毁 handle。
 *        应在 z_lib_deinit() 之前调用。
 */
void fd_deinit(fd_ctx_t *ctx);
