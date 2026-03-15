#include "z_nn_test_face.hpp"
#include "z_lib.hpp"
#include <stdio.h>
#include <string.h>

using namespace maix;

/* ================== 配置 ================== */
static const char *FD_MODEL_PATH =
    "/root/models/scrfd_det_face_432_768_INT8_cv181x.cvimodel";

static const CVI_TDL_SUPPORTED_MODEL_E FD_MODEL_ID =
    CVI_TDL_SUPPORTED_MODEL_SCRFDFACE;

static const float FD_CONF_THRESH = 0.45f;
static const float FD_NMS_THRESH  = 0.45f;

/* NN 预处理通道挂载在已启动的 VPSS Group 1 CHN1 */
#define FD_VPSS_GRP  1
#define FD_VPSS_CHN  VPSS_CHN1
/* ========================================= */


/**
 * @brief 初始化人脸检测模块。
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
CVI_S32 fd_init(fd_ctx_t *ctx)
{
    if (!ctx) return CVI_TDL_FAILURE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->nn_grp = FD_VPSS_GRP;
    ctx->nn_chn = FD_VPSS_CHN;

    CVI_S32 ret;

    /* 步骤 1：创建 handle，vec_vpss_engine[0] 是懒初始化占位对象，不创建 VPSS Group */
    printf("[FD] CreateHandle2 (lazy vpss placeholder)...\n");
    ret = CVI_TDL_CreateHandle2(&ctx->handle, -1, 0);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] CreateHandle2 failed: 0x%x\n", ret);
        return ret;
    }

    /* 步骤 2：在 OpenModel 之前设置 skip，确保 model instance 创建时就携带该标记。
     *        这样 initVPSSIfNeeded 在任何推理调用中都会提前返回，
     *        占位 VpssEngine 的 init() 永远不会被触发。 */
    ret = CVI_TDL_SetSkipVpssPreprocess(ctx->handle, FD_MODEL_ID, true);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] SetSkipVpssPreprocess failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    /* 步骤 3：加载模型 */
    printf("[FD] OpenModel: %s\n", FD_MODEL_PATH);
    ret = CVI_TDL_OpenModel(ctx->handle, FD_MODEL_ID, FD_MODEL_PATH);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] OpenModel failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    /* 设置检测阈值 */
    CVI_TDL_SetModelThreshold(ctx->handle, FD_MODEL_ID, FD_CONF_THRESH);
    CVI_TDL_SetModelNmsThreshold(ctx->handle, FD_MODEL_ID, FD_NMS_THRESH);

    /* 步骤 4：查询模型所需 VPSS 通道配置
     *   输入：相机输出分辨率（Z_WIDTH × Z_HEIGHT）
     *   输出：目标尺寸、像素格式（BGR_888_PLANAR）、归一化系数 */
    cvtdl_vpssconfig_t vpss_cfg;
    memset(&vpss_cfg, 0, sizeof(vpss_cfg));
    ret = CVI_TDL_GetVpssChnConfig(ctx->handle, FD_MODEL_ID,
                                   Z_WIDTH, Z_HEIGHT, 0, &vpss_cfg);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] GetVpssChnConfig failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[FD] VPSS config: fmt=%d size=%ux%u normalize=%d factor=%.4f mean=%.4f\n",
           vpss_cfg.chn_attr.enPixelFormat,
           vpss_cfg.chn_attr.u32Width,
           vpss_cfg.chn_attr.u32Height,
           vpss_cfg.chn_attr.stNormalize.bEnable,
           vpss_cfg.chn_attr.stNormalize.factor[0],
           vpss_cfg.chn_attr.stNormalize.mean[0]);

    /* 启用帧缓冲，使 CVI_VPSS_GetChnFrame 能主动取帧；帧率跟随相机 */
    vpss_cfg.chn_attr.u32Depth = 1;
    vpss_cfg.chn_attr.stFrameRate.s32SrcFrameRate = 30;
    vpss_cfg.chn_attr.stFrameRate.s32DstFrameRate = 30;

    /* 步骤 5：将此通道动态添加到 VIO 管线已启动的 Group 1 */
    ret = z_lib_vpss_add_nn_chn(ctx->nn_grp, ctx->nn_chn,
                                 vpss_cfg.chn_coeff, &vpss_cfg.chn_attr);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] z_lib_vpss_add_nn_chn failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[FD] Init done. nn_grp=%d nn_chn=%d\n", ctx->nn_grp, ctx->nn_chn);
    return CVI_SUCCESS;
}


/**
 * @brief 推理一帧：
 *   1. 从 VPSS NN 通道取已预处理帧（格式已是 BGR_888_PLANAR，尺寸为模型输入）
 *   2. 直接送入 FaceDetection（SDK 内部跳过 VPSS，不触发 initVPSSIfNeeded）
 *   3. 释放帧
 */
CVI_S32 fd_detect(fd_ctx_t *ctx, cvtdl_face_t *obj_meta)
{
    if (!ctx || !ctx->handle || !obj_meta) return CVI_TDL_FAILURE;

    VIDEO_FRAME_INFO_S stFrm;
    memset(&stFrm, 0, sizeof(stFrm));

    CVI_S32 ret = z_lib_vpss_take_frame_generic(ctx->nn_grp, ctx->nn_chn, &stFrm, 200);
    if (ret != CVI_SUCCESS) {
        return ret;
    }

    ret = CVI_TDL_FaceDetection(ctx->handle, &stFrm, FD_MODEL_ID, obj_meta);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] FaceDetection failed: 0x%x\n", ret);
    }

    z_lib_vpss_release_frame_generic(ctx->nn_grp, ctx->nn_chn, &stFrm);
    return ret;
}


/**
 * @brief 反初始化：
 *   1. 移除 VPSS NN 通道（应在 z_lib_deinit 之前调用）
 *   2. 销毁 TDL handle
 */
void fd_deinit(fd_ctx_t *ctx)
{
    if (!ctx) return;

    if (ctx->handle) {
        printf("[FD] Removing VPSS NN channel grp=%d chn=%d...\n",
               ctx->nn_grp, ctx->nn_chn);
        z_lib_vpss_remove_nn_chn(ctx->nn_grp, ctx->nn_chn);

        printf("[FD] Destroy tdl handle...\n");
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
    }

    printf("[FD] Deinit done\n");
}
