
# if include exist, then add it to ADD_INCLUDE for local debug
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/cvi_tpu_lib)
    list(APPEND ADD_INCLUDE "cvi_tpu_lib/include")
    # Prefer MAIX_ARCH; fall back to toolchain path heuristic
    if(MAIX_ARCH STREQUAL "arm64" OR (NOT MAIX_ARCH STREQUAL "riscv64" AND NOT ${CONFIG_TOOLCHAIN_PATH} MATCHES "musl"))
        message(STATUS "cvi_tpu lib use glibc (MAIX_ARCH=${MAIX_ARCH})")
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/cvi_tpu_lib/lib_glibc")
            list(APPEND ADD_STATIC_LIB "cvi_tpu_lib/lib_glibc/libcvimath-static.a"
                                    "cvi_tpu_lib/lib_glibc/libcviruntime-static.a"
                                    "cvi_tpu_lib/lib_glibc/libcvikernel-static.a"
            )
        elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/lib/arm64")
            list(APPEND ADD_STATIC_LIB "lib/arm64/libcvimath-static.a"
                                    "lib/arm64/libcviruntime-static.a"
                                    "lib/arm64/libcvikernel-static.a"
            )
        endif()
    else()
        message(STATUS "cvi_tpu lib use musl (MAIX_ARCH=${MAIX_ARCH})")
        list(APPEND ADD_STATIC_LIB  "cvi_tpu_lib/lib_musl/libcvimath-static.a"
                                    "cvi_tpu_lib/lib_musl/libcviruntime-static.a"
                                    "cvi_tpu_lib/lib_musl/libcvikernel-static.a"
            )
    endif()
elseif(PLATFORM_MAIXCAM AND NOT CONFIG_COMPONENTS_COMPILE_FROM_SOURCE)
    if(MAIX_ARCH STREQUAL "arm64")
        # Prefer synced MaixArm64Lib cache or local lib/arm64 (must contain static libs)
        set(_repo_root "${SDK_PATH}/..")
        set(_arm64_tpu "${_repo_root}/MaixArm64Lib/arm64-glibc/tpu_sdk")
        set(cvi_tpu_lib_dir "")
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/lib/arm64/libcvimath-static.a")
            set(cvi_tpu_lib_dir "${CMAKE_CURRENT_LIST_DIR}/lib/arm64")
        elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/lib/arm64/lib/libcvimath-static.a")
            set(cvi_tpu_lib_dir "${CMAKE_CURRENT_LIST_DIR}/lib/arm64")
        elseif(EXISTS "${_arm64_tpu}")
            set(cvi_tpu_lib_dir "${_arm64_tpu}")
        else()
            message(WARNING "cvi_tpu: arm64 libs not found; place under lib/arm64 or sync MaixArm64Lib")
        endif()
        if(cvi_tpu_lib_dir)
            if(EXISTS "${cvi_tpu_lib_dir}/include")
                list(APPEND ADD_INCLUDE "${cvi_tpu_lib_dir}/include")
                set_property(SOURCE "${cvi_tpu_lib_dir}/include" PROPERTY GENERATED 1)
            endif()
            # Try common layout variants
            foreach(_cand
                    "${cvi_tpu_lib_dir}/lib_glibc"
                    "${cvi_tpu_lib_dir}/lib"
                    "${cvi_tpu_lib_dir}")
                if(EXISTS "${_cand}/libcvimath-static.a")
                    set(harfbuzz_lib_file "${_cand}/libcvimath-static.a"
                                          "${_cand}/libcviruntime-static.a"
                                          "${_cand}/libcvikernel-static.a")
                    break()
                endif()
            endforeach()
            if(harfbuzz_lib_file)
                set_property(SOURCE ${harfbuzz_lib_file} PROPERTY GENERATED 1)
                list(APPEND ADD_STATIC_LIB ${harfbuzz_lib_file})
            endif()
        endif()
    elseif(CONFIG_TOOLCHAIN_PATH MATCHES "musl" OR CONFIG_TOOLCHAIN_PATH MATCHES "glibc" OR MAIX_ARCH STREQUAL "riscv64")
        set(cvi_tpu_lib_parent_dir "${DL_EXTRACTED_PATH}/cvi_tpu")
        file(GLOB ALL_SUBDIRS LIST_DIRECTORIES true "${cvi_tpu_lib_parent_dir}/*")
        if(ALL_SUBDIRS)
            list(GET ALL_SUBDIRS 0 FIRST_SUBDIR)
            get_filename_component(FIRST_SUBDIR_NAME "${FIRST_SUBDIR}" NAME)
            set(cvi_tpu_lib_dir "${cvi_tpu_lib_parent_dir}/${FIRST_SUBDIR_NAME}")
        else()
            message(WARNING "No subdirectories found in ${cvi_tpu_lib_parent_dir}")
        endif()
        set(harfbuzz_include_dir "${cvi_tpu_lib_dir}/include")
        set_property(SOURCE ${harfbuzz_include_dir} PROPERTY GENERATED 1)
        if(NOT cvi_tpu_lib_dir)
            message(FATAL_ERROR "cvi_tpu not support this toolchain")
        endif()
        if(CONFIG_TOOLCHAIN_PATH MATCHES "musl" OR MAIX_ARCH STREQUAL "riscv64")
            set(harfbuzz_lib_file "${cvi_tpu_lib_dir}/lib_musl/libcvimath-static.a"
                                  "${cvi_tpu_lib_dir}/lib_musl/libcviruntime-static.a"
                                  "${cvi_tpu_lib_dir}/lib_musl/libcvikernel-static.a"
            )
        elseif(CONFIG_TOOLCHAIN_PATH MATCHES "glibc")
            set(harfbuzz_lib_file "${cvi_tpu_lib_dir}/lib_glibc/libcvimath-static.a"
                                "${cvi_tpu_lib_dir}/lib_glibc/libcviruntime-static.a"
                                "${cvi_tpu_lib_dir}/lib_glibc/libcvikernel-static.a"
            )
        endif()
        if(harfbuzz_lib_file)
            set_property(SOURCE ${harfbuzz_lib_file} PROPERTY GENERATED 1)
            list(APPEND ADD_INCLUDE ${harfbuzz_include_dir})
            list(APPEND ADD_STATIC_LIB ${harfbuzz_lib_file})
        endif()
    endif()
endif()
if(PLATFORM_MAIXCAM AND NOT ADD_INCLUDE)
    message(FATAL_ERROR "cvi_tpu_lib not found please check (MAIX_ARCH=${MAIX_ARCH})")
endif()

register_component()
