add_library(ernc_domain STATIC
    types/types.cpp
)

target_include_directories(ernc_domain PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_compile_features(ernc_domain PUBLIC cxx_std_20)
