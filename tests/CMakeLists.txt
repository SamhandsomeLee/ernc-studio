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
    locate_pack4_anc
    locate_pack4_can_msg
    locate_missing
    locate_pack4_out_of_range
    locate_verified_keeps_geometry
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

add_executable(ernc_array_state_tests
    array_state_test.cpp
)

target_link_libraries(ernc_array_state_tests PRIVATE ernc_domain)

foreach(ernc_array_state_case IN ITEMS edit_one_index_keeps_other_read reset_copies_read_into_set type_or_index_mismatch_is_rejected mark_clean_keeps_values)
    add_test(
        NAME "array_state.${ernc_array_state_case}"
        COMMAND ernc_array_state_tests "${ernc_array_state_case}"
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

add_executable(ernc_region_catalog_tests
    region_catalog_test.cpp
)

target_link_libraries(ernc_region_catalog_tests PRIVATE ernc_domain)

foreach(ernc_region_case IN ITEMS manifest_regions_match_layout gap_rejects_the_catalog verified_write_follows_writable missing_region_rejects_the_catalog)
    add_test(
        NAME "region_catalog.${ernc_region_case}"
        COMMAND ernc_region_catalog_tests "${ernc_region_case}" "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_read_parameter_tests
    read_parameter_test.cpp
)

target_link_libraries(ernc_read_parameter_tests PRIVATE ernc_application ernc_simulator)

set(ernc_read_parameter_cases
    anc_off_observes_scalar
    can_msg_reads_seventy_two
    unknown_id_does_not_enqueue
    check_data_does_not_enqueue
    disconnected_does_not_enqueue
    second_read_while_busy
    stale_session_does_not_observe
)

foreach(ernc_read_parameter_case IN LISTS ernc_read_parameter_cases)
    add_test(
        NAME "read_parameter.${ernc_read_parameter_case}"
        COMMAND ernc_read_parameter_tests
            "${ernc_read_parameter_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_stage_value_tests
    stage_value_test.cpp
)

target_link_libraries(ernc_stage_value_tests PRIVATE ernc_application)

set(ernc_stage_value_cases
    anc_off_stages_scalar
    step_is_read_only
    refresh_is_rejected
    nan_is_rejected
    type_mismatch_is_rejected
    can_msg_is_rejected
    check_data_is_rejected
    unknown_id_is_rejected
    failed_stage_keeps_previous
)

foreach(ernc_stage_value_case IN LISTS ernc_stage_value_cases)
    add_test(
        NAME "stage_value.${ernc_stage_value_case}"
        COMMAND ernc_stage_value_tests
            "${ernc_stage_value_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_write_parameter_tests
    write_parameter_test.cpp
)

target_link_libraries(ernc_write_parameter_tests PRIVATE ernc_application ernc_simulator)

set(ernc_write_parameter_cases
    anc_off_verified_clears_dirty
    readback_mismatch_keeps_staged
    ack_timeout_does_not_resend
    step_does_not_enqueue
    can_msg_does_not_enqueue
    edit_during_write_stays_dirty
)

foreach(ernc_write_parameter_case IN LISTS ernc_write_parameter_cases)
    add_test(
        NAME "write_parameter.${ernc_write_parameter_case}"
        COMMAND ernc_write_parameter_tests
            "${ernc_write_parameter_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_batch_read_tests
    batch_read_test.cpp
)

target_link_libraries(ernc_batch_read_tests PRIVATE ernc_application ernc_simulator)

set(ernc_batch_read_cases
    all_first_frame_is_full
    flag_switches_share_one_frame
    gap_is_not_filled
    selected_pair_updates_values
    can_msg_uses_full_frames
    check_data_is_not_in_a_frame
)

add_executable(ernc_batch_write_tests
    batch_write_test.cpp
)

target_link_libraries(ernc_batch_write_tests PRIVATE ernc_application ernc_simulator)

set(ernc_batch_write_cases
    two_scalars_write_one_at_a_time
    mismatch_names_anc_then_writes_enc
    step_and_can_msg_are_not_written
)

foreach(ernc_batch_write_case IN LISTS ernc_batch_write_cases)
    add_test(
        NAME "batch_write.${ernc_batch_write_case}"
        COMMAND ernc_batch_write_tests
            "${ernc_batch_write_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_array_operations_tests
    array_operations_test.cpp
)

target_link_libraries(ernc_array_operations_tests PRIVATE ernc_application ernc_simulator)

set(ernc_array_operation_cases
    can_msg_read_tail_keeps_set
    read_stops_at_failed_index
    cancel_keeps_completed_prefix
    dirty_gap_splits_write
    horn_write_tail_is_ten_slots
    mismatch_stops_later_chunks
    ack_timeout_does_not_resend
    can_msg_and_step_do_not_enqueue
)

foreach(ernc_array_operation_case IN LISTS ernc_array_operation_cases)
    add_test(
        NAME "array_operations.${ernc_array_operation_case}"
        COMMAND ernc_array_operations_tests
            "${ernc_array_operation_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

foreach(ernc_batch_read_case IN LISTS ernc_batch_read_cases)
    add_test(
        NAME "batch_read.${ernc_batch_read_case}"
        COMMAND ernc_batch_read_tests
            "${ernc_batch_read_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
            "${ernc_layout_manifest}"
    )
endforeach()

add_executable(ernc_chunk_plan_tests
    chunk_plan_test.cpp
)

target_link_libraries(ernc_chunk_plan_tests PRIVATE ernc_domain)

set(ernc_chunk_plan_cases
    read_720_default
    read_tail_forty
    write_sixty_two_default
    write_sixty_two_at_max
    read_sixty_two_exact
    read_sixty_two_uses_default
    zero_count_is_empty
    zero_chunk_size_rejected
    read_oversize_chunk_clamped
    write_oversize_chunk_clamped
    write_one_hundred_twenty_two_at_max
    single_slot
    read_exactly_sixty
    read_sixty_one_default_tail
    write_sixty_one_default_tail
    write_sixty_one_exact
    read_sixty_three_default
    read_sixty_three_at_max
    chunk_size_one
    nonzero_start_is_contiguous
    last_uint32_slot
    range_past_uint32_rejected
    full_read_chunk_ending_at_max
    write_range_at_uint32_edge
)

foreach(ernc_chunk_plan_case IN LISTS ernc_chunk_plan_cases)
    add_test(
        NAME "chunk_plan.${ernc_chunk_plan_case}"
        COMMAND ernc_chunk_plan_tests "${ernc_chunk_plan_case}"
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

add_executable(ernc_modbus_fc03_tests
    modbus_fc03_test.cpp
)

target_link_libraries(ernc_modbus_fc03_tests PRIVATE ernc_protocol)

set(ernc_modbus_fc03_cases
    one_slot_request
    sixty_slot_request
    sixty_two_slot_request
    slot_one_register_is_big_endian
    last_register_is_big_endian
    zero_slots_rejected
    sixty_three_slots_rejected
    register_span_rejected
    slave_argument_is_written
    payload_bytes_keep_wire_order
    exception_code_is_kept
    tampered_crc_rejected
    byte_count_mismatch_rejected
    response_sixty_three_rejected
    other_function_rejected
)

foreach(ernc_modbus_fc03_case IN LISTS ernc_modbus_fc03_cases)
    add_test(
        NAME "modbus_fc03.${ernc_modbus_fc03_case}"
        COMMAND ernc_modbus_fc03_tests "${ernc_modbus_fc03_case}"
    )
endforeach()

add_executable(ernc_modbus_fc16_tests
    modbus_fc16_test.cpp
)

target_link_libraries(ernc_modbus_fc16_tests PRIVATE ernc_protocol)

set(ernc_modbus_fc16_cases
    one_slot_write
    payload_bytes_keep_order
    sixty_slot_write
    sixty_one_slot_write
    sixty_two_slots_rejected
    zero_slots_rejected
    payload_size_rejected
    register_span_rejected
    slave_argument_is_written
    ack_matches_address_and_count
    ack_count_mismatch
    ack_address_mismatch
    exception_code_is_kept
    tampered_crc_rejected
)

foreach(ernc_modbus_fc16_case IN LISTS ernc_modbus_fc16_cases)
    add_test(
        NAME "modbus_fc16.${ernc_modbus_fc16_case}"
        COMMAND ernc_modbus_fc16_tests "${ernc_modbus_fc16_case}"
    )
endforeach()

add_executable(ernc_typed_codec_tests
    typed_codec_test.cpp
)

target_link_libraries(ernc_typed_codec_tests PRIVATE ernc_protocol)

set(ernc_typed_codec_cases
    float_two_point_five
    float_negative
    signed_zero_bytes_differ
    int32_limits
    uint32_limits
    same_bytes_keep_kind
    quiet_nan_bits_round_trip
    nan_payload_bits_preserved
    infinities_round_trip
    short_buffer_rejected
    scalar_block_round_trip
    mixed_kinds_rejected
    empty_block_is_empty
    block_length_rejected
)

foreach(ernc_typed_codec_case IN LISTS ernc_typed_codec_cases)
    add_test(
        NAME "typed_codec.${ernc_typed_codec_case}"
        COMMAND ernc_typed_codec_tests "${ernc_typed_codec_case}"
    )
endforeach()

add_executable(ernc_response_validator_tests
    response_validator_test.cpp
)

target_link_libraries(ernc_response_validator_tests PRIVATE ernc_protocol)

set(ernc_response_validator_cases
    read_payload_accepted
    read_slave_mismatch
    read_length_mismatch
    wrong_function_rejected
    crc_rejected
    frame_length_rejected
    write_ack_accepted
    write_address_mismatch
    write_count_mismatch
    read_exception_keeps_code
    write_exception_keeps_code
    read_rejects_write_exception
    exception_slave_mismatch
    command_range_rejected
)

foreach(ernc_response_validator_case IN LISTS ernc_response_validator_cases)
    add_test(
        NAME "response_validator.${ernc_response_validator_case}"
        COMMAND ernc_response_validator_tests "${ernc_response_validator_case}"
    )
endforeach()

add_executable(ernc_wire_parser_tests
    wire_parser_test.cpp
)

target_link_libraries(ernc_wire_parser_tests PRIVATE ernc_protocol)

set(ernc_wire_parser_cases
    chunk_matches_byte_feed
    sticky_frames_in_order
    garbage_bytes_dropped
    half_frame_waits
    bad_crc_is_not_a_frame
    declared_length_waits
    clear_drops_partial_frame
)

foreach(ernc_wire_parser_case IN LISTS ernc_wire_parser_cases)
    add_test(
        NAME "wire_parser.${ernc_wire_parser_case}"
        COMMAND ernc_wire_parser_tests "${ernc_wire_parser_case}"
    )
endforeach()

add_executable(ernc_device_transport_tests
    device_transport_test.cpp
)

target_link_libraries(ernc_device_transport_tests PRIVATE ernc_domain)

set(ernc_device_transport_cases
    closed_submit
    empty_submit
    duplicate_open
    submit_stores_bytes
    poll_bytes_then_idle
    close_while_closed
    close_emits_one_closed
    refused_submit
    fault_then_submit
)

foreach(ernc_device_transport_case IN LISTS ernc_device_transport_cases)
    add_test(
        NAME "device_transport.${ernc_device_transport_case}"
        COMMAND ernc_device_transport_tests "${ernc_device_transport_case}"
    )
endforeach()

add_executable(ernc_serial_transport_tests
    serial_transport_test.cpp
)

target_link_libraries(ernc_serial_transport_tests PRIVATE ernc_serial)

set(ernc_serial_transport_cases
    worker_owns_port
    missing_port_stays_closed
    closed_submit
    empty_submit_while_closed
    close_while_closed
    off_worker_open
    list_ports_has_no_empty_name
    interface_closed_submit
)

foreach(ernc_serial_transport_case IN LISTS ernc_serial_transport_cases)
    add_test(
        NAME "serial_transport.${ernc_serial_transport_case}"
        COMMAND ernc_serial_transport_tests "${ernc_serial_transport_case}"
    )
endforeach()

add_executable(ernc_device_session_tests
    device_session_test.cpp
)

target_link_libraries(ernc_device_session_tests PRIVATE ernc_device)

set(ernc_device_session_cases
    connect_sees_connecting
    open_failure_retires_id
    disconnect_drops_pending_and_old_id
    disconnect_drains_closed
    degraded_keeps_id
    second_connect_rejected
    pending_while_disconnected
    disconnect_while_disconnected
)

foreach(ernc_device_session_case IN LISTS ernc_device_session_cases)
    add_test(
        NAME "device_session.${ernc_device_session_case}"
        COMMAND ernc_device_session_tests "${ernc_device_session_case}"
    )
endforeach()

add_executable(ernc_scheduler_tests
    scheduler_test.cpp
)

target_link_libraries(ernc_scheduler_tests PRIVATE ernc_device)

set(ernc_scheduler_cases
    two_enqueues_one_submit
    response_releases_next
    high_waits_behind_inflight
    disconnect_drops_inflight
    empty_read_rejected
    write_ack_is_not_verified
    half_frame_stays_inflight
)

foreach(ernc_scheduler_case IN LISTS ernc_scheduler_cases)
    add_test(
        NAME "scheduler.${ernc_scheduler_case}"
        COMMAND ernc_scheduler_tests "${ernc_scheduler_case}"
    )
endforeach()

add_executable(ernc_retry_policy_tests
    retry_policy_test.cpp
)

target_link_libraries(ernc_retry_policy_tests PRIVATE ernc_device)

set(ernc_retry_policy_cases
    waiting_before_deadline
    resubmit_before_last_attempt
    single_attempt_timeout
    exhausted_after_resend
    invalid_limit
    scheduler_times_out_once
    scheduler_resubmits_same_frame
    response_cancels_retry
    write_ignores_read_deadline
    late_frame_keeps_timeout
)

foreach(ernc_retry_policy_case IN LISTS ernc_retry_policy_cases)
    add_test(
        NAME "retry_policy.${ernc_retry_policy_case}"
        COMMAND ernc_retry_policy_tests "${ernc_retry_policy_case}"
    )
endforeach()

add_executable(ernc_theme_tokens_tests
    theme_tokens_test.cpp
)

target_include_directories(ernc_theme_tokens_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

set(ernc_theme_tokens_cases
    tokens_match_design
    qss_contains_tokens
    missing_token_is_reported
    status_match_design
    shared_color_differs_by_icon_and_text
)

foreach(ernc_theme_tokens_case IN LISTS ernc_theme_tokens_cases)
    add_test(
        NAME "theme_tokens.${ernc_theme_tokens_case}"
        COMMAND ernc_theme_tokens_tests
            "${ernc_theme_tokens_case}"
            "${CMAKE_SOURCE_DIR}/resources/themes/ernc-light.qss"
    )
endforeach()

add_executable(ernc_navigator_pages_tests
    navigator_pages_test.cpp
    ../src/presentation/navigator/navigator_pages.cpp
)

target_include_directories(ernc_navigator_pages_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

target_link_libraries(ernc_navigator_pages_tests PRIVATE ernc_domain)

set(ernc_navigator_pages_cases
    page_order
    check_data_not_date
    switch_state_split
    parameters_once_in_file_order
    unexpected_category_rejected
    missing_category_rejected
)

foreach(ernc_navigator_pages_case IN LISTS ernc_navigator_pages_cases)
    add_test(
        NAME "navigator_pages.${ernc_navigator_pages_case}"
        COMMAND ernc_navigator_pages_tests
            "${ernc_navigator_pages_case}"
            "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
    )
endforeach()

add_executable(ernc_editor_selection_tests
    editor_selection_test.cpp
)

target_include_directories(ernc_editor_selection_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

set(ernc_editor_selection_cases
    initial_empty
    page_keeps_parameter
    empty_does_not_clear
)

foreach(ernc_editor_selection_case IN LISTS ernc_editor_selection_cases)
    add_test(
        NAME "editor_selection.${ernc_editor_selection_case}"
        COMMAND ernc_editor_selection_tests "${ernc_editor_selection_case}"
    )
endforeach()

add_executable(ernc_parameter_table_tests
    parameter_table_test.cpp
)

target_include_directories(ernc_parameter_table_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

set(ernc_parameter_table_cases
    fixed_rows
    read_is_not_editable
)

foreach(ernc_parameter_table_case IN LISTS ernc_parameter_table_cases)
    add_test(
        NAME "parameter_table.${ernc_parameter_table_case}"
        COMMAND ernc_parameter_table_tests "${ernc_parameter_table_case}"
    )
endforeach()

if(ERNC_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

    add_executable(ernc_parameter_store_model_tests
        parameter_store_model_test.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
    )

    target_include_directories(ernc_parameter_store_model_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_parameter_store_model_tests PRIVATE Qt6::Core ernc_domain)

    set(ernc_parameter_store_model_cases
        observe_shows_read
        stage_shows_set
        offline_shows_stale
        other_session_is_ignored
    )

    foreach(ernc_parameter_store_model_case IN LISTS ernc_parameter_store_model_cases)
        add_test(
            NAME "parameter_store_model.${ernc_parameter_store_model_case}"
            COMMAND ernc_parameter_store_model_tests
                "${ernc_parameter_store_model_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
        )
    endforeach()

    add_executable(ernc_parameter_filter_tests
        parameter_filter_test.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
    )

    target_include_directories(ernc_parameter_filter_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_parameter_filter_tests PRIVATE Qt6::Core ernc_domain)

    set(ernc_parameter_filter_cases
        all_search_keeps_source_order
        flag_page_hides_other_group
        dirty_keeps_stale_dirty_row
    )

    foreach(ernc_parameter_filter_case IN LISTS ernc_parameter_filter_cases)
        add_test(
            NAME "parameter_filter.${ernc_parameter_filter_case}"
            COMMAND ernc_parameter_filter_tests
                "${ernc_parameter_filter_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
        )
    endforeach()

    add_executable(ernc_parameter_delegate_tests
        parameter_delegate_test.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_parameter_delegate_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_parameter_delegate_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    set(ernc_parameter_delegate_cases
        anc_off_toggle_words
        anc_off_on_stages_zero
        step_has_no_editor
        running_time_keeps_firmware_value
    )

    foreach(ernc_parameter_delegate_case IN LISTS ernc_parameter_delegate_cases)
        add_test(
            NAME "parameter_delegate.${ernc_parameter_delegate_case}"
            COMMAND ernc_parameter_delegate_tests
                "${ernc_parameter_delegate_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_write_plan_dialog_tests
        write_plan_dialog_test.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
    )

    target_include_directories(ernc_write_plan_dialog_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_write_plan_dialog_tests PRIVATE Qt6::Widgets ernc_domain ernc_application ernc_simulator)

    set(ernc_write_plan_dialog_cases
        flag_preview_before_write
        rnc_time_and_step
        all_page_disables_confirm
        can_msg_is_not_sent
        ack_then_verified
        mismatch_shows_diagnostic
        timeout_does_not_resend
    )

    foreach(ernc_write_plan_dialog_case IN LISTS ernc_write_plan_dialog_cases)
        add_test(
            NAME "write_plan_dialog.${ernc_write_plan_dialog_case}"
            COMMAND ernc_write_plan_dialog_tests
                "${ernc_write_plan_dialog_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_flag_page_tests
        ernc_flag_page_test.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_flag_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_flag_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    set(ernc_flag_page_cases
        reverse_switches_stage_wire_values
        unknown_toggle_is_not_off
        speaker_keeps_raw_number
        bypass_has_no_toggle
        refresh_stays_blocked
        flag_page_follows_tree
    )

    foreach(ernc_flag_page_case IN LISTS ernc_flag_page_cases)
        add_test(
            NAME "ernc_flag.${ernc_flag_page_case}"
            COMMAND ernc_flag_page_tests
                "${ernc_flag_page_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_in_out_page_tests
        ernc_in_out_page_test.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_in_out_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_in_out_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    set(ernc_in_out_page_cases
        refernum_stages_count
        array_shortcut_keeps_parameter_id
        output_amplitude_stages_float
        bluetooth_gain_is_absent
        in_out_page_follows_tree
    )

    foreach(ernc_in_out_page_case IN LISTS ernc_in_out_page_cases)
        add_test(
            NAME "ernc_in_out.${ernc_in_out_page_case}"
            COMMAND ernc_in_out_page_tests
                "${ernc_in_out_page_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_enc_param_page_tests
        enc_param_page_test.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_enc_param_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_enc_param_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    set(ernc_enc_param_page_cases
        orders_share_fields_and_keep_raw_flag
        amp1_array_keeps_parameter_id
        order_four_five_are_absent
        enc_page_follows_tree
    )

    foreach(ernc_enc_param_page_case IN LISTS ernc_enc_param_page_cases)
        add_test(
            NAME "enc_param.${ernc_enc_param_page_case}"
            COMMAND ernc_enc_param_page_tests
                "${ernc_enc_param_page_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_rnc_param_page_tests
        rnc_param_page_test.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_rnc_param_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_rnc_param_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    set(ernc_rnc_param_page_cases
        step_has_no_writer
        running_time_keeps_firmware_value
        alpha_stages_and_leaky_keeps_id
        excluded_rows_are_absent
        rnc_page_follows_tree
    )

    foreach(ernc_rnc_param_page_case IN LISTS ernc_rnc_param_page_cases)
        add_test(
            NAME "rnc_param.${ernc_rnc_param_page_case}"
            COMMAND ernc_rnc_param_page_tests
                "${ernc_rnc_param_page_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_check_page_tests
        check_page_test.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_check_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_check_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    set(ernc_check_page_cases
        check_mode_requires_confirm
        check_region_keeps_parent_id
        check_page_follows_tree
    )

    foreach(ernc_check_page_case IN LISTS ernc_check_page_cases)
        add_test(
            NAME "check.${ernc_check_page_case}"
            COMMAND ernc_check_page_tests
                "${ernc_check_page_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_switch_state_page_tests
        switch_state_page_test.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_switch_state_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_switch_state_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    foreach(ernc_switch_case IN ITEMS switch_state_is_unnamed switch_page_follows_tree)
        add_test(
            NAME "switch_state.${ernc_switch_case}"
            COMMAND ernc_switch_state_page_tests
                "${ernc_switch_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_can_names_page_tests
        can_names_page_test.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_can_names_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_can_names_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    foreach(ernc_can_case IN ITEMS can_names_stay_unmapped can_catalog_opens_from_check)
        add_test(
            NAME "can_names.${ernc_can_case}"
            COMMAND ernc_can_names_page_tests
                "${ernc_can_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
                "${CMAKE_SOURCE_DIR}/catalog/source/can_names.raw.json"
        )
    endforeach()

    add_executable(ernc_threshold_page_tests
        threshold_page_test.cpp
        ../src/presentation/pages/threshold.cpp
        ../src/presentation/pages/can_names.cpp
        ../src/presentation/pages/switch_state.cpp
        ../src/presentation/pages/check.cpp
        ../src/presentation/pages/rnc_param.cpp
        ../src/presentation/pages/enc_param.cpp
        ../src/presentation/pages/ernc_flag.cpp
        ../src/presentation/pages/ernc_in_out.cpp
        ../src/presentation/workspace/editor_tabs.cpp
        ../src/presentation/parameters/parameter_table_model.cpp
        ../src/presentation/parameters/parameter_filter_proxy.cpp
        ../src/presentation/parameters/write_plan_dialog.cpp
        ../src/presentation/controls/parameter_delegates.cpp
    )

    target_include_directories(ernc_threshold_page_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_threshold_page_tests PRIVATE Qt6::Widgets ernc_domain ernc_application)

    foreach(ernc_threshold_case IN ITEMS threshold_groups_have_no_formula threshold_page_follows_tree)
        add_test(
            NAME "threshold.${ernc_threshold_case}"
            COMMAND ernc_threshold_page_tests
                "${ernc_threshold_case}"
                "${CMAKE_SOURCE_DIR}/catalog/parameter_catalog.json"
                "${CMAKE_SOURCE_DIR}/firmware/layout_manifest.json"
        )
    endforeach()

    add_executable(ernc_array_editor_model_tests
        array_editor_model_test.cpp
        ../src/presentation/arrays/array_editor_model.cpp
    )

    target_include_directories(ernc_array_editor_model_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src/presentation"
    )

    target_link_libraries(ernc_array_editor_model_tests PRIVATE Qt6::Core ernc_domain)

    add_test(
        NAME "array_editor.set_column_edits_only_that_index"
        COMMAND ernc_array_editor_model_tests set_column_edits_only_that_index
    )
endif()

add_executable(ernc_inspector_context_tests
    inspector_context_test.cpp
    ../src/presentation/inspector/inspector_context.cpp
)

target_include_directories(ernc_inspector_context_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

target_link_libraries(ernc_inspector_context_tests PRIVATE ernc_domain)

set(ernc_inspector_context_cases
    empty_selection
    anc_off
    alpha1_values
    step_read_only
    check_data_catalog_only
    no_sketch_values
)

foreach(ernc_inspector_context_case IN LISTS ernc_inspector_context_cases)
    add_test(
        NAME "inspector_context.${ernc_inspector_context_case}"
        COMMAND ernc_inspector_context_tests "${ernc_inspector_context_case}"
    )
endforeach()

add_executable(ernc_operation_log_tests
    operation_log_test.cpp
)

target_include_directories(ernc_operation_log_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

set(ernc_operation_log_cases
    keeps_newest_lines
)

foreach(ernc_operation_log_case IN LISTS ernc_operation_log_cases)
    add_test(
        NAME "operation_log.${ernc_operation_log_case}"
        COMMAND ernc_operation_log_tests "${ernc_operation_log_case}"
    )
endforeach()

add_executable(ernc_layout_settings_tests
    layout_settings_test.cpp
    ../src/presentation/shell/layout_settings.cpp
)

target_include_directories(ernc_layout_settings_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/presentation"
)

add_test(
    NAME "layout_settings.width_bands"
    COMMAND ernc_layout_settings_tests "width_bands"
)
add_test(
    NAME "layout_settings.presets"
    COMMAND ernc_layout_settings_tests "presets"
)
add_test(
    NAME "layout_settings.keymap_esc_does_not_cancel"
    COMMAND ernc_layout_settings_tests
        "keymap_esc_does_not_cancel"
        "${CMAKE_SOURCE_DIR}/resources/keymap/shell.txt"
)

add_executable(ernc_write_verification_tests
    write_verification_test.cpp
)

target_link_libraries(ernc_write_verification_tests PRIVATE ernc_device)

set(ernc_write_verification_cases
    compare_match
    compare_mismatch
    ack_without_readback
    ack_stops_before_read
    next_pump_verifies_with_read
    write_ack_timeout_does_not_resend
)

foreach(ernc_write_verification_case IN LISTS ernc_write_verification_cases)
    add_test(
        NAME "write_verification.${ernc_write_verification_case}"
        COMMAND ernc_write_verification_tests "${ernc_write_verification_case}"
    )
endforeach()

add_executable(ernc_cancellation_tests
    cancellation_test.cpp
)

target_link_libraries(ernc_cancellation_tests PRIVATE ernc_device)

set(ernc_cancellation_cases
    dispose_drops_stale_frame
    cancel_does_not_send_the_rest
    late_frame_after_cancel_stays_cancelled
    late_frame_does_not_update_new_session
)

foreach(ernc_cancellation_case IN LISTS ernc_cancellation_cases)
    add_test(
        NAME "cancellation.${ernc_cancellation_case}"
        COMMAND ernc_cancellation_tests "${ernc_cancellation_case}"
    )
endforeach()

add_executable(ernc_simulator_slave_tests
    simulator_slave_test.cpp
)

target_link_libraries(ernc_simulator_slave_tests PRIVATE ernc_simulator)

set(ernc_simulator_slave_cases
    read_returns_planted_bytes
    write_then_read_shows_new_bytes
    illegal_request_leaves_image
    same_request_is_deterministic
    memory_transport_polls_response
)

foreach(ernc_simulator_slave_case IN LISTS ernc_simulator_slave_cases)
    add_test(
        NAME "simulator_slave.${ernc_simulator_slave_case}"
        COMMAND ernc_simulator_slave_tests "${ernc_simulator_slave_case}"
    )
endforeach()

add_executable(ernc_simulator_fault_tests
    simulator_fault_test.cpp
)

target_link_libraries(ernc_simulator_fault_tests PRIVATE ernc_simulator)

set(ernc_simulator_fault_cases
    same_seed_same_sequence
    drop_leaves_image
    disconnect_leaves_image
    bad_crc_is_not_success
    bad_frame_is_not_success
    delay_releases_on_second_poll
)

foreach(ernc_simulator_fault_case IN LISTS ernc_simulator_fault_cases)
    add_test(
        NAME "simulator_fault.${ernc_simulator_fault_case}"
        COMMAND ernc_simulator_fault_tests "${ernc_simulator_fault_case}"
    )
endforeach()

# ES-038: spawn ernc_comm_lab. The checker links only the C++ standard library.
if(TARGET ernc_comm_lab)
    add_executable(ernc_comm_lab_tests
        comm_lab_test.cpp
    )

    set(ernc_comm_lab_cases
        help
        simulator_readonly
        allow_write_still_reads
        serial_blocked
        serial_missing_profile
        serial_empty_whitelist
        serial_missing_port
        serial_read_limit
        unknown_scenario
        missing_profile
        smoke
        bulk
        fault_injection
        serial_bulk_blocked
        iterations_zero
        iterations_101
    )

    foreach(ernc_comm_lab_case IN LISTS ernc_comm_lab_cases)
        add_test(
            NAME "comm_lab.${ernc_comm_lab_case}"
            COMMAND ernc_comm_lab_tests
                "$<TARGET_FILE:ernc_comm_lab>"
                "${ernc_comm_lab_case}"
        )
    endforeach()
endif()
