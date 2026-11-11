add_library(ernc_simulator STATIC
    slave_image.cpp
    memory_transport.cpp
    faults/fault_sequence.cpp
    faults/fault_apply.cpp
)

target_include_directories(ernc_simulator PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ernc_simulator PUBLIC ernc_protocol)

target_compile_features(ernc_simulator PUBLIC cxx_std_20)
