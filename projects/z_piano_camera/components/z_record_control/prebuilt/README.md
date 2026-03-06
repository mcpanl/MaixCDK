# prebuilt/ —— RecordControl 预编译库目录

## 用途

当 `src/z_record_control.cpp` **不存在**时，CMake 将自动使用本目录下的
`libz_record_control.so` 进行链接，实现对录制控制部分的**闭源发布**。

当 `src/z_record_control.cpp` **存在**时，CMake 优先从源码编译，
生成新的 `libz_record_control.so`，本目录内容不使用。

---

## 如何生成 / 更新预编译库

```bash
# 1. 确保 src/z_record_control.cpp 存在
# 2. 正常编译项目（以 maixcam 平台为例）
cd <项目根目录>
maixcdk build

# 3. 将编译产物复制到本目录
cp build/z_record_control/libz_record_control.so \
   components/z_record_control/prebuilt/libz_record_control.so

# 4. 闭源：移走或删除 src/z_record_control.cpp
mv components/z_record_control/src/z_record_control.cpp \
   /safe/backup/z_record_control.cpp

# 5. 重新编译，CMake 自动切换到 prebuilt 模式
maixcdk build
```
