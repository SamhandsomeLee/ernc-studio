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
    shipped_member_alias
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

add_executable(ernc_runtime_store_tests
    runtime_store_test.cpp
)

target_link_libraries(ernc_runtime_store_tests PRIVATE ernc_domain)

set(ernc_runtime_cases
    missing_id_has_no_state
    observe_keeps_staged
    stage_keeps_stale_device
    offline_keeps_values
    failed_observation_keeps_values
    failed_observation_on_unknown
    stage_starts_stale
    zero_kinds_stay_distinct
    offline_on_empty
)

foreach(ernc_runtime_case IN LISTS ernc_runtime_cases)
    add_test(
        NAME "runtime_store.${ernc_runtime_case}"
        COMMAND ernc_runtime_store_tests "${ernc_runtime_case}"
    )
endforeach()

add_executable(ernc_validation_tests
    validation_test.cpp
)

target_link_libraries(ernc_validation_tests PRIVATE ernc_domain)

set(ernc_validation_cases
    step_rejected
    command_rejected
    read_write_float_accepted
    type_mismatch_rejected
    non_finite_rejected
    integer_kinds_accepted
    missing_access_rejected
    undeclared_type_accepted
    catalog_types_match_header
)

foreach(ernc_validation_case IN LISTS ernc_validation_cases)
    add_test(
        NAME "validation.${ernc_validation_case}"
        COMMAND ernc_validation_tests
            "${ernc_validation_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
    )
endforeach()

add_executable(ernc_toggle_semantics_tests
    toggle_semantics_test.cpp
)

target_link_libraries(ernc_toggle_semantics_tests PRIVATE ernc_domain)

set(ernc_toggle_cases
    reverse_switches
    unconfirmed_stay_unknown
    encode_reverse_levels
    encode_rejects_unknown
    integer_level_rejects_float
)

foreach(ernc_toggle_case IN LISTS ernc_toggle_cases)
    add_test(
        NAME "toggle_semantics.${ernc_toggle_case}"
        COMMAND ernc_toggle_semantics_tests
            "${ernc_toggle_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
    )
endforeach()

add_executable(ernc_command_policy_tests
    command_policy_test.cpp
)

target_link_libraries(ernc_command_policy_tests PRIVATE ernc_domain)

set(ernc_command_policy_cases
    policy_is_reserved
    catalog_classes_stay_reserved
    mode_change_is_reserved
    momentary_stays_one_shot
    missing_access_is_not_actuated
)

foreach(ernc_command_policy_case IN LISTS ernc_command_policy_cases)
    add_test(
        NAME "command_policy.${ernc_command_policy_case}"
        COMMAND ernc_command_policy_tests
            "${ernc_command_policy_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
    )
endforeach()

add_executable(ernc_write_plan_tests
    write_plan_test.cpp
)

target_link_libraries(ernc_write_plan_tests PRIVATE ernc_domain)

set(ernc_write_plan_cases
    selected_verified_is_included
    blocked_kinds_stay_out
    pack4_path_is_included
    category_filters
    stale_flag_kept
    store_unchanged
    duplicate_selection_once
    real_manifest_alpha_included
)

foreach(ernc_write_plan_case IN LISTS ernc_write_plan_cases)
    add_test(
        NAME "write_plan.${ernc_write_plan_case}"
        COMMAND ernc_write_plan_tests
            "${ernc_write_plan_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

foreach(ernc_layout_case IN LISTS ernc_layout_cases)
    add_test(
        NAME "firmware_layout.${ernc_layout_case}"
        COMMAND ernc_firmware_layout_tests "${ernc_layout_case}" "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_crc16_tests
    crc16_test.cpp
)

target_link_libraries(ernc_crc16_tests PRIVATE ernc_protocol)

set(ernc_crc16_cases
    catalogue_123456789
    append_lays_low_byte_first
    read_body_crc
    literal_frame_verifies
    tampered_crc_rejected
    tampered_body_rejected
    shorter_than_three_rejected
    empty_append_is_not_a_valid_frame
)

foreach(ernc_crc16_case IN LISTS ernc_crc16_cases)
    add_test(
        NAME "crc16.${ernc_crc16_case}"
        COMMAND ernc_crc16_tests "${ernc_crc16_case}"
    )
endforeach()

add_executable(ernc_wire_encoder_tests
    wire_encoder_test.cpp
)

target_link_libraries(ernc_wire_encoder_tests PRIVATE ernc_protocol)

set(ernc_wire_encoder_cases
    eight_byte_frame_prefixed
    max_frame_encodes
    over_max_rejected
    empty_frame_is_zero_length
)

foreach(ernc_wire_encoder_case IN LISTS ernc_wire_encoder_cases)
    add_test(
        NAME "wire_encoder.${ernc_wire_encoder_case}"
        COMMAND ernc_wire_encoder_tests "${ernc_wire_encoder_case}"
    )
endforeach()
