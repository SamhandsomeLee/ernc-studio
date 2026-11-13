add_library(ernc_logging STATIC
    diagnostic_log.cpp
    app_session_log.cpp
    session_log_archive.cpp
)

target_include_directories(ernc_logging PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_logging PUBLIC ernc_domain)

target_compile_features(ernc_logging PUBLIC cxx_std_20)
