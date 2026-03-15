#pragma once

#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include "z_mmf.h"

/**
 * @brief YOLO 目标检测上下文。
 *        持有 TDL handle 以及外部 VPSS 预处理通道的 grp/chn，
 *        生命周期由 yolo_init / yolo_deinit 管理。
 */
typedef struct {
    cvitdl_handle_t handle;   /**< TDL SDK handle（CreateHandle2 创建，vpss 懒初始化占位） */
    VPSS_GRP        nn_grp;   /**< NN 预处理通道所在的 VPSS 组 */
    VPSS_CHN        nn_chn;   /**< NN 预处理通道号 */
} yolo_ctx_t;

/**
 * @brief 初始化 YOLO 检测：
 *        CreateHandle2 → SetSkipVpssPreprocess(true) → OpenModel →
 *        GetVpssChnConfig → 动态添加 VPSS 通道（grp=1, chn=VPSS_CHN1）。
 */
CVI_S32 yolo_init(yolo_ctx_t *ctx);

/**
 * @brief 推理一帧：内部自动从 VPSS NN 通道取帧、推理、释放帧。
 *        调用前请 memset(obj_meta, 0, sizeof(*obj_meta))。
 */
CVI_S32 yolo_detect(yolo_ctx_t *ctx, cvtdl_object_t *obj_meta);

/**
 * @brief 反初始化：移除 VPSS 通道 + 销毁 handle。
 *        应在 z_lib_deinit() 之前调用。
 */
void yolo_deinit(yolo_ctx_t *ctx);