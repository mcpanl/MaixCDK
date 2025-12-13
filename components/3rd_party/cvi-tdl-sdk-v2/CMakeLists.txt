set(TDL_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/CV181X)
set(MPI_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../cvi-middleware/cvi_mpi)

list(APPEND ADD_INCLUDE
        ${TDL_SDK_ROOT}/include
        ${TDL_SDK_ROOT}/include/c_apis
        ${MPI_SDK_ROOT}/include
        ${MPI_SDK_ROOT}/include/linux
)

list(APPEND ADD_REQUIREMENTS basic z_vision)

list(APPEND ADD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/dummy.cpp
)



#list(APPEND ADD_STATIC_LIB "${TDL_SDK_ROOT}/lib/libcvimath-static.a")
#list(APPEND ADD_STATIC_LIB "${TDL_SDK_ROOT}/lib/libcvikernel-static.a")
#list(APPEND ADD_STATIC_LIB "${TDL_SDK_ROOT}/lib/libcviruntime-static.a")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libz.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvimath.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcviruntime.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcvikernel.so")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libcnpy.so")
#list(APPEND ADD_STATIC_LIB "${TDL_SDK_ROOT}/lib/libtdl_core-static.a")
list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libtdl_core.so")

#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libopencv_core.so.4.5")
#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libopencv_imgproc.so.4.5")
#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libopencv_imgcodecs.so.4.5")

#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libtdl_ex.so")

#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_imgcodecs.so")
#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_imgproc.so")
#list(APPEND ADD_DYNAMIC_LIB "${TDL_SDK_ROOT}/lib/libaaa_core.so")
#
#
#
#
#
#
#
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvikernel.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcviruntime.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvimath.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvi_draw_rect.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvi_md.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvi_kit.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvi_preprocess.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvi_tdl.so")
#list(APPEND ADD_DYNAMIC_LIB "tdl_sdk/lib/libcvi_tdl_app.so")














#register_component(STATIC)
register_component(DYNAMIC)