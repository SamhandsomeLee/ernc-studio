add_library(ernc_protocol STATIC
    crc16.cpp
    wire_encoder.cpp
    modbus_fc03.cpp
    modbus_fc16.cpp
    typed_codec.cpp
    response_validator.cpp
)

target_include_directories(ernc_protocol PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_protocol PUBLIC ernc_domain)

target_compile_features(ernc_protocol PUBLIC cxx_std_20)
