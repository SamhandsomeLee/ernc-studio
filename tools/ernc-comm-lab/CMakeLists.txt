add_executable(ernc_comm_lab
    main.cpp
)

target_link_libraries(ernc_comm_lab PRIVATE ernc_device ernc_simulator)

set_target_properties(ernc_comm_lab PROPERTIES
    OUTPUT_NAME "ernc-comm-lab"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_features(ernc_comm_lab PRIVATE cxx_std_20)
