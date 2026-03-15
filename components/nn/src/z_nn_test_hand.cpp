#include "z_nn_test_hand.hpp"
#include "z_lib.hpp"
#include <stdio.h>
#include <string.h>

using namespace maix;

/* ================== 配置 ================== */
static const char *HAND_MODEL_PATH =
    "/root/models/yolov8n_det_hand_384_640_INT8_cv181x.cvimodel";

static const CVI_TDL_SUPPORTED_MODEL_E HAND_MODEL_ID =
    CVI_TDL_SUPPORTED_MODEL_HAND_DETECTION;

static const float HAND_CONF_THRESH = 0.45f;
static const float HAND_NMS_THRESH  = 0.45f;

#define HAND_VPSS_GRP  1
#define HAND_VPSS_CHN  VPSS_CHN1
/* ========================================= */


CVI_S32 hand_init(hand_ctx_t *ctx)
{
    if (!ctx) return CVI_TDL_FAILURE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->nn_grp = HAND_VPSS_GRP;
    ctx->nn_chn = HAND_VPSS_CHN;

    CVI_S32 ret;

    printf("[HAND] CreateHandle2 (lazy vpss placeholder)...\n");
    ret = CVI_TDL_CreateHandle2(&ctx->handle, -1, 0);
    if (ret != CVI_SUCCESS) {
        printf("[HAND][ERR] CreateHandle2 failed: 0x%x\n", ret);
        return ret;
    }

    ret = CVI_TDL_SetSkipVpssPreprocess(ctx->handle, HAND_MODEL_ID, true);
    if (ret != CVI_SUCCESS) {
        printf("[HAND][ERR] SetSkipVpssPreprocess failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[HAND] OpenModel: %s\n", HAND_MODEL_PATH);
    ret = CVI_TDL_OpenModel(ctx->handle, HAND_MODEL_ID, HAND_MODEL_PATH);
    if (ret != CVI_SUCCESS) {
        printf("[HAND][ERR] OpenModel failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    CVI_TDL_SetModelThreshold(ctx->handle, HAND_MODEL_ID, HAND_CONF_THRESH);
    CVI_TDL_SetModelNmsThreshold(ctx->handle, HAND_MODEL_ID, HAND_NMS_THRESH);

    cvtdl_vpssconfig_t vpss_cfg;
    memset(&vpss_cfg, 0, sizeof(vpss_cfg));
    ret = CVI_TDL_GetVpssChnConfig(ctx->handle, HAND_MODEL_ID,
                                   Z_WIDTH, Z_HEIGHT, 0, &vpss_cfg);
    if (ret != CVI_SUCCESS) {
        printf("[HAND][ERR] GetVpssChnConfig failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[HAND] VPSS config: fmt=%d size=%ux%u normalize=%d factor=%.4f mean=%.4f\n",
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
        printf("[HAND][ERR] z_lib_vpss_add_nn_chn failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
        return ret;
    }

    printf("[HAND] Init done. nn_grp=%d nn_chn=%d\n", ctx->nn_grp, ctx->nn_chn);
    return CVI_SUCCESS;
}


CVI_S32 hand_detect(hand_ctx_t *ctx, cvtdl_object_t *obj_meta)
{
    if (!ctx || !ctx->handle || !obj_meta) return CVI_TDL_FAILURE;

    VIDEO_FRAME_INFO_S stFrm;
    memset(&stFrm, 0, sizeof(stFrm));

    CVI_S32 ret = z_lib_vpss_take_frame_generic(ctx->nn_grp, ctx->nn_chn, &stFrm, 200);
    if (ret != CVI_SUCCESS) {
        return ret;
    }

    ret = CVI_TDL_Detection(ctx->handle, &stFrm, HAND_MODEL_ID, obj_meta);
    if (ret != CVI_SUCCESS) {
        printf("[HAND][ERR] Detection failed: 0x%x\n", ret);
    }

    z_lib_vpss_release_frame_generic(ctx->nn_grp, ctx->nn_chn, &stFrm);
    return ret;
}


void hand_deinit(hand_ctx_t *ctx)
{
    if (!ctx) return;

    if (ctx->handle) {
        printf("[HAND] Removing VPSS NN channel grp=%d chn=%d...\n",
               ctx->nn_grp, ctx->nn_chn);
        z_lib_vpss_remove_nn_chn(ctx->nn_grp, ctx->nn_chn);

        printf("[HAND] Destroy tdl handle...\n");
        CVI_TDL_DestroyHandle(ctx->handle);
        ctx->handle = NULL;
    }

    printf("[HAND] Deinit done\n");
}