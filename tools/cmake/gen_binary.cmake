
# set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <OBJECTS> -o <TARGET>.elf <LINK_LIBRARIES>")
# set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <OBJECTS> -o <TARGET>.elf <LINK_LIBRARIES>")

# add_custom_command(TARGET ${PROJECT_ID} POST_BUILD
#     COMMAND ${CMAKE_OBJCOPY} --output-format=binary ${CMAKE_BINARY_DIR}/${PROJECT_ID}.elf ${CMAKE_BINARY_DIR}/${PROJECT_ID}.bin
#     DEPENDS ${PROJECT_ID}
#     COMMENT "-- Generating binary file ...")

# variable #{g_dynamic_libs} have dependency dynamic libs and compiled dynamic libs(register component and assigned DYNAMIC or SHARED flag)

string(TOLOWER ${BUILD_TYPE} build_type)

# Dist folder: include arch suffix when MAIX_ARCH is riscv64/arm64
if(DEFINED DIST_NAME AND NOT DIST_NAME STREQUAL "")
    set(_dist_subdir ${DIST_NAME})
elseif(MAIX_ARCH STREQUAL "riscv64" OR MAIX_ARCH STREQUAL "arm64")
    set(_dist_subdir ${PROJECT_ID}_${build_type}_${MAIX_ARCH})
else()
    set(_dist_subdir ${PROJECT_ID}_${build_type})
endif()

set(cp_dist_cmd COMMAND mkdir -p ${PROJECT_DIST_DIR}/${_dist_subdir} && cp ${CMAKE_BINARY_DIR}/${PROJECT_ID} ${PROJECT_DIST_DIR}/${_dist_subdir}/)
if(g_dynamic_libs)
    set(cp_command COMMAND cp ${g_dynamic_libs} ${CMAKE_BINARY_DIR}/dl_lib/)
    set(cp_dl_to_dist_cmd COMMAND mkdir -p ${PROJECT_DIST_DIR}/${_dist_subdir}/dl_lib && cp ${g_dynamic_libs} ${PROJECT_DIST_DIR}/${_dist_subdir}/dl_lib)
endif()

if(${BUILD_TYPE} STREQUAL "Release")
    set(strip_cmd COMMAND ${CMAKE_STRIP} ${CMAKE_BINARY_DIR}/${PROJECT_ID})
    # add dl lib to strip_cmd
    if(g_dynamic_libs)
        set(strip_cmd COMMAND ${CMAKE_STRIP} ${CMAKE_BINARY_DIR}/${PROJECT_ID} ${g_dynamic_libs})
    endif()
endif()

set(cp_assets_cmd COMMAND python ${SDK_PATH}/tools/cmake/copy_assets.py ${PROJECT_PATH} ${PROJECT_DIST_DIR}/${_dist_subdir})

# Optional ELF arch check after link
set(elf_check_cmd "")
if(MAIX_ARCH STREQUAL "riscv64" OR MAIX_ARCH STREQUAL "arm64")
    set(elf_check_cmd COMMAND python3 ${SDK_PATH}/tools/cmake/check_elf_arch.py ${MAIX_ARCH} ${CMAKE_BINARY_DIR}/${PROJECT_ID})
endif()

add_custom_command(TARGET ${PROJECT_ID} POST_BUILD
    ${strip_cmd}
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/dl_lib
    ${cp_command}
    ${cp_dist_cmd}
    ${cp_dl_to_dist_cmd}
    ${cp_assets_cmd}
    ${elf_check_cmd}
    DEPENDS ${PROJECT_ID}
    COMMENT "-- copy dynamic libs to build/dl_lib dir ...")
