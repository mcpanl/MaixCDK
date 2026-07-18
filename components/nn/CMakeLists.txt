
list(APPEND ADD_INCLUDE "include")
#list(APPEND ADD_PRIVATE_INCLUDE "include_private")

# z_nn_test_*.hpp 的实现在 z_nn_test_*.cpp 中；默认不编进 nn 库以减小体积。
# 需要时：cmake 配置阶段传 -DMAIX_NN_BUILD_Z_NN_TEST=ON，
# 或在工程根目录 compile/compile_flags.cmake 里 set(... CACHE BOOL ... FORCE)。
option(MAIX_NN_BUILD_Z_NN_TEST "Build z_nn_test_*.cpp into nn (z_nn_test_*.hpp helpers)" OFF)

append_srcs_dir(ADD_SRCS "src")
if(NOT MAIX_NN_BUILD_Z_NN_TEST)
    list(FILTER ADD_SRCS EXCLUDE REGEX ".*/z_nn_test_[^/]+\\.cpp$|^z_nn_test_[^/]+\\.cpp$")
endif()
list(APPEND ADD_REQUIREMENTS basic ini clipper2 vision)

if(PLATFORM_MAIXCAM)
    list(APPEND ADD_REQUIREMENTS cvi-tdl-sdk cvi_tpu)
    # When x_mmf vision backend is selected, NN_MaixCam shares the MediaRuntime
    # so that SYS/VB is properly initialized even for NN-only use cases.
    if(MAIXCAM_VISION_BACKEND STREQUAL "x_mmf")
        list(APPEND ADD_REQUIREMENTS x_lib)
        list(APPEND ADD_DEFINITIONS_PRIVATE -DMAIXCAM_NN_USE_XMMF_RUNTIME=1)
    endif()
elseif(PLATFORM_RK3566)
    list(APPEND ADD_INCLUDE "port/rk3566")
    append_srcs_dir(ADD_SRCS "port/rk3566")
    list(APPEND ADD_REQUIREMENTS rknn_runtime)
else()
endif()

register_component()

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

