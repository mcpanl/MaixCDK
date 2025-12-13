#pragma once

#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

// 初始化：创建 handle + 加载 yolov8 模型
CVI_S32 od_init(cvitdl_handle_t *tdl_handle);

// 识别：对一帧做 detection，结果写入 obj_meta（外部变量）
CVI_S32 od_detect(
    cvitdl_handle_t tdl_handle,
    VIDEO_FRAME_INFO_S *frame,
    cvtdl_object_t *obj_meta
);

// 销毁：关闭模型 + 销毁 handle
void od_deinit(cvitdl_handle_t tdl_handle);
