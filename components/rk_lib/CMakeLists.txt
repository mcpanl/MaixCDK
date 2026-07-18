list(APPEND ADD_INCLUDE "include")

if(PLATFORM_RK3566)
    if(NOT DEFINED CONFIG_RK_LIB_COMPILE_FROM_SOURCE)
        set(CONFIG_RK_LIB_COMPILE_FROM_SOURCE y)
    endif()
    if(NOT DEFINED CONFIG_RK_LIB_BUNDLED_X11)
        set(CONFIG_RK_LIB_BUNDLED_X11 y)
    endif()

    if(CONFIG_RK_LIB_COMPILE_FROM_SOURCE)
        set(RK_LIB_WITH_X11 ON CACHE BOOL
            "Build rk_lib X11 window. OFF = V4L2-only + rk_vo stubs.")

        set(_rk_x11_sources
            "${CMAKE_CURRENT_SOURCE_DIR}/maixcdk_rk_lib/src/rk_v4l2.c"
        )

        if(RK_LIB_WITH_X11)
            set(_rk_x11_inc_dirs)
            set(_rk_x11_link_libs)
            set(_rk_x11_use_bundled 0)
            if(CONFIG_RK_LIB_BUNDLED_X11)
                set(_rk_x11_use_bundled 1)
            endif()

            if(_rk_x11_use_bundled)
                list(APPEND ADD_REQUIREMENTS x11_client)
                list(APPEND _rk_x11_sources "${CMAKE_CURRENT_SOURCE_DIR}/maixcdk_rk_lib/src/rk_x11.c")
                message(STATUS "rk_lib: X11 via MaixCDK x11_client (bundled from source)")
            else()
                if(NOT CMAKE_CROSSCOMPILING)
                    find_package(X11 QUIET)
                    if(X11_FOUND)
                        set(_rk_x11_inc_dirs ${X11_INCLUDE_DIR})
                        set(_rk_x11_link_libs ${X11_LIBRARIES})
                        message(STATUS "rk_lib: X11 via find_package (native)")
                    endif()
                endif()

                if(NOT _rk_x11_link_libs)
                    set(_x11_inc_hints)
                    if(CMAKE_SYSROOT)
                        list(APPEND _x11_inc_hints "${CMAKE_SYSROOT}/usr/include")
                    endif()
                    if(DEFINED ENV{RK_SYSROOT} AND NOT "$ENV{RK_SYSROOT}" STREQUAL "")
                        list(APPEND _x11_inc_hints "$ENV{RK_SYSROOT}/usr/include")
                    endif()
                    list(APPEND _x11_inc_hints /usr/include /usr/local/include /usr/X11R6/include)

                    find_path(_rk_x11_xlib_h X11/Xlib.h PATHS ${_x11_inc_hints})
                    if(NOT _rk_x11_xlib_h)
                        find_path(_rk_x11_xlib_h X11/Xlib.h)
                    endif()

                    if(CMAKE_CROSSCOMPILING)
                        set(_x11_lib_hints)
                        if(CMAKE_SYSROOT)
                            list(APPEND _x11_lib_hints
                                "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
                                "${CMAKE_SYSROOT}/usr/lib"
                                "${CMAKE_SYSROOT}/lib"
                            )
                        endif()
                        if(DEFINED ENV{RK_SYSROOT} AND NOT "$ENV{RK_SYSROOT}" STREQUAL "")
                            list(APPEND _x11_lib_hints
                                "$ENV{RK_SYSROOT}/usr/lib/aarch64-linux-gnu"
                                "$ENV{RK_SYSROOT}/usr/lib"
                            )
                        endif()
                        list(APPEND _x11_lib_hints
                            /usr/lib/aarch64-linux-gnu
                            /usr/aarch64-linux-gnu/lib
                        )
                        find_library(_rk_x11_x11_lib X11 PATHS ${_x11_lib_hints} NO_DEFAULT_PATH)
                        if(NOT _rk_x11_x11_lib)
                            message(FATAL_ERROR
                                "rk_lib: system X11 for aarch64 not found (RK_LIB_BUNDLED_X11=n).\n"
                                "  Enable bundled X11 in menuconfig, or install target libX11 in sysroot / multiarch.\n"
                                "  Headless: RK_LIB_WITH_X11=OFF")
                        endif()
                        message(STATUS "rk_lib: system X11 (cross): ${_rk_x11_xlib_h} ${_rk_x11_x11_lib}")
                    else()
                        set(_x11_lib_hints)
                        if(CMAKE_SYSROOT)
                            list(APPEND _x11_lib_hints
                                "${CMAKE_SYSROOT}/usr/lib"
                                "${CMAKE_SYSROOT}/lib"
                                "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
                                "${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf"
                            )
                        endif()
                        if(DEFINED ENV{RK_SYSROOT} AND NOT "$ENV{RK_SYSROOT}" STREQUAL "")
                            list(APPEND _x11_lib_hints
                                "$ENV{RK_SYSROOT}/usr/lib/aarch64-linux-gnu"
                                "$ENV{RK_SYSROOT}/usr/lib/arm-linux-gnueabihf"
                                "$ENV{RK_SYSROOT}/usr/lib"
                            )
                        endif()
                        if(CMAKE_SYSTEM_PROCESSOR)
                            list(APPEND _x11_lib_hints "/usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu")
                        endif()
                        list(APPEND _x11_lib_hints /usr/lib /usr/local/lib /usr/X11R6/lib)

                        find_library(_rk_x11_x11_lib X11 PATHS ${_x11_lib_hints})
                        if(NOT _rk_x11_x11_lib)
                            find_library(_rk_x11_x11_lib X11)
                        endif()
                        if(_rk_x11_xlib_h AND _rk_x11_x11_lib)
                            message(STATUS "rk_lib: system X11 (manual): ${_rk_x11_xlib_h} ${_rk_x11_x11_lib}")
                        endif()
                    endif()

                    if(_rk_x11_xlib_h AND _rk_x11_x11_lib)
                        set(_rk_x11_inc_dirs ${_rk_x11_xlib_h})
                        set(_rk_x11_link_libs ${_rk_x11_x11_lib})
                    endif()
                endif()

                if(_rk_x11_inc_dirs AND _rk_x11_link_libs)
                    list(APPEND _rk_x11_sources "${CMAKE_CURRENT_SOURCE_DIR}/maixcdk_rk_lib/src/rk_x11.c")
                else()
                    message(FATAL_ERROR
                        "rk_lib: X11 not found (RK_LIB_WITH_X11=ON, RK_LIB_BUNDLED_X11=n).\n"
                        "  Native: sudo apt install libx11-dev\n"
                        "  Or turn on RK_LIB_BUNDLED_X11 in menuconfig. Headless: RK_LIB_WITH_X11=OFF")
                endif()
            endif()
        else()
            list(APPEND _rk_x11_sources "${CMAKE_CURRENT_SOURCE_DIR}/maixcdk_rk_lib/src/rk_x11_stub.c")
            message(STATUS "rk_lib: RK_LIB_WITH_X11=OFF (V4L2 only; rk_vo_* return -ENOSYS)")
        endif()

        list(APPEND ADD_SRCS ${_rk_x11_sources})
        register_component(DYNAMIC)
        target_compile_options(rk_lib PRIVATE -fPIC)
        if(RK_LIB_WITH_X11 AND _rk_x11_use_bundled)
            target_link_libraries(rk_lib PRIVATE m pthread)
        elseif(RK_LIB_WITH_X11 AND _rk_x11_link_libs)
            target_include_directories(rk_lib PRIVATE ${_rk_x11_inc_dirs})
            target_link_libraries(rk_lib PRIVATE ${_rk_x11_link_libs} m pthread)
        else()
            target_link_libraries(rk_lib PRIVATE m pthread)
        endif()
    else()
        set(_rk_prebuilt "${CMAKE_CURRENT_SOURCE_DIR}/lib/librk_lib.so")
        if(NOT EXISTS "${_rk_prebuilt}")
            message(FATAL_ERROR "Prebuilt rk_lib expected at ${_rk_prebuilt} (enable RK_LIB_COMPILE_FROM_SOURCE or add librk_lib.so)")
        endif()
        list(APPEND ADD_DYNAMIC_LIB "lib/librk_lib.so")
        list(APPEND ADD_DIST_LIB_IGNORE "lib/librk_lib.so")
        register_component()
    endif()
else()
    register_component()
endif()
