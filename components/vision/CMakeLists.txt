# Config enable component2 or not in Kconfig
################# Add include #################
list(APPEND ADD_INCLUDE "include" "include/base"
)
# Internal headers (maix_image_util.hpp, etc.) used by vision/src/*.cpp
list(APPEND ADD_PRIVATE_INCLUDE "include_private")
if(PLATFORM_LINUX)
    list(APPEND ADD_PRIVATE_INCLUDE "port/linux")
endif()
if(PLATFORM_RK3566)
    list(APPEND ADD_PRIVATE_INCLUDE "port/rk3566")
    append_srcs_dir(ADD_SRCS "port/rk3566")
    list(APPEND ADD_REQUIREMENTS rk_lib)
endif()

# MaixCam backend selector — default is z_lib (existing, stable).
# Set MAIXCAM_VISION_BACKEND=x_mmf to use the new x_lib-based port.
if(PLATFORM_MAIXCAM)
    set(MAIXCAM_VISION_BACKEND "x_mmf" CACHE STRING
        "MaixCam vision backend: z_lib (default, stable) or x_mmf (new, x_lib-based)")
    set_property(CACHE MAIXCAM_VISION_BACKEND PROPERTY STRINGS "z_lib" "x_mmf")

    if(MAIXCAM_VISION_BACKEND STREQUAL "x_mmf")
        set(MAIXCAM_VISION_USE_X_MMF 1)
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam_xmmf")
        # z_touchscreen_maixcam.hpp 等仍只在 port/maixcam；x_mmf 不编译该目录 .cpp，仅保留头路径
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam")
        list(APPEND ADD_REQUIREMENTS x_lib)
        message(STATUS "[vision] MaixCam backend: x_mmf")
    else()
        set(MAIXCAM_VISION_USE_X_MMF 0)
        list(APPEND ADD_PRIVATE_INCLUDE "port/maixcam")
        list(APPEND ADD_REQUIREMENTS z_lib)
        message(STATUS "[vision] MaixCam backend: z_lib (default)")
    endif()
endif()

# Same value as generated vision_maixcam_config.h — PUBLIC so user projects that
# `ADD_REQUIREMENTS vision` can `#if MAIXCAM_VISION_USE_X_MMF` without extra include paths.
if(PLATFORM_MAIXCAM)
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_USE_X_MMF=${MAIXCAM_VISION_USE_X_MMF})
    message(STATUS "[vision] MAIXCAM_VISION_BACKEND=${MAIXCAM_VISION_BACKEND} -> MAIXCAM_VISION_USE_X_MMF=${MAIXCAM_VISION_USE_X_MMF} (PUBLIC to dependents)")
else()
    list(APPEND ADD_DEFINITIONS -DMAIXCAM_VISION_USE_X_MMF=0)
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
    if(MAIXCAM_VISION_BACKEND STREQUAL "x_mmf")
        append_srcs_dir(ADD_SRCS "port/maixcam_xmmf")
    else()
        append_srcs_dir(ADD_SRCS "port/maixcam")
    endif()
endif()
# list(REMOVE_ITEM COMPONENT_SRCS "src/test.c")
# set(ADD_ASM_SRCS "src/asm.S")
# list(APPEND ADD_SRCS ${ADD_ASM_SRCS})
# SET_PROPERTY(SOURCE ${ADD_ASM_SRCS} PROPERTY LANGUAGE C) # set .S  ASM file as C language
# SET_SOURCE_FILES_PROPERTIES(${ADD_ASM_SRCS} PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp -D BBBBB")
###############################################

list(APPEND ADD_REQUIREMENTS x_lib basic opencv opencv_freetype peripheral omv zbar)
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

