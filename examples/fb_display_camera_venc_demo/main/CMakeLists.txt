############### Add include ###################
list(APPEND ADD_INCLUDE "include")
list(APPEND ADD_PRIVATE_INCLUDE "")
###############################################

############ Add source files #################
append_srcs_dir(ADD_SRCS "src")
###############################################

###### Add required/dependent components ######
list(APPEND ADD_REQUIREMENTS basic vision zonhor_mmf FFmpeg)
###############################################

register_component()
