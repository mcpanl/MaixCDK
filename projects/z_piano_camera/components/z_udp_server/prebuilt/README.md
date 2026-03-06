# prebuilt/ —— UDP Server 预编译库目录

## 用途

当 `src/z_udp_server.cpp` **不存在**时，CMake 将自动使用本目录下的
`libz_udp_server.so` 进行链接，实现对 UDP 图传部分的**闭源发布**。

当 `src/z_udp_server.cpp` **存在**时，CMake 优先从源码编译，
生成新的 `libz_udp_server.so`，本目录内容不使用。

---

## 如何生成 / 更新预编译库

```bash
# 1. 确保 src/z_udp_server.cpp 存在
# 2. 正常编译项目（以 maixcam 平台为例）
cd <项目根目录>
maixcdk build

# 3. 将编译产物复制到本目录
cp build/z_udp_server/libz_udp_server.so \
   components/z_udp_server/prebuilt/libz_udp_server.so

# 4. 闭源：移走或删除 src/z_udp_server.cpp
mv components/z_udp_server/src/z_udp_server.cpp \
   /safe/backup/z_udp_server.cpp

# 5. 重新编译，CMake 自动切换到 prebuilt 模式
maixcdk build
```

---

## 目录说明

| 文件 | 说明 |
|------|------|
| `libz_udp_server.so` | 针对目标平台（如 maixcam arm）的预编译共享库 |

> ⚠️ `.so` 文件与编译平台强相关，请确保使用与目标设备匹配的交叉编译产物。
> 本目录下的 `.so` 不应提交到公开代码仓库（建议在 `.gitignore` 中排除）。
