# Bundled X.Org client libraries for RK3566 (no apt libx11-dev / multiarch).
# Produces libX11.so (+ libxcb, libXau, libXdmcp) under ${CMAKE_BINARY_DIR}/x11_client_install

if(PLATFORM_RK3566)
    find_program(X11_CLIENT_PYTHON3 python3)
    if(NOT X11_CLIENT_PYTHON3)
        message(FATAL_ERROR "x11_client: 'python3' not found (needed for xcb-proto / libxcb code generation)")
    endif()
    find_program(X11_CLIENT_MESON meson)
    if(NOT X11_CLIENT_MESON)
        message(FATAL_ERROR "x11_client: 'meson' not found (needed for xorgproto). Install: sudo apt install meson ninja-build")
    endif()
    find_program(X11_CLIENT_NINJA NAMES ninja ninja-build)
    if(NOT X11_CLIENT_NINJA)
        message(FATAL_ERROR "x11_client: 'ninja' not found. Install: sudo apt install ninja-build")
    endif()

    set(_x11_prefix "${CMAKE_BINARY_DIR}/x11_client_install")
    set(_x11_src "${DL_EXTRACTED_PATH}/x11_client_srcs")
    set(_x11_stamp "${_x11_prefix}/.stamp_x11_client")
    set(_x11_script "${CMAKE_CURRENT_LIST_DIR}/scripts/build_x11_client.sh")
    set(_x11_libx11 "${_x11_prefix}/lib/libX11.so")
    set(_x11_mark "${_x11_src}/libX11-1.8.7/configure")

    list(APPEND ADD_INCLUDE "${_x11_prefix}/include")
    list(APPEND ADD_REQUIREMENTS pthread)
    list(APPEND ADD_REQUIREMENTS
        "${_x11_prefix}/lib/libX11.so"
        "${_x11_prefix}/lib/libxcb.so"
        "${_x11_prefix}/lib/libXau.so"
        "${_x11_prefix}/lib/libXdmcp.so"
    )
    list(APPEND ADD_DYNAMIC_LIB
        "${_x11_prefix}/lib/libX11.so"
        "${_x11_prefix}/lib/libxcb.so"
        "${_x11_prefix}/lib/libXau.so"
        "${_x11_prefix}/lib/libXdmcp.so"
    )

    set_property(SOURCE "${_x11_prefix}/include" PROPERTY GENERATED 1)
    set_property(SOURCE "${_x11_libx11}" PROPERTY GENERATED 1)

    add_custom_command(
        OUTPUT
            "${_x11_stamp}"
            "${_x11_libx11}"
            "${_x11_prefix}/lib/libxcb.so"
            "${_x11_prefix}/lib/libXau.so"
            "${_x11_prefix}/lib/libXdmcp.so"
        COMMAND bash "${_x11_script}" "${_x11_src}" "${_x11_prefix}" "${CONFIG_TOOLCHAIN_PREFIX}"
        DEPENDS "${_x11_script}" "${_x11_mark}"
        COMMENT "Building x11_client (bundled libX11 + libxcb from X.Org sources)..."
    )
    list(APPEND ADD_FILE_DEPENDS "${_x11_stamp}")

    list(APPEND ADD_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/x11_client_placeholder.c")
    register_component()
else()
    list(APPEND ADD_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/x11_client_placeholder.c")
    register_component()
endif()
