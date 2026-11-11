add_library(ernc_filesystem STATIC
    legacy_region_codec.cpp
)

target_include_directories(ernc_filesystem PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_filesystem PUBLIC ernc_domain)

target_compile_features(ernc_filesystem PUBLIC cxx_std_20)
