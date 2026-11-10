add_library(ernc_domain STATIC
    types/types.cpp
    catalog/catalog.cpp
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
