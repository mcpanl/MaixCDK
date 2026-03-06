############### Add include ###################
list(APPEND ADD_INCLUDE "include"
    )
list(APPEND ADD_PRIVATE_INCLUDE "")
###############################################

############ Add source files #################
# list(APPEND ADD_SRCS  "src/main.c"
#                       "src/test.c"
#     )
append_srcs_dir(ADD_SRCS "src")       # append source file in src dir to var ADD_SRCS
# z_udp_server.cpp 已迁移至 components/z_udp_server/ 组件，由该组件统一管理编译
# 无论源码是否存在，这里都强制排除，避免与组件中的版本重复编译
list(REMOVE_ITEM ADD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/z_udp_server.cpp"
                          "src/z_udp_server.cpp")
# z_video_stream.cpp 已迁移至 components/z_video_stream/ 组件，由该组件统一管理编译
list(REMOVE_ITEM ADD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/z_video_stream.cpp"
                          "src/z_video_stream.cpp")
# z_record_control.cpp 已迁移至 components/z_record_control/ 组件，由该组件统一管理编译
list(REMOVE_ITEM ADD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/z_record_control.cpp"
                          "src/z_record_control.cpp")
# list(REMOVE_ITEM COMPONENT_SRCS "src/test2.c")
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
list(APPEND ADD_REQUIREMENTS basic vision FFmpeg ext_dev network z_udp_server z_video_stream z_record_control)
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

#### Add compile option for this component ####
#### Just for this component, won't affect other 
#### modules, including component that depend 
#### on this component
# list(APPEND ADD_DEFINITIONS_PRIVATE -DAAAAA=1)

#### Add compile option for this component
#### and components depend on this component
# list(APPEND ADD_DEFINITIONS -DAAAAA222=1
#                             -DAAAAA333=1)
###############################################

############ Add static libs ##################
#### Update parent's variables like CMAKE_C_LINK_FLAGS
# set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group libmaix/libtest.a -ltest2 -Wl,--end-group" PARENT_SCOPE)
###############################################

######### Add files need to download #########
# list(APPEND ADD_FILE_DOWNLOADS "{
#     'url': 'https://*****/abcde.tar.xz',
#     'urls': [],  # backup urls, if url failed, will try urls
#     'sites': [], # download site, user can manually download file and put it into dl_path
#     'sha256sum': '',
#     'filename': 'abcde.tar.xz',
#     'path': 'toolchains/xxxxx',
#     }"
# )
#
# then extracted file in ${DL_EXTRACTED_PATH}/toolchains/xxxxx,
# you can directly use then, for example use it in add_custom_command
##############################################

add_definitions(-D__dso_handle=__cvi_dso_handle)

# register component, DYNAMIC or SHARED flags will make component compiled to dynamic(shared) lib
register_component()
