############### Add include ###################
list(APPEND ADD_INCLUDE "include")
# 组件注册名为 vision（非 z_vision）；显式加入头路径，避免依赖解析未传播 PUBLIC include
list(APPEND ADD_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../../components/vision/include")
list(APPEND ADD_PRIVATE_INCLUDE "")
###############################################

############ Add source files #################
append_srcs_dir(ADD_SRCS "src")
###############################################

###### Add required/dependent components ######
# 与 z_vio_demo 一致：前段 Display/Camera 依赖 z_lib；vision 已依赖 x_lib
list(APPEND ADD_REQUIREMENTS basic peripheral z_lib vision x_lib atomic)
###############################################

# register component, DYNAMIC or SHARED flags will make component compiled to dynamic(shared) lib
register_component()

