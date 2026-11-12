# Qwt 6 for Qt 6 under MSYS2 MINGW64. Shared by ernc_ui_qwt and CTest plot targets.

macro(ernc_require_qwt)
    if(NOT DEFINED ERNC_MINGW64_ROOT OR ERNC_MINGW64_ROOT STREQUAL "")
        message(FATAL_ERROR "ERNC_MINGW64_ROOT is required to find Qwt")
    endif()
    set(ernc_qwt_include "${ERNC_MINGW64_ROOT}/include/qwt-qt6")
    set(ernc_qwt_library "${ERNC_MINGW64_ROOT}/lib/libqwt-qt6.dll.a")
    if(NOT EXISTS "${ernc_qwt_include}/qwt.h" OR NOT EXISTS "${ernc_qwt_library}")
        message(FATAL_ERROR
            "Qwt 6 for Qt 6 is missing under ${ERNC_MINGW64_ROOT}. "
            "Expected include/qwt-qt6/qwt.h and lib/libqwt-qt6.dll.a.")
    endif()
    if(NOT TARGET Ernc::Qwt)
        add_library(Ernc::Qwt UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Ernc::Qwt PROPERTIES
            IMPORTED_LOCATION "${ernc_qwt_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${ernc_qwt_include}")
    endif()
endmacro()

function(ernc_target_link_qwt target)
    if(NOT TARGET Ernc::Qwt)
        message(FATAL_ERROR "ernc_require_qwt() must run before ernc_target_link_qwt")
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Concurrent PrintSupport Svg OpenGL)
    target_link_libraries(${target} PRIVATE
        Ernc::Qwt
        Qt6::Concurrent
        Qt6::PrintSupport
        Qt6::Svg
        Qt6::OpenGL)
    target_include_directories(${target} PRIVATE "${ernc_qwt_include}")
endfunction()
