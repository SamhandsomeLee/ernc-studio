find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Svg OpenGL)
include(FindQwt)
ernc_require_qwt()

set(ernc_ui_qwt_sources
    ../analysis/analysis_panel.cpp
    ../analysis/analysis_region_bridge.cpp
    ../analysis/check_data_plot.cpp
    ../analysis/dataset_manager.cpp
    ../analysis/ernc_tf_plot.cpp
    ../analysis/plot_workspace.cpp
    ../analysis/spec_radius_plot.cpp
    ../regions/region_workspace.cpp
    ../regions/spec_radius_page.cpp
)

add_library(ernc_ui_qwt STATIC ${ernc_ui_qwt_sources})

target_include_directories(ernc_ui_qwt
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src/presentation"
)

target_link_libraries(ernc_ui_qwt
    PUBLIC
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::PrintSupport
        Qt6::Svg
        Qt6::OpenGL
        ernc_domain
        ernc_application
    PRIVATE
        ernc_filesystem
)

ernc_target_link_qwt(ernc_ui_qwt)

target_compile_features(ernc_ui_qwt PUBLIC cxx_std_20)
