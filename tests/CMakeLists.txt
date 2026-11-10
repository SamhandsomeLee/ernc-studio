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

foreach(ernc_layout_case IN LISTS ernc_layout_cases)
    add_test(
        NAME "firmware_layout.${ernc_layout_case}"
        COMMAND ernc_firmware_layout_tests "${ernc_layout_case}" "${ernc_layout_manifest}"
    )
endforeach()
