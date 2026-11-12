add_library(ernc_application STATIC
    read_parameter.cpp
    stage_value.cpp
    write_parameter.cpp
    batch_read.cpp
    batch_write.cpp
    array_operations.cpp
    ../region/region_reader.cpp
    ../region/region_writer.cpp
    ../region/transfer_cancellation.cpp
    ../workflows/workflow_runner.cpp
    ../workflows/enc_echo.cpp
    ../workflows/enc_divergence.cpp
    ../workflows/horn.cpp
)

target_include_directories(ernc_application PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_application PUBLIC ernc_device ernc_protocol)

target_compile_features(ernc_application PUBLIC cxx_std_20)
