set(TDL_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/tdl_sdk_milkv)
set(MPI_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../cvi-middleware/cvi_mpi_sophgo)

# Arch-specific TDL lib roots (riscv64 default layout; arm64 under lib/arm64 or MaixArm64Lib)
# Require a real .so so empty placeholder lib/arm64/ (README only) does not win.
if(MAIX_ARCH STREQUAL "arm64")
    set(_repo_tdl "${SDK_PATH}/../MaixArm64Lib/arm64-glibc/tdl_sdk/lib")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/arm64/libcvi_tdl.so")
        set(TDL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/arm64")
    elseif(EXISTS "${_repo_tdl}/libcvi_tdl.so")
        set(TDL_LIB_DIR "${_repo_tdl}")
        message(STATUS "cvi-tdl-sdk: using MaixArm64Lib arm64 tdl_sdk lib")
    else()
        set(TDL_LIB_DIR "${TDL_SDK_ROOT}/lib")
        message(WARNING "cvi-tdl-sdk: arm64 libs not found under lib/arm64 or MaixArm64Lib; falling back to ${TDL_LIB_DIR}")
    endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/riscv64/libcvi_tdl.so")
    set(TDL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/riscv64")
else()
    set(TDL_LIB_DIR "${TDL_SDK_ROOT}/lib")
endif()
message(STATUS "cvi-tdl-sdk TDL_LIB_DIR=${TDL_LIB_DIR} (MAIX_ARCH=${MAIX_ARCH})")

list(APPEND ADD_INCLUDE
        ${TDL_SDK_ROOT}/include
        ${TDL_SDK_ROOT}/include/cvi_tdl
        ${TDL_SDK_ROOT}/sample/3rd/opencv/include
        ${MPI_SDK_ROOT}/include
        ${MPI_SDK_ROOT}/include/linux
)

list(APPEND ADD_REQUIREMENTS basic opencv)

list(APPEND ADD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/dummy.cpp
)

list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvikernel.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcviruntime.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvimath.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvi_draw_rect.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvi_md.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvi_kit.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvi_preprocess.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvi_tdl.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_LIB_DIR}/libcvi_tdl_app.so")

register_component(DYNAMIC)
