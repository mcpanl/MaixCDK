list(APPEND ADD_INCLUDE "include")
append_srcs_dir(ADD_SRCS "src")

list(APPEND ADD_REQUIREMENTS basic vision ini rknn_runtime)

register_component()
