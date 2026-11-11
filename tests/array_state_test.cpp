#include "ernc/domain/array_state.hpp"

#include <iostream>
#include <string_view>

namespace {

using ernc::domain::ArrayElement;
using ernc::domain::ArrayState;
using ernc::domain::ErrorCode;
using ernc::domain::ScalarValue;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool editOneIndexKeepsOtherRead() {
    ArrayState state{10, ScalarValue::Kind::Float32};
    if (!state.setRead(0, ScalarValue::fromFloat(0.25F)).ok() || !state.setRead(2, ScalarValue::fromFloat(1.5F)).ok()
        || !state.setValue(2, ScalarValue::fromFloat(4.0F)).ok()) {
        return fail("edit_one_index_keeps_other_read");
    }
    const ArrayElement* first = state.element(0);
    const ArrayElement* edited = state.element(2);
    if (first == nullptr || edited == nullptr || !first->read || first->set || first->dirty || !edited->read || !edited->set
        || !edited->dirty) {
        return fail("edit_one_index_keeps_other_read");
    }
    const auto firstRead = first->read->asFloat();
    const auto editedRead = edited->read->asFloat();
    const auto editedSet = edited->set->asFloat();
    if (!firstRead.ok() || firstRead.value() != 0.25F || !editedRead.ok() || editedRead.value() != 1.5F || !editedSet.ok()
        || editedSet.value() != 4.0F) {
        return fail("edit_one_index_keeps_other_read");
    }
    if (!state.setRead(2, ScalarValue::fromFloat(1.5F)).ok() || !edited->set || edited->set->asFloat().value() != 4.0F || !edited->dirty) {
        return fail("edit_one_index_keeps_other_read");
    }
    return true;
}

bool resetCopiesReadIntoSet() {
    ArrayState state{10, ScalarValue::Kind::Float32};
    if (!state.setRead(2, ScalarValue::fromFloat(1.5F)).ok() || !state.setValue(2, ScalarValue::fromFloat(4.0F)).ok()) {
        return fail("reset_copies_read_into_set");
    }
    state.setValue(1, ScalarValue::fromFloat(9.0F));
    state.reset();
    const ArrayElement* edited = state.element(2);
    const ArrayElement* cleared = state.element(1);
    if (edited == nullptr || cleared == nullptr || !edited->set || edited->dirty || cleared->set || cleared->dirty) {
        return fail("reset_copies_read_into_set");
    }
    const auto set = edited->set->asFloat();
    if (!set.ok() || set.value() != 1.5F) {
        return fail("reset_copies_read_into_set");
    }
    return true;
}

bool markCleanKeepsValues() {
    ArrayState state{2, ScalarValue::Kind::Float32};
    if (!state.setRead(0, ScalarValue::fromFloat(1.0F)).ok() || !state.setValue(0, ScalarValue::fromFloat(4.0F)).ok()
        || !state.setValue(1, ScalarValue::fromFloat(5.0F)).ok()) {
        return fail("mark_clean_keeps_values");
    }
    const auto outOfRange = state.markClean(2);
    const ArrayElement* kept = state.element(0);
    if (outOfRange.ok() || kept == nullptr || !kept->dirty || !kept->read || !kept->set) {
        return fail("mark_clean_keeps_values");
    }
    if (!state.markClean(0).ok()) {
        return fail("mark_clean_keeps_values");
    }
    const ArrayElement* cleaned = state.element(0);
    const ArrayElement* other = state.element(1);
    if (cleaned == nullptr || other == nullptr || cleaned->dirty || !other->dirty || !cleaned->set || !other->set) {
        return fail("mark_clean_keeps_values");
    }
    const auto read = cleaned->read->asFloat();
    const auto set = cleaned->set->asFloat();
    const auto otherSet = other->set->asFloat();
    if (!read.ok() || read.value() != 1.0F || !set.ok() || set.value() != 4.0F || !otherSet.ok() || otherSet.value() != 5.0F) {
        return fail("mark_clean_keeps_values");
    }
    return true;
}

bool typeOrIndexMismatchIsRejected() {
    ArrayState state{10, ScalarValue::Kind::Float32};
    if (!state.setRead(0, ScalarValue::fromFloat(1.0F)).ok()) {
        return fail("type_or_index_mismatch_is_rejected");
    }
    const auto wrongType = state.setValue(0, ScalarValue::fromUInt32(1U));
    const auto pastEnd = state.setRead(10, ScalarValue::fromFloat(2.0F));
    const ArrayElement* first = state.element(0);
    if (wrongType.ok() || pastEnd.ok() || wrongType.error() != ErrorCode::InvalidParameter || pastEnd.error() != ErrorCode::InvalidParameter
        || first == nullptr || first->set || first->dirty || state.element(10) != nullptr) {
        return fail("type_or_index_mismatch_is_rejected");
    }
    const auto read = first->read->asFloat();
    if (!read.ok() || read.value() != 1.0F) {
        return fail("type_or_index_mismatch_is_rejected");
    }
    return true;
}

bool runCase(std::string_view name) {
    if (name == "edit_one_index_keeps_other_read") {
        return editOneIndexKeepsOtherRead();
    }
    if (name == "reset_copies_read_into_set") {
        return resetCopiesReadIntoSet();
    }
    if (name == "type_or_index_mismatch_is_rejected") {
        return typeOrIndexMismatchIsRejected();
    }
    if (name == "mark_clean_keeps_values") {
        return markCleanKeepsValues();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_array_state_tests <case>\n";
        return 2;
    }
    return runCase(argv[1]) ? 0 : 1;
}
