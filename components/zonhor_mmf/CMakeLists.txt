# Zonhor-only IMX678 MMF (sample_sensor_lcd topology + Camera RGB channel).
# Not built / registered on non-zonhor platforms.

list(APPEND ADD_INCLUDE "include")

if(PLATFORM_ZONHOR)
    message(STATUS "[zonhor_mmf] building for PLATFORM_ZONHOR MAIX_ARCH=${MAIX_ARCH}")

    list(APPEND ADD_REQUIREMENTS basic cvi-middleware)
    list(APPEND ADD_SRCS
        "src/zonhor_mmf.c"
        "src/zonhor_fb_lcd.c"
        "src/zonhor_media_runtime.cpp"
    )
    list(APPEND ADD_DEFINITIONS_PRIVATE -Wl,--exclude-libs,ALL)
    # cvi_audio (via middleware) needs libdl
    list(APPEND ADD_REQUIREMENTS dl)

    register_component()
else()
    message(STATUS "[zonhor_mmf] skipped (not PLATFORM_ZONHOR)")
endif()
