find_package(Qt6 REQUIRED COMPONENTS Core SerialPort)

qt_standard_project_setup()

qt_add_library(ernc_serial STATIC
    serial_transport.cpp
    serial_transport.hpp
)

target_include_directories(ernc_serial PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ernc_serial
    PUBLIC ernc_domain Qt6::Core Qt6::SerialPort
)

target_compile_features(ernc_serial PUBLIC cxx_std_20)
