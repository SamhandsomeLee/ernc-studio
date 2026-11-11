add_library(ernc_device STATIC
    session.cpp
    scheduler.cpp
    retry_policy.cpp
)

target_include_directories(ernc_device PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ernc_device PUBLIC ernc_domain ernc_protocol)

target_compile_features(ernc_device PUBLIC cxx_std_20)
