# Config enable component2 or not in Kconfig
################# Add include #################
list(APPEND ADD_INCLUDE "include" "include/base"
)
# Internal headers (maix_image_util.hpp, etc.) used by vision/src/*.cpp
list(APPEND ADD_PRIVATE_INCLUDE "include_private")
if(PLATFORM_LINUX)
    list(APPEND ADD_PRIVATE_INCLUDE "port/linux")
    list(APPEND ADD_PRIVATE_INCLUDE "port/linux_common")
endif()
if(PLATFORM_MAIXCAM)
    list(APPEND ADD_PRIVATE_INCLUDE "port/linux_common")
endif()
if(PLATFORM_RK3566)
    list(APPEND ADD_PRIVATE_INCLUDE "port/linux_common")
endif()
if(PLATFORM_RK3566)
    list(APPEND ADD_PRIVATE_INCLUDE "port/rk3566")
    append_srcs_dir(ADD_SRCS "port/rk3566")
    list(APPEND ADD_REQUIREMENTS rk_lib)
endif()

# MaixCam / Zonhor backend selector.
# - maixcam: default x_mmf (except fb_display_demo → fb)
# - zonhor: default zonhor_mmf (IMX678 sample_sensor_lcd topology); Display → /dev/fb0
# Set MAIXCAM_VISION_BACKEND=zonhor_mmf|x_mmf|z_lib|fb to override.
# Note: PLATFORM_ZONHOR also sets PLATFORM_MAIXCAM=1 in the build system.
if(PLATFORM_MAIXCAM)
    if(PROJECT_ID STREQUAL "fb_display_demo")
        set(_vision_backend_default "fb")
    elseif(PLATFORM_ZONHOR)
        set(_vision_backend_default "zonhor_mmf")
    else()
        set(_vision_backend_default "x_mmf")
    endif()
    set(MAIXCAM_VISION_BACKEND "${_vision_backend_default}" CACHE STRING
        "MaixCam/Zonhor vision backend: zonhor_mmf, x_mmf, z_lib, or fb")
    set_property(CACHE MAIXCAM_VISION_BACKEND PROPERTY STRINGS "zonhor_mmf" "z_lib" "x_mmf" "fb")

    set(MAIXCAM_VISION_USE_X_MMF 0)
    set(MAIXCAM_VISION_USE_ZONHOR_MMF 0)
    set(MAIXCAM_VISION_FB_ONLY 0)

    if(MAIXCAM_VISION_BACKEND STREQUAL "zonhor_mmf")
        set(MAIXCAM_VISION_USE_ZONHOR_MMF 1)
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam_zonhor")
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam")
        list(APPEND ADD_REQUIREMENTS zonhor_mmf)
        message(STATUS "[vision] Zonhor backend: zonhor_mmf (camera) + FB display default")
    elseif(MAIXCAM_VISION_BACKEND STREQUAL "x_mmf")
        set(MAIXCAM_VISION_USE_X_MMF 1)
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam_xmmf")
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam")
        list(APPEND ADD_REQUIREMENTS x_lib)
        if(PLATFORM_ZONHOR)
            message(STATUS "[vision] Zonhor backend: x_mmf (camera) + FB display default")
        else()
            message(STATUS "[vision] MaixCam backend: x_mmf")
        endif()
    elseif(MAIXCAM_VISION_BACKEND STREQUAL "fb")
        set(MAIXCAM_VISION_FB_ONLY 1)
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam")
        message(STATUS "[vision] MaixCam backend: fb (framebuffer only)")
    else()
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam")
        list(APPEND ADD_REQUIREMENTS z_lib)
        message(STATUS "[vision] MaixCam backend: z_lib (default)")
    endif()
endif()

# Same value as generated vision_maixcam_config.h — PUBLIC so user projects that
# `ADD_REQUIREMENTS vision` can `#if MAIXCAM_VISION_USE_X_MMF` without extra include paths.
if(PLATFORM_MAIXCAM)
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_USE_X_MMF=${MAIXCAM_VISION_USE_X_MMF})
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_USE_ZONHOR_MMF=${MAIXCAM_VISION_USE_ZONHOR_MMF})
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_FB_ONLY=${MAIXCAM_VISION_FB_ONLY})
    message(STATUS "[vision] MAIXCAM_VISION_BACKEND=${MAIXCAM_VISION_BACKEND} -> X_MMF=${MAIXCAM_VISION_USE_X_MMF} ZONHOR_MMF=${MAIXCAM_VISION_USE_ZONHOR_MMF} FB_ONLY=${MAIXCAM_VISION_FB_ONLY}")
else()
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_USE_X_MMF=0)
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_USE_ZONHOR_MMF=0)
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_FB_ONLY=0)
endif()

# Generate the compile-time config header so source files can #if MAIXCAM_VISION_USE_X_MMF
set(_vision_gen_dir "${CMAKE_CURRENT_BINARY_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include_private/vision_maixcam_config.h.in"
    "${_vision_gen_dir}/vision_maixcam_config.h"
    @ONLY
)
list(APPEND ADD_PRIVATE_INCLUDE "${_vision_gen_dir}")
###############################################

############## Add source files ###############
# list(APPEND ADD_SRCS  "src/lib2.c"
#     )
# FILE(GLOB_RECURSE EXTRA_SRC  "src/*.c")
# FILE(GLOB EXTRA_SRC  "src/*.c")
# list(APPEND ADD_SRCS  ${EXTRA_SRC})
# aux_source_directory(src ADD_SRCS)  # collect all source file in src dir, will set var ADD_SRCS
append_srcs_dir(ADD_SRCS "src")     # append source file in src dir to var ADD_SRCS
if(PLATFORM_LINUX)
    append_srcs_dir(ADD_SRCS "port/linux")
elseif(PLATFORM_MAIXCAM)
    if(NOT MAIXCAM_VISION_BACKEND STREQUAL "fb")
        if(MAIXCAM_VISION_BACKEND STREQUAL "zonhor_mmf")
            append_srcs_dir(ADD_SRCS "port/maixcam_zonhor")
        elseif(MAIXCAM_VISION_BACKEND STREQUAL "x_mmf")
            append_srcs_dir(ADD_SRCS "port/maixcam_xmmf")
            maix_append_arch_port_srcs(ADD_SRCS "port/maixcam_xmmf")
        else()
            append_srcs_dir(ADD_SRCS "port/maixcam")
            maix_append_arch_port_srcs(ADD_SRCS "port/maixcam")
        endif()
    endif()
endif()
# list(REMOVE_ITEM COMPONENT_SRCS "src/test.c")
# set(ADD_ASM_SRCS "src/asm.S")
# list(APPEND ADD_SRCS ${ADD_ASM_SRCS})
# SET_PROPERTY(SOURCE ${ADD_ASM_SRCS} PROPERTY LANGUAGE C) # set .S  ASM file as C language
# SET_SOURCE_FILES_PROPERTIES(${ADD_ASM_SRCS} PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp -D BBBBB")
###############################################

list(APPEND ADD_REQUIREMENTS basic opencv opencv_freetype peripheral omv zbar FFmpeg voice)

	# z_video_zonhor.cpp 依赖 sophgo_middleware.hpp 的声明（mmf_*, PAYLOAD_TYPE_E, PIXEL_*）。
	# maixcam_lib 在 arm64 下可能缺失预置 so，因此不要把它加进 ADD_REQUIREMENTS；
	# 仅提供 include path 让编译通过。
	list(APPEND ADD_PRIVATE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../maixcam_lib/include")
###### Add required/dependent components ######
###############################################

###### Add link search path for requirements/libs ######
# list(APPEND ADD_LINK_SEARCH_PATH "${CONFIG_TOOLCHAIN_PATH}/lib")
# list(APPEND ADD_REQUIREMENTS pthread m)  # add system libs, pthread and math lib for example here
# set (OpenCV_DIR opencv/lib/cmake/opencv4)
# find_package(OpenCV REQUIRED)
###############################################

############ Add static libs ##################
# list(APPEND ADD_STATIC_LIB "lib/libtest.a")
###############################################

############ Add dynamic libs ##################
# list(APPEND ADD_DYNAMIC_LIB "lib/arch/v831/libmaix_nn.so"
#                             "lib/arch/v831/libmaix_cam.so"
# )
###############################################

#### Add compile option for this component ####
#### Just for this component, won't affect other 
#### modules, including component that depend 
#### on this component
# list(APPEND ADD_DEFINITIONS_PRIVATE -DAAAAA=1)

#### Add compile option for this component
#### Add components that depend on this component
# list(APPEND ADD_DEFINITIONS -DAAAAA222=1
#                             -DAAAAA333=1)
###############################################

############ Add static libs ##################
#### Update parent's variables like CMAKE_C_LINK_FLAGS
# set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group libmaix/libtest.a -ltest2 -Wl,--end-group" PARENT_SCOPE)
###############################################

# register component, DYNAMIC or SHARED flags will make component compiled to dynamic(shared) lib
register_component()

