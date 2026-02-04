#include "z_nn_test_face.hpp"
#include <stdio.h>
#include <string.h>

/* ================== 写死的配置 ================== */
static const char *FD_MODEL_PATH =
    "/root/models/scrfd_det_face_432_768_INT8_cv181x.cvimodel";

static const CVI_TDL_SUPPORTED_MODEL_E FD_MODEL_ID =
    CVI_TDL_SUPPORTED_MODEL_SCRFDFACE;

static const float FD_CONF_THRESH = 0.45f;
static const float FD_NMS_THRESH  = 0.45f;
/* ================================================= */


/**
 * @brief 初始化目标检测（创建 handle + 加载模型）
 */
CVI_S32 fd_init(cvitdl_handle_t *tdl_handle)
{
    if (!tdl_handle) {
        return CVI_TDL_FAILURE;
    }

    CVI_S32 ret;

    printf("[FD] Create tdl handle...\n");
    ret = CVI_TDL_CreateHandle2(tdl_handle, 3, 0);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] CreateHandle failed: 0x%x\n", ret);
        return ret;
    }

    printf("[FD] Open FACE model...\n");
    ret = CVI_TDL_OpenModel(*tdl_handle, FD_MODEL_ID, FD_MODEL_PATH);
    if (ret != CVI_SUCCESS) {
        printf("[FD][ERR] OpenModel failed: 0x%x\n", ret);
        CVI_TDL_DestroyHandle(*tdl_handle);
        *tdl_handle = NULL;
        return ret;
    }

    // 设置阈值（写死）
    CVI_TDL_SetModelThreshold(*tdl_handle, FD_MODEL_ID, FD_CONF_THRESH);
    CVI_TDL_SetModelNmsThreshold(*tdl_handle, FD_MODEL_ID, FD_NMS_THRESH);

    printf("[FD] Init done\n");
    return CVI_SUCCESS;
}


/**
 * @brief 对单帧做目标检测
 */
CVI_S32 fd_detect(
    cvitdl_handle_t tdl_handle,
    VIDEO_FRAME_INFO_S *frame,
    cvtdl_face_t *obj_meta
)
{
    if (!tdl_handle || !frame || !obj_meta) {
        return CVI_TDL_FAILURE;
    }

    // ⚠️ 调用前，外部可以先 memset(obj_meta, 0, sizeof)
    return CVI_TDL_FaceDetection(
        tdl_handle,
        frame,
        FD_MODEL_ID,
        obj_meta
    );
}


/**
 * @brief 反初始化（释放模型 + handle）
 */
void fd_deinit(cvitdl_handle_t tdl_handle)
{
    if (!tdl_handle) {
        return;
    }

    printf("[FD] Destroy tdl handle...\n");

    // 如果后续你需要 CloseModel，也可以加
    // CVI_TDL_CloseModel(tdl_handle, OD_MODEL_ID);

    CVI_TDL_DestroyHandle(tdl_handle);

    printf("[FD] Deinit done\n");
}
