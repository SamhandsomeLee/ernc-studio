find_package(Qt6 REQUIRED COMPONENTS Widgets Core SerialPort)

qt_standard_project_setup()

qt_add_executable(ernc_app
    main.cpp
    ../src/presentation/theme/theme.cpp
    ../src/presentation/theme/status_badge.cpp
    ../src/presentation/shell/main_window.cpp
    ../src/presentation/shell/layout_settings.cpp
    ../src/presentation/navigator/navigator_pages.cpp
    ../src/presentation/navigator/navigator_model.cpp
    ../src/presentation/workspace/editor_tabs.cpp
    ../src/presentation/pages/ernc_flag.cpp
    ../src/presentation/pages/ernc_in_out.cpp
    ../src/presentation/pages/enc_param.cpp
    ../src/presentation/pages/rnc_param.cpp
    ../src/presentation/pages/check.cpp
    ../src/presentation/pages/switch_state.cpp
    ../src/presentation/pages/can_names.cpp
    ../src/presentation/pages/can_info_panel.cpp
    ../src/presentation/pages/threshold.cpp
    ../src/presentation/pages/other_page.cpp
    ../src/presentation/parameters/parameter_table_model.cpp
    ../src/presentation/parameters/parameter_filter_proxy.cpp
    ../src/presentation/parameters/write_plan_dialog.cpp
    ../src/presentation/controls/parameter_delegates.cpp
    ../src/presentation/controls/parameter_toggle_switch.cpp
    ../src/presentation/inspector/inspector_context.cpp
    ../src/presentation/inspector/inspector_panel.cpp
    ../src/presentation/task_cards/task_card_panel.cpp
    ../src/presentation/operation_center/operation_center_panel.cpp
    ../src/presentation/diagnostics/diagnostic_log_presenter.cpp
    ../src/presentation/diagnostics/session_log_paths.cpp
    ../src/presentation/device/device_controller.cpp
    ../src/presentation/device/device_runtime_worker.cpp
    ../src/presentation/device/serial_connect_dialog.cpp
    ../src/presentation/device/serial_linked_stack.cpp
)

qt_add_resources(ernc_app "ernc_themes"
    PREFIX "/ernc"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${CMAKE_SOURCE_DIR}/resources/themes/ernc-light.qss"
)

target_include_directories(ernc_app PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

target_compile_definitions(ernc_app PRIVATE
    "ERNC_PARAMETER_CATALOG_PATH=\"${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json\""
    "ERNC_LAYOUT_MANIFEST_PATH=\"${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json\""
    "ERNC_CAN_NAMES_PATH=\"${CMAKE_SOURCE_DIR}/catalog/source/can_names.raw.json\""
    "ERNC_CAN_SLOT_MAP_PATH=\"${CMAKE_SOURCE_DIR}/catalog/can_msg_slot_map.json\""
    "ERNC_SHELL_KEYMAP_PATH=\"${CMAKE_SOURCE_DIR}/resources/keymap/shell.txt\""
)

target_link_libraries(ernc_app PRIVATE Qt6::Widgets Qt6::SerialPort ernc_ui_qwt ernc_domain ernc_application ernc_device
    ernc_logging ernc_filesystem ernc_serial)

set_target_properties(ernc_app PROPERTIES
    OUTPUT_NAME "ernc-studio"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
