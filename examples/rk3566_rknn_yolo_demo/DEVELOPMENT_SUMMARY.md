# RK3566 RKNN YOLO Demo 开发总结

## 1. 本次目标

基于现有 `rk3566_rknn_nn_demo` 与 `nn_rk` 组件，完成一个新的 YOLO 测试 demo，目标包括：

- 新建 RK3566 专用 demo 工程；
- 下载 YOLO ONNX 并转换为 RKNN；
- 在 `nn_rk` 中补齐 YOLO 相关头文件实现；
- 在 demo 里输出检测框 `xywh`；
- 跑通并修复检测结果异常问题。

---

## 2. 主要产出

### 2.1 新增 demo 工程

目录：`examples/rk3566_rknn_yolo_demo`

新增内容：

- `CMakeLists.txt`
- `main/CMakeLists.txt`
- `main/src/main.cpp`
- `README.md`
- `download_and_convert_yolov5n.py`（默认 Ultralytics 导出 224×224，可选 `--legacy640`）
- `models/yolov5n.mud`（640，Rockchip ONNX 链路）
- `models/yolov5n_224.mud` / `yolov5n_224.rknn`（224 链路，由脚本生成）
- `models/coco80.txt`

功能：

- 加载 `.mud/.rknn` 模型；
- 对输入图片执行检测；
- 打印 `class/score/xywh`；
- 保存 `result.jpg` 可视化结果。

### 2.2 `nn_rk` 组件增强

新增头文件：

- `components/nn_rk/include/z_nn_rk_yolo.hpp`
- `components/nn_rk/include/maix_nn_rk_yolo.hpp`

并在：

- `components/nn_rk/include/maix_nn_rk.hpp`

中导出 YOLO 接口。

实现内容：

- YOLOv5 模型加载、参数解析（`anchors/labels/mean/scale`）；
- 前处理 + 推理；
- 后处理（decode + NMS + bbox 映射）；
- 输出 `DetectObject(x,y,w,h,class_id,score)`。

### 2.3 推理输出格式修正

修改：

- `components/nn_rk/src/z_nn_rk.cpp`

关键调整：

- `rknn_outputs_get` 设置 `want_float = 1`，直接获取反量化 float 输出；
- 输出 tensor 统一按 `FLOAT32` 构建，避免各模型重复做 int8/uint8 反量化处理。

---

## 3. 开发过程与疑难点

本次最核心的难点都在 YOLO 后处理链路，经历了三轮问题定位与修复。

### 阶段 A：异常 1 - 一次输出 4000+ 框

现象：

- 检测日志中出现几千个框，明显异常；
- 类别和坐标分布混乱。

初步判断：

- 使用了量化输出原始值参与后处理，导致置信度计算失真，NMS 失效。

修复：

- RKNN 输出改为 float（`want_float=1`），规避手动反量化遗漏。

---

### 阶段 B：异常 2 - 框数量下降但仍不贴目标

现象：

- 框数量下降（例如几十个），但仍大量重叠且位置偏离；
- 类别也呈现异常集中。

定位思路：

- 检查 YOLO 输出层与 anchor 组的对应关系；
- 发现不能假设输出顺序固定（map 遍历顺序与模型 head 顺序可能不一致）。

修复：

- 后处理前按特征图分辨率排序输出层（大图 -> 小图），再对应 anchors。

---

### 阶段 C：异常 3 - 分数形态可疑（疑似重复激活）

现象：

- 分数分布与预期不符；
- 怀疑输出可能已经过 sigmoid，再次 sigmoid 会导致 decode 失真。

修复：

- 增加输出统计判断（值域近似 `[0,1]`）；
- 若模型输出已 sigmoid，则跳过重复 sigmoid；
- 对 `xywh/object/class` 激活统一走自适应逻辑。

---

## 4. 最终结果

最终在板端验证：

- 检测框能够正确框中目标；
- `xywh` 输出正常；
- 结果图保存正常；
- 工程可编译、可运行，链路闭环完成。

---

## 5. 经验沉淀

- **不要假设 YOLO 输出顺序固定**：head 顺序应通过 shape/stride 识别后再映射；
- **RKNN 量化模型优先取 float 输出**：大幅降低后处理歧义；
- **不同导出模型后处理语义可能不同**：是否已 sigmoid 必须做兼容；
- **问题定位先看分布特征**：框数量级、分数区间、类别集中度能快速指向 decode 问题；
- **先让结果可解释，再调阈值**：阈值只是在正确后处理基础上的“精修”。

---

## 6. 后续可优化项

- 增加 debug 开关，打印各输出层 shape/min/max 与 decode 模式；
- 增加 top-N 输出与按类统计，便于日志阅读；
- 新增摄像头实时检测示例；
- 抽象一层通用 YOLO head 解析，兼容 YOLOv5/YOLOv8 导出差异；
- 若需同时支持「单输出 xyxy」与「单输出 cxcywh」，可在 `.mud` 的 `[extra]` 增加显式字段（例如 `ultralytics_boxes = cxcywh`），避免仅靠形状推断。

---

## 7. 224 模型、Ultralytics 导出与踩坑记录

本节总结在 **640×640 Rockchip ONNX** 之外，增加 **224×224 Ultralytics 导出** 时的代码与文档改动，以及线上遇到的现象与根因。

### 7.1 需求与脚本侧改动

- **目标**：在 RK3566 上用更小输入（如 224×224）做 YOLO 推理，缩短耗时。
- **`download_and_convert_yolov5n.py`**：
  - 默认改为通过 **Ultralytics** 加载 `yolov5n.pt`（实际常解析为 `yolov5nu.pt`）、导出对应 `imgsz` 的 ONNX，再经 RKNN Toolkit 转成 `models/yolov5n_{imgsz}.rknn`，并生成 `models/yolov5n_{imgsz}.mud`。
  - `.mud` 中 **anchors** 仍按 COCO 默认 640 锚框乘以 `imgsz/640` 写入（与 Rockchip 三路 head 解码一致时才有意义）。
  - **`--legacy640`**：保留原先「下载 Rockchip 托管的 640 ONNX → `yolov5n.rknn` / `yolov5n.mud`」路径。
- **依赖**：除 `rknn-toolkit2` 外需安装 **`ultralytics`**（会拉取 PyTorch，环境较重）。

### 7.2 认知纠偏：输入分辨率与 `.mud`

- **输入宽高不是写在 `.mud` 里决定的**，而是由 **RKNN 模型里第一个输入 tensor 的 shape** 决定；`YOLOv5::load()` 从 `inputs_info()` 读取得到 `_input_size`。
- **换 224 模型** 必须 **重导 ONNX → 重转 RKNN**；只改 `.mud` 数字无法把网络输入从 640 改成 224。
- **精度预期**：同一套 COCO 权重仅缩小 `imgsz` 导出，一般会 **变快**、**mAP/小目标** 往往变差，需实测权衡。

### 7.3 坑一：Ultralytics 单输出 ≠ Rockchip 三路 4D head

**现象**

- 日志：`[W] some output layers are not 4D, valid layers: 0/1`
- 推理能跑完，但 **objects: 0**，或后处理完全跳过。

**原因**

- **Rockchip / rknn_model_zoo 式 YOLOv5 ONNX**：通常为 **3 个 4D 输出**（如 `[1, 255, H, W]`），与现有 `_post_process` 里按层 decode、配 anchors 的逻辑一致。
- **Ultralytics 默认导出的检测头**（`end2end=False`）：常为 **单个 3D 输出**，例如 **`[1, 84, 1029]`**（224×224 时 `1029 = 28×28 + 14×14 + 7×7`，即多尺度格子拼成一列；`84 = 4 + 80` 为框参数 + 类别）。

**结论**

- 原后处理只接受 **4D** feature map，会把这种 **单路 3D** 输出全部判为非法 → **必须通过单独分支解码**。

**修改（`components/nn_rk/include/z_nn_rk_yolo.hpp`）**

- 增加 **`_try_decode_ultralytics_concat`**：当 `outputs->size() == 1` 且 shape 为 **`[1, 4+nc, N]`** 或 **`[1, N, 4+nc]`**（及 RKNN 偶发的 2D 挤压 batch）时，按 Ultralytics **concat** 格式解码，再走原有 NMS 与 **`_correct_bbox`**（与 `forward_image` 的 **FIT_CONTAIN**  letterbox 一致）。

### 7.4 坑二：框格式误用 xyxy，实为 cxcywh（框偏、比例看起来不对）

**现象**

- 224 模型已能检出类别与较高 **score**，但 **`result.jpg` 框位置明显错位**；640 Rockchip 模型仍正常。

**原因**

- Ultralytics 中 **`Detect`** 在 **`end2end=False`、`xyxy=False`** 时，`dist2bbox` 使用 **`xywh=True`**，导出张量 **前 4 通道为 `cx, cy, w, h`**（相对 **模型输入分辨率** 的像素），而 **不是** `x1, y1, x2, y2`。
- 若按 xyxy 去解释并转成 `xywh`，再乘 letterbox 逆变换回原始图坐标，视觉上就像 **缩放/比例没对齐**。

**修改**

- 在 `_try_decode_ultralytics_concat` 中改为：`x0 = cx - w/2`，`y0 = cy - h/2`，再输出 `DetectObject(x0, y0, w, h, ...)`。

**注意**

- 若将来使用 **`end2end=True`** 或更换导出工具，单输出的前四维可能是 **xyxy**；当前实现针对 Ultralytics 下 **`[1, 4+nc, N]` 且 `Detect(end2end=False)`** 常见的 **cxcywh**。若混用多种导出，建议在 `.mud` 中增加显式 **box 格式** 字段（见 §6）。

### 7.5 坑三：224 链路里 anchors 与单输出解码

- **单输出 Ultralytics 路径** 的框已在图内用 anchor/stride decode 完毕，**.mud 里的 `anchors` 不参与该分支**，但 **`load()` 仍要求 `anchors` 字段存在**（可保留缩放后的占位锚框）。
- **Rockchip 三路 head** 仍 **依赖** `.mud` 中与训练分辨率匹配的 anchors（640 默认一组；224 若仍用三路 ONNX，需与训练/export 一致，不能乱填）。

### 7.6 端到端核对建议

1. 板端看日志 **`input size: WxH`** 是否与预期一致。
2. 若出现 **`valid layers: 0/1`**：检查 ONNX/RKNN 输出维数与条数，确认是否单输出 3D。
3. 若有框但 **位置不对**：核对导出仓库/版本下 **`Detect` 的 `end2end` / `xyxy`**，确认前四维语义（cxcywh vs xyxy）。
4. 与 Python **Ultralytics `predict`** 同图对比少量框的 xyxy，可快速验证decode 是否与部署一致。

---

## 8. 相关文件一览（224 + Ultralytics）

| 路径 | 说明 |
|------|------|
| `examples/rk3566_rknn_yolo_demo/download_and_convert_yolov5n.py` | 下载/导出 ONNX、转 RKNN、写 `.mud` |
| `examples/rk3566_rknn_yolo_demo/README.md` | 英文简短使用说明 |
| `components/nn_rk/include/z_nn_rk_yolo.hpp` | 三路 4D decode + Ultralytics 单输出 decode + NMS + letterbox 映射 |

