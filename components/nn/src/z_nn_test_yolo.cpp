#include "z_nn_test_yolo.hpp"
#include "z_lib.hpp"
#include <stdio.h>
#include <string.h>

using namespace maix;

/* ================== 配置 ================== */
static const char *YOLO_MODEL_PATH =
    "/root/models/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel";

static const CVI_TDL_SUPPORTED_MODEL_E YOLO_MODEL_ID =
    CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION;

static const float YOLO_CONF_THRESH = 0.45f;
static const float YOLO_NMS_THRESH  = 0.45f;

#define YOLO_VPSS_GRP  1
#define YOLO_VPSS_CHN  VPSS_CHN1
/* ========================================= */


/**
 * @brief 初始化 YOLO 目标检测模块。
 *
 * 核心原则：彻底复用外部已有的 VPSS Group，SDK 内部不再创建任何新 Group。
 *
 * 实现方式：
 *   1. CVI_TDL_CreateHandle2(-1, 0)
 *      - 向 vec_vpss_engine 中压入一个 VpssEngine 占位对象（懒初始化，
 *        构造函数不再调用 init()，因此不会创建任何 VPSS Group）。
 *      - 保证 getInferenceInstance 访问 vec_vpss_engine[0] 时不会越界。
 *
 *   2. CVI_TDL_SetSkipVpssPreprocess(true)  ← 必须在 OpenModel 之前调用
 *      - 在 model instance 被创建时就打上 "skip vpss" 标记。
 *      - 之后所有推理调用中 initVPSSIfNeeded() 都会提前返回，
 *        占位 VpssEngine 的 init() 永远不会被触发，也就永远不会
 *        分配新的 VPSS Group。
 *
 *   3. CVI_TDL_OpenModel  —  加载模型权重
 *
 *   4. CVI_TDL_GetVpssChnConfig  —  查询模型所需 VPSS 通道参数
 *
 *   5. z_lib_vpss_add_nn_chn  —  将该通道动态挂载到 Group 1
 *      帧数据由外部 VPSS 管线预处理完毕后，直接送入推理。
 */
CVI_S32 yolo_init(yolo_ctx_t *ctx)
{
    if (!ctx) return CVI_TDL_FAILURE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->nn_grp = YOLO_VPSS_GRP;
    ctx->nn_chn = YOLO_VPSS_CHN;

    CVI_S32 ret;

    printf("[YOLO] CreateHandle2 (lazy vpss placeholder)...\n");
    ret = CVI_TDL_CreateHandle2(&ctx->handle, -1, 0);
    if (ret != CVI_SUCCESS) {
        printf("[YOLO][ERR] CreateHandle2 failed: 0x%x\n", ret);
        return ret;
    }

    ret = CVI_TDL_SetSkipVpssPreprocess(ctx->handle, YOLO_MODEL_ID, true);
    if (ret != CVI_SUCCESS) {
        printf("[YOLO][ERR] SetSkipVpssPreprocess failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[YOLO] OpenModel: %s\n", YOLO_MODEL_PATH);
    ret = CVI_TDL_OpenModel(ctx->handle, YOLO_MODEL_ID, YOLO_MODEL_PATH);
    if (ret != CVI_SUCCESS) {
        printf("[YOLO][ERR] OpenModel failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    CVI_TDL_SetModelThreshold(ctx->handle, YOLO_MODEL_ID, YOLO_CONF_THRESH);
    CVI_TDL_SetModelNmsThreshold(ctx->handle, YOLO_MODEL_ID, YOLO_NMS_THRESH);

    cvtdl_vpssconfig_t vpss_cfg;
    memset(&vpss_cfg, 0, sizeof(vpss_cfg));
    ret = CVI_TDL_GetVpssChnConfig(ctx->handle, YOLO_MODEL_ID,
                                   Z_WIDTH, Z_HEIGHT, 0, &vpss_cfg);
    if (ret != CVI_SUCCESS) {
        printf("[YOLO][ERR] GetVpssChnConfig failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[YOLO] VPSS config: fmt=%d size=%ux%u normalize=%d factor=%.4f mean=%.4f\n",
           vpss_cfg.chn_attr.enPixelFormat,
           vpss_cfg.chn_attr.u32Width,
           vpss_cfg.chn_attr.u32Height,
           vpss_cfg.chn_attr.stNormalize.bEnable,
           vpss_cfg.chn_attr.stNormalize.factor[0],
           vpss_cfg.chn_attr.stNormalize.mean[0]);

    vpss_cfg.chn_attr.u32Depth = 1;
    vpss_cfg.chn_attr.stFrameRate.s32SrcFrameRate = 30;
    vpss_cfg.chn_attr.stFrameRate.s32DstFrameRate = 30;

    ret = z_lib_vpss_add_nn_chn(ctx->nn_grp, ctx->nn_chn,
                                vpss_cfg.chn_coeff, &vpss_cfg.chn_attr);
    if (ret != CVI_SUCCESS) {
        printf("[YOLO][ERR] z_lib_vpss_add_nn_chn failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[YOLO] Init done. nn_grp=%d nn_chn=%d\n", ctx->nn_grp, ctx->nn_chn);
    return CVI_SUCCESS;
}


/**
 * @brief 推理一帧：
 *   1. 从 VPSS NN 通道取已预处理帧（格式/尺寸已匹配模型输入）
 *   2. 直接送入 Detection（SDK 内部跳过 VPSS，不触发 initVPSSIfNeeded）
 *   3. 释放帧
 */
CVI_S32 yolo_detect(yolo_ctx_t *ctx, cvtdl_object_t *obj_meta)
{
    if (!ctx || !ctx->handle || !obj_meta) return CVI_TDL_FAILURE;

    VIDEO_FRAME_INFO_S stFrm;
    memset(&stFrm, 0, sizeof(stFrm));

    CVI_S32 ret = z_lib_vpss_take_frame_generic(ctx->nn_grp, ctx->nn_chn, &stFrm, 200);
    if (ret != CVI_SUCCESS) {
        return ret;
    }

    ret = CVI_TDL_Detection(ctx->handle, &stFrm, YOLO_MODEL_ID, obj_meta);
    if (ret != CVI_SUCCESS) {
        printf("[YOLO][ERR] Detection failed: 0x%x\n", ret);
    }

    z_lib_vpss_release_frame_generic(ctx->nn_grp, ctx->nn_chn, &stFrm);
    return ret;
}


/**
 * @brief 反初始化：
 *   1. 移除 VPSS NN 通道（应在 z_lib_deinit 之前调用）
 *   2. 销毁 TDL handle
 */
void yolo_deinit(yolo_ctx_t *ctx)
{
    if (!ctx) return;

    if (ctx->handle) {
        printf("[YOLO] Removing VPSS NN channel grp=%d chn=%d...\n",
               ctx->nn_grp, ctx->nn_chn);
        z_lib_vpss_remove_nn_chn(ctx->nn_grp, ctx->nn_chn);

        printf("[YOLO] Destroy tdl handle...\n");
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
    }

    printf("[YOLO] Deinit done\n");
}