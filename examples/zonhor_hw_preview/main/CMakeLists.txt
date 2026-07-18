############### Add include ###################
list(APPEND ADD_INCLUDE "include")
###############################################

############ Add source files #################
append_srcs_dir(ADD_SRCS "src")
###############################################

###### Add required/dependent components ######
list(APPEND ADD_REQUIREMENTS basic zonhor_mmf)
###############################################

register_component()
