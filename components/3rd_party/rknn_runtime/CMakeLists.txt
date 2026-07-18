if(PLATFORM_RK3566)
    set(_rknn_local_root "${CMAKE_CURRENT_LIST_DIR}/rknn_runtime_prebuilt")
    set(_rknn_extract_root "${DL_EXTRACTED_PATH}/rknn_runtime")
    set(_rknn_root "")

    if(EXISTS "${_rknn_local_root}/include/rknn_api.h")
        set(_rknn_root "${_rknn_local_root}")
    else()
        file(GLOB _rknn_extract_dirs LIST_DIRECTORIES true "${_rknn_extract_root}/*")
        foreach(_item IN LISTS _rknn_extract_dirs)
            if(EXISTS "${_item}/include/rknn_api.h")
                set(_rknn_root "${_item}")
                break()
            endif()
        endforeach()
    endif()

    if(NOT _rknn_root)
        message(FATAL_ERROR
            "rknn_runtime: RKNN runtime not found.\n"
            "  Put files under components/3rd_party/rknn_runtime/rknn_runtime_prebuilt/:\n"
            "    include/rknn_api.h\n"
            "    lib/librknnrt.so\n"
            "  Or place extracted package under ${_rknn_extract_root}/<pkg>/."
        )
    endif()

    list(APPEND ADD_INCLUDE "${_rknn_root}/include")
    list(APPEND ADD_DYNAMIC_LIB "${_rknn_root}/lib/librknnrt.so")
    list(APPEND ADD_REQUIREMENTS pthread dl)

    set_property(SOURCE "${_rknn_root}/include" PROPERTY GENERATED 1)
    set_property(SOURCE "${_rknn_root}/lib/librknnrt.so" PROPERTY GENERATED 1)
endif()

register_component()
