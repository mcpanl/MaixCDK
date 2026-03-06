############################################################
# z_video_stream 组件 —— 支持「有源码则编译，无源码则用预编译 .so」
#
# 目录结构：
#   components/z_video_stream/
#   ├── CMakeLists.txt               <- 本文件（无需手动改动）
#   ├── src/
#   │   └── z_video_stream.cpp       <- 可选：有此文件则从源码编译生成新 .so
#   └── prebuilt/
#       └── libz_video_stream.so     <- 当 src/ 中无源码时，自动使用此预编译库
#
# 头文件位于 main/include/z_video_stream.hpp（由 main 组件统一对外暴露）。
#
# === 如何生成 prebuilt .so ===
#   1. 保证 src/z_video_stream.cpp 存在，正常编译一次项目
#   2. 将 build/z_video_stream/libz_video_stream.so 复制到 prebuilt/
#   3. 删除（或移走）src/z_video_stream.cpp
#   4. 重新编译，CMake 会自动切换到使用 prebuilt/libz_video_stream.so
############################################################

list(APPEND ADD_REQUIREMENTS basic vision)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/z_video_stream.cpp")
    # ── 有源码：从源码编译，输出新的 libz_video_stream.so ──────────────────
    message(STATUS "[z_video_stream] ✅ 源码存在，从源码编译 libz_video_stream.so")

    # 私有 include：访问 main/include/ 下的项目私有头文件（mmf_vi_helper.hpp 等）
    list(APPEND ADD_PRIVATE_INCLUDE "${PROJECT_SOURCE_DIR}/main/include")

    list(APPEND ADD_SRCS "src/z_video_stream.cpp")

    register_component(DYNAMIC)

    # 编译完成后自动将 .so 复制并覆盖到 prebuilt/，方便后续切换为预编译模式
    add_custom_command(TARGET z_video_stream POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:z_video_stream>
            "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libz_video_stream.so"
        COMMENT "[z_video_stream] 🔄 编译完成，已将 .so 自动复制到 prebuilt/libz_video_stream.so"
    )

else()
    # ── 无源码：使用预编译 .so ─────────────────────────────────────────────
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libz_video_stream.so")
        message(STATUS "[z_video_stream] 📦 未找到源码，使用预编译 prebuilt/libz_video_stream.so")

        list(APPEND ADD_DYNAMIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libz_video_stream.so")

        register_component()
    else()
        message(FATAL_ERROR
            "[z_video_stream] ❌ 既未找到源码 src/z_video_stream.cpp，"
            "也未找到预编译库 prebuilt/libz_video_stream.so！\n"
            "请至少提供其中一个。"
        )
    endif()
endif()
