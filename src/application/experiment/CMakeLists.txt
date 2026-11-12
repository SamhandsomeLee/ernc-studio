find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_library(ernc_experiment STATIC
    experiment_service.cpp
)

target_include_directories(ernc_experiment PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_experiment PUBLIC
    ernc_domain
    ernc_storage
    Qt6::Core
    Qt6::Sql
)

target_compile_features(ernc_experiment PUBLIC cxx_std_20)
