find_package(Qt6 REQUIRED COMPONENTS Core SerialPort)

add_executable(ernc-serial-slave main.cpp)

set_target_properties(ernc-serial-slave PROPERTIES AUTOMOC ON)

target_link_libraries(ernc-serial-slave PRIVATE
    Qt6::Core
    Qt6::SerialPort
    ernc_application
    ernc_domain
    ernc_filesystem
    ernc_protocol
    ernc_simulator
)

target_compile_features(ernc-serial-slave PRIVATE cxx_std_20)
