find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_library(ernc_storage STATIC
    migrator.cpp
    migrations/migrations.cpp
    snapshot_repository.cpp
    experiment_repository.cpp
    experiment_asset_store.cpp
)

target_include_directories(ernc_storage PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ernc_storage PUBLIC
    ernc_domain
    Qt6::Core
    Qt6::Sql
)

target_compile_features(ernc_storage PUBLIC cxx_std_20)
