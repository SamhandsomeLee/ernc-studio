add_library(ernc_domain STATIC
    types/types.cpp
    catalog/catalog.cpp
    catalog/alias_resolver.cpp
    firmware_layout/firmware_layout.cpp
    region/region_catalog.cpp
    parameter/runtime_store.cpp
    parameter/array_state.cpp
    parameter/write_plan.cpp
    validation/validation.cpp
    control_semantics/toggle_semantics.cpp
    control_semantics/command_policy.cpp
    transfer/chunk_plan.cpp
)

target_include_directories(ernc_domain PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_compile_features(ernc_domain PUBLIC cxx_std_20)

add_executable(ernc_catalog_contract
    catalog/contract_main.cpp
)

target_link_libraries(ernc_catalog_contract PRIVATE ernc_domain)

set_target_properties(ernc_catalog_contract PROPERTIES
    OUTPUT_NAME "ernc-catalog-contract"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(ernc_alias_contract
    catalog/alias_contract_main.cpp
)

target_link_libraries(ernc_alias_contract PRIVATE ernc_domain)

set_target_properties(ernc_alias_contract PROPERTIES
    OUTPUT_NAME "ernc-alias-contract"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(ernc_layout_contract
    firmware_layout/contract_main.cpp
)

target_link_libraries(ernc_layout_contract PRIVATE ernc_domain)

set_target_properties(ernc_layout_contract PROPERTIES
    OUTPUT_NAME "ernc-layout-contract"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
