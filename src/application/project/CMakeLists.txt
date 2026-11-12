find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_library(ernc_project STATIC
    project_session.cpp
    parameter_snapshot.cpp
    ../../infrastructure/filesystem/project_store.cpp
)

target_include_directories(ernc_project PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_project PUBLIC
    ernc_domain
    ernc_storage
    Qt6::Core
    Qt6::Sql
)

target_compile_features(ernc_project PUBLIC cxx_std_20)
