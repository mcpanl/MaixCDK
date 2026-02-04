#pragma once

#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

// scrfd_det_face_432_768_INT8_cv181x.cvimodel
// 初始化：创建 handle + 加载 face 模型
CVI_S32 fd_init(cvitdl_handle_t *tdl_handle);

// 识别：对一帧做 detection，结果写入 obj_meta（外部变量）
CVI_S32 fd_detect(
    cvitdl_handle_t tdl_handle,
    VIDEO_FRAME_INFO_S *frame,
    cvtdl_face_t *obj_meta
);

// 销毁：关闭模型 + 销毁 handle
void fd_deinit(cvitdl_handle_t tdl_handle);
