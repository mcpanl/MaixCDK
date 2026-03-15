set(TDL_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/tdl_sdk_milkv)
set(MPI_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../cvi-middleware/cvi_mpi_sophgo)

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

# libaaa_*.so were opencv 3.2 compatibility shims; removed after upgrading to opencv 4.9
# list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_imgcodecs.so")
# list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_imgproc.so")
# list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_core.so")





list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvikernel.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcviruntime.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvimath.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvi_draw_rect.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvi_md.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvi_kit.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvi_preprocess.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvi_tdl.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvi_tdl_app.so")














register_component(DYNAMIC)