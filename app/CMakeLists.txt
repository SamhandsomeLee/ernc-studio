find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(ernc_app
    main.cpp
    ../src/presentation/theme/theme.cpp
    ../src/presentation/theme/status_badge.cpp
    ../src/presentation/theme/theme_sample.cpp
)

qt_add_resources(ernc_app "ernc_themes"
    PREFIX "/ernc"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${CMAKE_SOURCE_DIR}/resources/themes/ernc-light.qss"
)

target_include_directories(ernc_app PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

target_link_libraries(ernc_app PRIVATE Qt6::Widgets)

set_target_properties(ernc_app PROPERTIES
    OUTPUT_NAME "ernc-studio"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
