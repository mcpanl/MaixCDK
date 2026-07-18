# RK3566 + RKNN + nn_rk 移植总结

本文档总结了本次在 MaixCDK 中将 RK3566 + RKNN 接入为独立模块 `nn_rk` 的全过程、关键实现、踩坑与解决方案，以及板端验证结果。

## 1. 背景与目标

目标是将 RK3566 的推理能力接入项目，并尽可能兼容原有 `nn` 使用习惯：

- RKNN Runtime 以 3rd 组件方式接入；
- 初期实现最小可用（加载 `.rknn` + 推理）；
- 避免被原 `nn` 中 CVI 相关代码干扰；
- 最终实现用户仅传 `.mud` 即可推理分类，并打印类名。

## 2. 方案演进

### 阶段 A：直接并入 `components/nn`

初版将 RK3566 后端加到 `components/nn`，可编译到一半，但在 RK3566 下触发了 CVI 相关源文件编译错误：

- `cvi_tdl.h` 缺失（`z_nn_test_*`）；
- `cviruntime.h` 缺失（`z_nn_maixcam.cpp`）。

根因：`components/nn` 里历史代码与 MaixCam/CVI 强耦合，平台隔离不彻底。

### 阶段 B：拆分为独立 `components/nn_rk`

根据实际情况改为独立模块：

- 新增 `components/nn_rk`；
- demo 改为依赖 `nn_rk`，不再依赖 `nn`；
- 成功规避 CVI 头文件与测试代码干扰。

该方案最终稳定通过构建并在板端运行。

## 3. 主要实现清单

## 3.1 RKNN Runtime 3rd 组件

路径：

- `components/3rd_party/rknn_runtime/CMakeLists.txt`
- `components/3rd_party/rknn_runtime/rknn_runtime_prebuilt/README.txt`

要点：

- 仅 `PLATFORM_RK3566` 生效；
- 从以下位置查找预编译运行时：
  - `components/3rd_party/rknn_runtime/rknn_runtime_prebuilt`
  - `dl/extracted/rknn_runtime/<pkg>`
- 导出 `include/rknn_api.h` 和 `lib/librknnrt.so`，并链接 `pthread`、`dl`。

## 3.2 nn_rk 核心组件

路径：

- `components/nn_rk/CMakeLists.txt`
- `components/nn_rk/include/z_nn_rk.hpp`
- `components/nn_rk/src/z_nn_rk.cpp`

功能：

- `NN`：支持 `load/unload/forward/forward_image`；
- `MUD`：支持 `.mud` 与 `.rknn` 双入口；
- `extra_info()` / `extra_info_labels()` / `mud()`；
- 自动识别模型输入布局（NCHW/NHWC）；
- 按 `rknn_tensor_attr` 处理输入输出 shape/dtype。

## 3.3 分类专属封装

路径：

- `components/nn_rk/include/z_nn_rk_classifier.hpp`
- `components/nn_rk/include/maix_nn_rk_classifier.hpp`
- `components/nn_rk/include/maix_nn_rk.hpp`（聚合头）

功能：

- `Classifier(model_mud_path)` 一键加载；
- 自动读取 `extra` 配置（`input_type`、`mean`、`scale`、`labels`）；
- `classify()` 返回 `(class_id, score)`；
- `classify_with_names()` 返回 `(class_name, score)`；
- `class_name(idx)` 辅助函数用于索引转名称。

## 3.4 demo 与模型配置

路径：

- `examples/rk3566_rknn_nn_demo/main/src/main.cpp`
- `examples/rk3566_rknn_nn_demo/models/mobilenetv2.mud`
- `examples/rk3566_rknn_nn_demo/models/imagenet_classes.txt`

结果：

- 支持 `mud + image` 直接运行；
- 输出 TopK：索引、类名、分数。

## 4. 关键疑难点与修复

### 问题 1：`invalid RKNN_MAGIC`

现象：

- 传入 ONNX 文件给运行时，`rknn_init` 报格式错误。

原因：

- 运行时只接受 `.rknn`，不接受 `.onnx`。

修复：

- 使用 RKNN-Toolkit2 先将 ONNX 转换成 `.rknn`。

### 问题 2：`rknn_inputs_set size mismatch (2016 < 150528)`

现象：

- 模型输入显示 `[1,224,224,3]`，但实际喂入大小不足。

原因：

- 把 NHWC 输入误当成 CHW，导致 resize 目标被错误计算为 `3x224`。

修复：

- 按 `in_attr.fmt` 自动判断 `RKNN_TENSOR_NHWC` / `RKNN_TENSOR_NCHW`；
- 修正 `model_w/model_h` 与 `to_tensor()` 的布局参数。

### 问题 3：能跑通但结果不准（猫图不出猫）

现象：

- 推理成功，但分类结果明显不合理。

原因：

- 预处理未与导出配置对齐（`mean/scale` 实际未生效）。

修复：

- 在 `forward_image` 中按 mud 参数真正执行预处理；
- 按模型输入 dtype 做数据转换（FP32/FP16/INT8/UINT8）；
- 分类默认 resize 策略调整为 `FIT_FILL`，与常见分类预处理更一致。

### 问题 4：只有类别索引，没有类名

修复：

- 补全 `imagenet_classes.txt` 为 1000 类；
- demo 输出 `topN: [id] class_name => score`。

## 5. 当前验证结果（板端）

命令：

```bash
./dist/rk3566_rknn_nn_demo_release/rk3566_rknn_nn_demo ./models/mobilenetv2.mud ./cat1.jpg
./dist/rk3566_rknn_nn_demo_release/rk3566_rknn_nn_demo ./models/mobilenetv2.mud ./dog1.jpg
```

结果：

- cat1.jpg：
  - Top1 `Persian cat`
  - Top2 `Egyptian cat`
  - Top3 `tabby`
- dog1.jpg：
  - Top1 `Eskimo dog`
  - Top2 `Siberian husky`
  - Top3 `malamute`

结论：当前分类链路已具备可用准确度，且符合预期。

## 6. 使用方式（用户视角）

## 6.1 准备 mud

示例：

```ini
[basic]
type = rknn
model = mobilenetv2-12.rknn

[extra]
model_type = classifier
input_type = rgb
mean = 123.675, 116.28, 103.53
scale = 0.017124753831663668, 0.01750700280112045, 0.017429193899782137
labels = imagenet_classes.txt
```

## 6.2 运行

```bash
export LD_LIBRARY_PATH=./dl_lib:$LD_LIBRARY_PATH
./rk3566_rknn_nn_demo ./models/mobilenetv2.mud ./cat1.jpg
```

## 7. 后续建议

- 增加导出脚本模板（ONNX -> RKNN）并与 mud 自动联动，避免配置漂移；
- 在 `nn_rk` 增加检测/分割等高层封装（类似 classifier）；
- 增加 batch 图片评测脚本（输出 Top1/Top5 命中率）；
- 可选：在输出中增加 raw logits 与 softmax 开关，便于调试量化误差。

---

如需后续并回统一 `nn` 抽象，建议先保持 `nn_rk` 独立稳定，再通过平台适配层做接口收敛，避免再次引入 CVI 侧回归。
