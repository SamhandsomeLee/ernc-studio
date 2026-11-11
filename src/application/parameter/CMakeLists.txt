add_library(ernc_application STATIC
    read_parameter.cpp
)

target_include_directories(ernc_application PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_application PUBLIC ernc_device ernc_protocol)

target_compile_features(ernc_application PUBLIC cxx_std_20)
