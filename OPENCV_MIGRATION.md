# OpenCV 双版本冲突排查与重构记录

> 日期：2026-03-16  
> 平台：MaixCAM（SG200X，RISC-V 64bit，musl libc）  
> 结果：**✅ 成功消除 opencv 3.2 / 4.9 双版本共存导致的 segfault**

---

## 一、问题现象

程序在调用 `maix::image::Image::resize()` 时必现 Segmentation Fault，其他功能（cvi-tdl-sdk 推理等）完全正常。

```
-- [I] load image /root/cat2.jpg success: Image(800, 800, RGB888), data size: 1920000
-- [I] will resize image
Segmentation fault
```

内核日志：

```
z_nn_new[343]: unhandled signal 11 code 0x1 at 0x0000000000000000
  in libopencv_imgproc.so.3.2[...]
badaddr: 0x0000000000000000   ← 空指针
cause: 0x000000000000000d     ← Load page fault
```

---

## 二、根因分析

### 2.1 双版本共存

通过 `readelf -d z_nn_new | grep NEEDED` 发现，最终可执行文件同时 NEEDED 两套 opencv：

```
NEEDED  libopencv_imgproc.so.3.2   ← 第 2 条（靠前！）
NEEDED  libopencv_core.so.3.2
NEEDED  libopencv_imgcodecs.so.3.2
...（40+ 个 cvi sdk 库）...
NEEDED  libopencv_imgproc.so.409   ← 第 52 条（靠后）
NEEDED  libopencv_core.so.409
```

### 2.2 "先到先得"规则触发 ABI 错位

ELF 动态链接器按 NEEDED 顺序注册符号，**先加载的符号胜出，后加载的同名符号被忽略**。

| 时机 | 实际发生的事 |
|---|---|
| 进程启动早期 | `.so.3.2` 率先加载 → `cv::resize` 等所有符号注册到全局表 |
| 进程启动晚期 | `.so.409` 加载 → 符号已存在 → **全部被忽略** |
| `z_image.cpp` 调用 `cv::resize` | 用 4.9 头文件编译的代码，实际调用到 Sophgo 定制 `.so.3.2` 的实现 |
| **崩溃** | 结构体内存布局（4.9 ABI）与运行时实现（3.2 ABI）完全不匹配 → 访问 null → segfault |

### 2.3 `.so.3.2` 为何排这么靠前

追踪依赖链：

```
z_nn_new
 └── libcvi-tdl-sdk.so (MaixCDK 编译的包装层)
      └── libaaa_imgcodecs.so  ← DT_SONAME = libopencv_imgcodecs.so.3.2
      └── libaaa_imgproc.so    ← DT_SONAME = libopencv_imgproc.so.3.2
      └── libcvi_tdl.so        ← NEEDED libopencv_core.so.3.2 (旧版 Sophgo 预编译)
```

`libaaa_*.so` 是 Sophgo 为 opencv 3.2 专门制作的**兼容转发垫片**，其 `DT_SONAME` 字段本身就声明为 `libopencv_*.so.3.2`，导致链接器把它们的名字写进了 NEEDED 列表。

### 2.4 为何 `ADD_DIST_LIB_IGNORE` 也有责任

[components/3rd_party/opencv/CMakeLists.txt](components/3rd_party/opencv/CMakeLists.txt) 中：

```cmake
list(APPEND ADD_DYNAMIC_LIB ${opencv_libs})
list(APPEND ADD_DIST_LIB_IGNORE ${opencv_libs})  ← opencv 4.9 被排除出 dist 包
```

dist 包里没有 opencv 4.9 的 `.so`，设备系统上有 opencv 3.2，动态链接器只能回退到系统的 3.2 版本，加剧了冲突。

---

## 三、修复步骤

### Step 1：重编译 `libcvi_tdl.so` 等依赖 opencv 3.2 的核心库

在 `duo-buildroot-sdk-v2/tdl_sdk/` 中，将编译时链接的 opencv 头文件和 `.so` 替换为 `opencv4_lib_maixcam_musl_4.9.0`，重新编译，得到依赖 `.so.409` 的新版本。

```bash
# 验证新产物
readelf -d tdl_sdk/install/lib/libcvi_tdl.so | grep "NEEDED.*opencv"
# 期望输出全部为 .so.409
```

### Step 2：将新库替换到 MaixCDK 组件目录

```bash
SRC=~/maix/duo-buildroot-sdk-v2/tdl_sdk/install/lib
DST=~/maix/MaixCDK/components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib

for so in libcvi_tdl.so libcvi_draw_rect.so libcvi_kit.so \
           libcvi_md.so libcvi_preprocess.so libcvi_tdl_app.so; do
    cp $SRC/$so $DST/$so
done
```

### Step 3：用 patchelf 修改 `libaaa_*.so` 的 NEEDED 条目

这两个文件的 NEEDED 仍指向 3.2（虽然它们自身的 DT_SONAME 也是 3.2，但 NEEDED 可以独立修改）：

```bash
LIB_DIR=~/maix/MaixCDK/components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib

patchelf --replace-needed libopencv_imgproc.so.3.2 libopencv_imgproc.so.409 $LIB_DIR/libaaa_imgcodecs.so
patchelf --replace-needed libopencv_core.so.3.2    libopencv_core.so.409    $LIB_DIR/libaaa_imgcodecs.so
patchelf --replace-needed libopencv_core.so.3.2    libopencv_core.so.409    $LIB_DIR/libaaa_imgproc.so
```

### Step 4：从 CMakeLists 中移除 `libaaa_*.so` 的链接声明

文件：[components/3rd_party/cvi-tdl-sdk/CMakeLists.txt](components/3rd_party/cvi-tdl-sdk/CMakeLists.txt)

```cmake
# 注释掉这三行（libaaa_*.so 的 DT_SONAME 就是 libopencv_*.so.3.2，
# 只要链接它们，NEEDED 里就必然出现 3.2）
# list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_imgcodecs.so")
# list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_imgproc.so")
# list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_core.so")
```

### Step 5：重新编译并将 opencv 4.9 打包进 dist

```bash
cd examples/z_nn_new
maixcdk clean && maixcdk build2 --build-type=Debug

# dist 包不含 opencv（ADD_DIST_LIB_IGNORE），需手动补充
SRC=~/maix/MaixCDK/dl/extracted/opencv/opencv4/opencv4_lib_maixcam_musl_4.9.0/dl_lib
DST=examples/z_nn_new/dist/z_nn_new_debug/dl_lib

cp $SRC/libopencv_core.so.409 \
   $SRC/libopencv_imgproc.so.409 \
   $SRC/libopencv_imgcodecs.so.409 \
   $SRC/libopencv_highgui.so.409 \
   $SRC/libopencv_video.so.409 $DST/
```

---

## 四、验证结果

### 编译产物扫描（无任何 3.2 NEEDED）

```
=== 可执行文件 z_nn_new opencv NEEDED ===
libopencv_core.so.409
libopencv_highgui.so.409
libopencv_imgcodecs.so.409
libopencv_imgproc.so.409
libopencv_video.so.409

=== dist/dl_lib 所有 .so ===
libcvi-tdl-sdk.so  → 全部 .so.409
libcvi_tdl.so      → 全部 .so.409
libcvi_tdl_app.so  → 全部 .so.409

✅ 无任何 opencv 3.2 NEEDED 残留
```

> 注：`libaaa_*.so` 自身的 `DT_SONAME` 字段仍显示 `.so.3.2`，这是它们的身份元数据，不影响运行（已无任何库声明 NEEDED 它们）。

### 设备运行结果

```
-- [I] Program start
-- [I] load image /root/cat2.jpg success: Image(800, 800, RGB888), data size: 1920000
-- [I] will resize image
-- [I] resize image /root/cat2.jpg success: Image(224, 224, RGB888), data size: 150528
-- [I] Program exit
```

---

## 五、关键知识点

### 1. ELF 动态链接器符号优先级

同一进程中，**NEEDED 顺序越靠前的库，其符号优先级越高**。后加载的同名符号会被静默忽略（而非报错）。这使得版本混用导致的 ABI 错位问题极难从报错信息直接定位。

### 2. DT_SONAME vs 文件名

- `DT_SONAME`：库文件内部声明的"逻辑名"，链接时被写入依赖方的 NEEDED 条目
- 文件名：动态链接器在磁盘上实际查找的名字
- 两者可以不同，`libaaa_imgcodecs.so`（文件名）的 `DT_SONAME` 就是 `libopencv_imgcodecs.so.3.2`

### 3. patchelf 的用途

`patchelf` 可在不重新编译的情况下，直接修改 ELF 二进制的 NEEDED 条目、RPATH、SONAME 等动态链接元数据，是处理预编译闭源库依赖问题的利器。

```bash
# 将某个 .so 的 NEEDED 从旧名改为新名
patchelf --replace-needed 旧SONAME 新SONAME 目标文件.so

# 修改自身 SONAME
patchelf --set-soname 新SONAME 目标文件.so
```

### 4. 符号链接的等价替代（临时方案）

如果无法修改 NEEDED，也可以在运行时目录建兼容符号链接：

```bash
ln -s libopencv_imgproc.so.409 libopencv_imgproc.so.3.2
```

动态链接器按文件名查找，找到符号链接后加载真正的 4.9 文件，两个 SONAME 最终指向同一个已加载实例，冲突消除。此方案无需重编译，适合快速验证，但最终应以 patchelf 或重编译方式彻底解决。

---

## 六、受影响的文件清单

| 文件 | 修改内容 |
|---|---|
| `components/3rd_party/cvi-tdl-sdk/CMakeLists.txt` | 注释掉三行 `libaaa_*.so` 的 `ADD_DYNAMIC_LIB` |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libcvi_tdl.so` | 替换为重编译版（依赖 .so.409） |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libcvi_tdl_app.so` | 同上 |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libcvi_draw_rect.so` | 同上 |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libcvi_kit.so` | 同上 |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libcvi_md.so` | 同上 |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libcvi_preprocess.so` | 同上 |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libaaa_imgcodecs.so` | patchelf 替换 NEEDED 为 .so.409 |
| `components/3rd_party/cvi-tdl-sdk/tdl_sdk_milkv/lib/libaaa_imgproc.so` | patchelf 替换 NEEDED 为 .so.409 |
