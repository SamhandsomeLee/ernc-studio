add_executable(ernc_firmware_layout_tests
    firmware_layout_test.cpp
)

target_link_libraries(ernc_firmware_layout_tests PRIVATE ernc_domain)

set(ernc_layout_manifest "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json")

set(ernc_layout_cases
    indexes_children
    anc_unverified
    child_path_unverified
    order_four_not_found
    refer_num_not_found
    short_name_not_found
    blank_path_not_found
    verified_float
    verified_int32
    verified_uint32
    verified_struct
    verified_child
    child_short_name_not_found
    pack4_unverified
    size_out_of_range
    slot_out_of_range
    shape_out_of_range
    duplicate_path
    bad_alignment
    malformed_document
    empty_document
    missing_file
)

add_test(
    NAME alias_contract.shipped
    COMMAND ernc_alias_contract
        "${CMAKE_SOURCE_DIR}/catalog/parameter_aliases.json"
        "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
)

add_executable(ernc_alias_resolver_tests
    alias_resolver_test.cpp
)

target_link_libraries(ernc_alias_resolver_tests PRIVATE ernc_domain)

set(ernc_alias_cases
    unique_pack4_stays_unverified
    unique_verified_keeps_geometry
    out_of_range_path_is_not_unknown
    unknown_path
    case_sensitive_name
    case_sensitive_path
    unresolved_has_no_path
    absent_name_is_not_unresolved
    duplicate_name_drops_table
    path_conflict_drops_table
    explicit_share
    lone_shared
    partial_share_of_three
    shared_category_keeps_order
    empty_aliases
    unique_rejects_reason
    unresolved_requires_reason
    unresolved_rejects_path
    rejects_blank_and_unknown_status
    duplicate_key
    malformed_alias_document
    missing_alias_file
    shipped_refer_num
    shipped_can_msg
    shipped_advanced
)

foreach(ernc_alias_case IN LISTS ernc_alias_cases)
    add_test(
        NAME "alias_resolver.${ernc_alias_case}"
        COMMAND ernc_alias_resolver_tests
            "${ernc_alias_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_aliases.json"
            "${ernc_layout_manifest}"
    )
endforeach()

foreach(ernc_layout_case IN LISTS ernc_layout_cases)
    add_test(
        NAME "firmware_layout.${ernc_layout_case}"
        COMMAND ernc_firmware_layout_tests "${ernc_layout_case}" "${ernc_layout_manifest}"
    )
endforeach()
