############################################################
# z_record_control 组件 —— 支持「有源码则编译，无源码则用预编译 .so」
#
# 目录结构：
#   components/z_record_control/
#   ├── CMakeLists.txt                 <- 本文件（无需手动改动）
#   ├── src/
#   │   └── z_record_control.cpp      <- 可选：有此文件则从源码编译生成新 .so
#   └── prebuilt/
#       └── libz_record_control.so    <- 当 src/ 中无源码时，自动使用此预编译库
#
# 头文件位于 main/include/z_record_control.hpp（由 main 组件统一对外暴露）。
#
# === 如何生成 prebuilt .so ===
#   1. 保证 src/z_record_control.cpp 存在，正常编译一次项目
#   2. 将 build/z_record_control/libz_record_control.so 复制到 prebuilt/
#   3. 删除（或移走）src/z_record_control.cpp
#   4. 重新编译，CMake 会自动切换到使用 prebuilt/libz_record_control.so
############################################################

list(APPEND ADD_REQUIREMENTS basic vision FFmpeg ext_dev network peripheral maixcam_lib z_udp_server z_video_stream)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/z_record_control.cpp")
    # ── 有源码：从源码编译，输出新的 libz_record_control.so ─────────────────
    message(STATUS "[z_record_control] ✅ 源码存在，从源码编译 libz_record_control.so")

    # 私有 include：访问 main/include/ 下的项目私有头文件
    list(APPEND ADD_PRIVATE_INCLUDE "${PROJECT_SOURCE_DIR}/main/include")

    list(APPEND ADD_SRCS "src/z_record_control.cpp")

    # 编译为动态库（DYNAMIC == SHARED），生成 libz_record_control.so
    register_component(DYNAMIC)

    # 编译完成后自动将 .so 复制并覆盖到 prebuilt/，方便后续切换为预编译模式
    add_custom_command(TARGET z_record_control POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:z_record_control>
            "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libz_record_control.so"
        COMMENT "[z_record_control] 🔄 编译完成，已将 .so 自动复制到 prebuilt/libz_record_control.so"
    )

else()
    # ── 无源码：使用预编译 .so ─────────────────────────────────────────────────
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libz_record_control.so")
        message(STATUS "[z_record_control] 📦 未找到源码，使用预编译 prebuilt/libz_record_control.so")

        list(APPEND ADD_DYNAMIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libz_record_control.so")

        register_component()
    else()
        message(FATAL_ERROR
            "[z_record_control] ❌ 既未找到源码 src/z_record_control.cpp，"
            "也未找到预编译库 prebuilt/libz_record_control.so！\n"
            "请至少提供其中一个。"
        )
    endif()
endif()
