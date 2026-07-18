
list(APPEND ADD_REQUIREMENTS basic ini)
list(APPEND ADD_INCLUDE "include")

message("[X_LIB]")

set(X_MMF_ENABLE_VI 1 CACHE STRING "Enable X_MMF VI module")
set(X_MMF_ENABLE_VPSS 1 CACHE STRING "Enable X_MMF VPSS module")
set(X_MMF_ENABLE_VO 1 CACHE STRING "Enable X_MMF VO module")
set(X_MMF_ENABLE_VENC 1 CACHE STRING "Enable X_MMF VENC module")
set(X_MMF_ENABLE_VDEC 1 CACHE STRING "Enable X_MMF VDEC module")
set(X_MMF_ENABLE_REGION 1 CACHE STRING "Enable X_MMF REGION module")

if(PLATFORM_MAIXCAM)
    message(STATUS "PLATFORM_MAIXCAM MAIX_ARCH=${MAIX_ARCH}")
    set(source_dir "maixcdk_x_lib/src")

    list(APPEND ADD_REQUIREMENTS cvi-middleware)

    list(APPEND ADD_SRCS "${source_dir}/x_mmf.c")

    if(X_MMF_ENABLE_VI)
        list(APPEND ADD_SRCS "${source_dir}/x_mmf_vi.c")
    endif()
    if(X_MMF_ENABLE_VPSS)
        list(APPEND ADD_SRCS "${source_dir}/x_mmf_vpss.c")
    endif()
    if(X_MMF_ENABLE_VO)
        list(APPEND ADD_SRCS "${source_dir}/x_mmf_vo.c")
    endif()
    if(X_MMF_ENABLE_VENC)
        list(APPEND ADD_SRCS "${source_dir}/x_mmf_venc.c")
    endif()
    if(X_MMF_ENABLE_VDEC)
        list(APPEND ADD_SRCS "${source_dir}/x_mmf_vdec.c")
    endif()
    if(X_MMF_ENABLE_REGION)
        list(APPEND ADD_SRCS "${source_dir}/x_mmf_region.c")
    endif()

    # C++ singleton that wraps X_MMF_Init/Deinit for vision + nn shared lifecycle
    list(APPEND ADD_SRCS "${source_dir}/maix_cvi_media_runtime.cpp")

    # Optional large arch-specific adaptations under maixcdk_x_lib/src/<arch>/
    maix_append_arch_port_srcs(ADD_SRCS "maixcdk_x_lib/src")

    list(APPEND ADD_DEFINITIONS
        -DX_MMF_ENABLE_VI=${X_MMF_ENABLE_VI}
        -DX_MMF_ENABLE_VPSS=${X_MMF_ENABLE_VPSS}
        -DX_MMF_ENABLE_VO=${X_MMF_ENABLE_VO}
        -DX_MMF_ENABLE_VENC=${X_MMF_ENABLE_VENC}
        -DX_MMF_ENABLE_VDEC=${X_MMF_ENABLE_VDEC}
        -DX_MMF_ENABLE_REGION=${X_MMF_ENABLE_REGION}
    )
    list(APPEND ADD_DEFINITIONS_PRIVATE -Wl,--exclude-libs,ALL)

    register_component()
elseif(PLATFORM_MAIXCAM2)
    list(APPEND ADD_INCLUDE "../vision/include")
    set(source_dir "maixcdk_maixcam_lib/maixcam2")

    if(CONFIG_MAIXCAM_LIB_COMPILE_FROM_SOURCE)
        append_srcs_dir(ADD_SRCS "${source_dir}")
        register_component(DYNAMIC)
    else()
        list(APPEND ADD_DYNAMIC_LIB "lib/libmaixcam2_lib.so")
        list(APPEND ADD_DIST_LIB_IGNORE "lib/libmaixcam2_lib.so")
        register_component()
    endif()
elseif(PLATFORM_RK3566)
    message(STATUS "PLATFORM_RK3566: x_lib multimedia not implemented (stub)")
    register_component()
endif()

# Config enable component2 or not in Kconfig
################# Add include #################
# list(APPEND ADD_INCLUDE "include"
#     )
# list(APPEND ADD_PRIVATE_INCLUDE "include_private")
###############################################

############## Add source files ###############
# list(APPEND ADD_SRCS  "src/lib2.c"
#     )
# FILE(GLOB_RECURSE EXTRA_SRC  "src/*.c")
# FILE(GLOB EXTRA_SRC  "src/*.c")
# list(APPEND ADD_SRCS  ${EXTRA_SRC})
# aux_source_directory(src ADD_SRCS)  # collect all source file in src dir, will set var ADD_SRCS
# append_srcs_dir(ADD_SRCS "src")     # append source file in src dir to var ADD_SRCS
# list(REMOVE_ITEM COMPONENT_SRCS "src/test.c")
# set(ADD_ASM_SRCS "src/asm.S")
# list(APPEND ADD_SRCS ${ADD_ASM_SRCS})
# SET_PROPERTY(SOURCE ${ADD_ASM_SRCS} PROPERTY LANGUAGE C) # set .S  ASM file as C language
# SET_SOURCE_FILES_PROPERTIES(${ADD_ASM_SRCS} PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp -D BBBBB")
###############################################


###### Add required/dependent components ######
# list(APPEND ADD_REQUIREMENTS basic ini)
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
# register_component()

