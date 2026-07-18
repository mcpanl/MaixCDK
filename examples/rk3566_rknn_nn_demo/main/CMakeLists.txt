if(NOT PLATFORM_RK3566)
    message(FATAL_ERROR "rk3566_rknn_nn_demo requires platform rk3566, e.g. maixcdk build -p rk3566")
endif()

list(APPEND ADD_INCLUDE "include")
append_srcs_dir(ADD_SRCS "src")
list(APPEND ADD_REQUIREMENTS basic nn_rk vision)

register_component()
