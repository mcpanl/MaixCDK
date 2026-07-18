set(middleware_src_path "${CMAKE_CURRENT_SOURCE_DIR}/cvi_mpi_sophgo")

# middleware
set(middleware_include_dir .
        ${middleware_src_path}/component/panel/cv181x
        ${middleware_src_path}/component/isp/common
        ${middleware_src_path}/include
        ${middleware_src_path}/include/isp/cv181x
        ${middleware_src_path}/sample/common
        ${middleware_src_path}/3rdparty/inih
        ${middleware_src_path}/modules/ive/include/
        ${middleware_src_path}/modules/audio/include/
#        ${middleware_src_path}/sample/z_mmf/
)
list(APPEND ADD_INCLUDE ${middleware_include_dir})
set_property(SOURCE ${middleware_include_dir} PROPERTY GENERATED 1)

append_srcs_dir(middleware_src_dir  ${middleware_src_path}/sample/common
        ${middleware_src_path}/component/isp/sensor/cv181x/gcore_gc4653
        ${middleware_src_path}/component/isp/sensor/cv181x/gcore_gc2083
)
list(APPEND ADD_SRCS        ${middleware_src_dir})
set_property(SOURCE ${middleware_src_dir} PROPERTY GENERATED 1)
# Prebuilt libisp.so lacks CVI_MIPI_Set_SnsI2cInfo; provide compat shim
list(APPEND ADD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/cvi_mipi_sns_i2c_compat.c")

# Arch-aware lib dir: prefer lib/<arch> when it actually contains libs
# (empty placeholder dirs with only README must not win over MaixArm64Lib)
if(MAIX_ARCH STREQUAL "arm64")
    set(_repo_cvi "${SDK_PATH}/../MaixArm64Lib/arm64-glibc/cvi_mpi/lib")
    if(EXISTS "${middleware_src_path}/lib/arm64/libvi.so")
        set(mmf_lib_dir ${middleware_src_path}/lib/arm64)
    elseif(EXISTS "${_repo_cvi}/libvi.so")
        set(mmf_lib_dir ${_repo_cvi})
        message(STATUS "cvi-middleware: using MaixArm64Lib arm64 cvi_mpi lib")
    else()
        message(FATAL_ERROR "cvi-middleware: arm64 libs missing. Place .so under ${middleware_src_path}/lib/arm64 or sync MaixArm64Lib (${_repo_cvi})")
    endif()
elseif(EXISTS "${middleware_src_path}/lib/riscv64/libvi.so")
    set(mmf_lib_dir ${middleware_src_path}/lib/riscv64)
elseif(EXISTS "${middleware_src_path}/lib/libvi.so")
    set(mmf_lib_dir ${middleware_src_path}/lib)
else()
    set(mmf_lib_dir ${middleware_src_path}/lib)
endif()
message(STATUS "cvi-middleware mmf_lib_dir=${mmf_lib_dir} (MAIX_ARCH=${MAIX_ARCH})")
set(middleware_dynamic_lib_file
        ${mmf_lib_dir}/libcvi_ive.so
        ${mmf_lib_dir}/libcvi_bin.so
        ${mmf_lib_dir}/libaaccomm2.so
        ${mmf_lib_dir}/libaacdec2.so
        ${mmf_lib_dir}/libaacenc2.so
        ${mmf_lib_dir}/libaacsbrdec2.so
        ${mmf_lib_dir}/libaacsbrenc2.so
        ${mmf_lib_dir}/libae.so
        ${mmf_lib_dir}/libaf.so
        ${mmf_lib_dir}/libawb.so
        ${mmf_lib_dir}/libcvi_audio.so
        ${mmf_lib_dir}/libcvi_bin_isp.so
        ${mmf_lib_dir}/libcvi_bin.so
        ${mmf_lib_dir}/libgdc.so
        ${mmf_lib_dir}/libcvi_ispd2.so
        ${mmf_lib_dir}/libcvi_RES1.so
        ${mmf_lib_dir}/libcvi_ssp.so
        ${mmf_lib_dir}/libcvi_ssp2.so
        ${mmf_lib_dir}/libcvi_VoiceEngine.so
        ${mmf_lib_dir}/libtinyalsa.so
        ${mmf_lib_dir}/libcvi_vqe.so
        ${mmf_lib_dir}/libcvi_dnvqe.so
        ${mmf_lib_dir}/libcvi_audio.so

#        ${mmf_lib_dir}/libdnvqe.so
        ${mmf_lib_dir}/libisp_algo.so
        ${mmf_lib_dir}/libisp.so
        ${mmf_lib_dir}/libmipi_tx.so
        ${mmf_lib_dir}/libmisc.so
#        ${mmf_lib_dir}/libosdc.so
        ${mmf_lib_dir}/libraw_dump.so
        ${mmf_lib_dir}/libsys.so
        ${mmf_lib_dir}/libvdec.so
        ${mmf_lib_dir}/libvenc.so
#        ${mmf_lib_dir}/libvpu.so
#        ${mmf_lib_dir}/libjson-c.so.5
#        ${mmf_lib_dir}/3rd/libcli.so
        ${mmf_lib_dir}/3rd/libini.so
        ${mmf_lib_dir}/3rd/libsbc.so
        ${mmf_lib_dir}/libvi.so
        ${mmf_lib_dir}/libvo.so
        ${mmf_lib_dir}/libvpss.so
        ${mmf_lib_dir}/librgn.so
        ${mmf_lib_dir}/libsns_full.so
)
# zonhor / IMX678 sensor driver (present in MaixArm64Lib arm64 cvi_mpi)
if(EXISTS "${mmf_lib_dir}/libsns_imx678.so")
    list(APPEND middleware_dynamic_lib_file ${mmf_lib_dir}/libsns_imx678.so)
endif()

#set(middleware_dynamic_lib_file     ${mmf_lib_dir}/libcvi_ive.so
#        ${mmf_lib_dir}/libcvi_bin.so
#        ${mmf_lib_dir}/libaaccomm2.so
#        ${mmf_lib_dir}/libaacdec2.so
#        ${mmf_lib_dir}/libaacenc2.so
#        ${mmf_lib_dir}/libaacsbrdec2.so
#        ${mmf_lib_dir}/libaacsbrenc2.so
#        ${mmf_lib_dir}/libae.so
#        ${mmf_lib_dir}/libaf.so
#        ${mmf_lib_dir}/libawb.so
#        ${mmf_lib_dir}/libcvi_audio.so
#        ${mmf_lib_dir}/libcvi_bin_isp.so
#        ${mmf_lib_dir}/libcvi_bin.so
#        ${mmf_lib_dir}/libcvi_ispd2.so
#        ${mmf_lib_dir}/libcvi_RES1.so
#        ${mmf_lib_dir}/libcvi_ssp.so
#        ${mmf_lib_dir}/libcvi_VoiceEngine.so
#        ${mmf_lib_dir}/libcvi_vqe.so
#        ${mmf_lib_dir}/libdnvqe.so
#        ${mmf_lib_dir}/libisp_algo.so
#        ${mmf_lib_dir}/libisp.so
#        ${mmf_lib_dir}/libmipi_tx.so
#        ${mmf_lib_dir}/libmisc.so
#        ${mmf_lib_dir}/libosdc.so
#        ${mmf_lib_dir}/libraw_dump.so
#        ${mmf_lib_dir}/libsys.so
#        ${mmf_lib_dir}/libvdec.so
#        ${mmf_lib_dir}/libvenc.so
#        ${mmf_lib_dir}/libvpu.so
#        ${mmf_lib_dir}/libjson-c.so.5
#        ${mmf_lib_dir}/libtinyalsa.so
#        ${mmf_lib_dir}/3rd/libcli.so
#        ${mmf_lib_dir}/3rd/libini.so)
list(APPEND ADD_DYNAMIC_LIB ${middleware_dynamic_lib_file})
set_property(SOURCE ${middleware_dynamic_lib_file} PROPERTY GENERATED 1)
# libvpss (linaro7.3 / glibc 2.25) needs timer_* from librt
list(APPEND ADD_REQUIREMENTS rt)

list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_GCORE_GC4653
        -DSENSOR0_TYPE=SONY_IMX327_MIPI_2M_30FPS_12BIT
        -DSENSOR1_TYPE=SONY_IMX327_MIPI_2M_30FPS_12BIT)

list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_GCORE_GC2083)

# Zonhor IMX678: sensor_list + sample_common_sensor map to stSnsImx678_Obj (libsns_imx678.so)
if(EXISTS "${mmf_lib_dir}/libsns_imx678.so")
    list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_SONY_IMX678)
    message(STATUS "cvi-middleware: enabling SENSOR_SONY_IMX678")
endif()

#        -DSENSOR0_TYPE=SONY_IMX327_MIPI_2M_30FPS_12BIT
#        -DSENSOR1_TYPE=SONY_IMX327_MIPI_2M_30FPS_12BIT)

#list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_SMS_SC035GS
#        -DSENSOR2_TYPE=SMS_SC035GS_MIPI_480P_120FPS_12BIT)

#list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_OV_OV2685)
#list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_LONTIUM_LT6911)
#list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_OV_OS04A10)
#list(APPEND ADD_DEFINITIONS_PRIVATE -DSENSOR_GCORE_GC02M1)
register_component(DYNAMIC)