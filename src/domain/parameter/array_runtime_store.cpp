#include "ernc/domain/array_runtime_store.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {

ArrayState& ArrayRuntimeStore::ensure(const ParameterId& id, const std::size_t count, const ScalarValue::Kind kind) {
    const auto found = records_.find(id.text());
    if (found != records_.end()) {
        return found->second.state;
    }
    Record record{ArrayState{count, kind}, ArrayRuntimeMeta{}};
    auto inserted = records_.emplace(id.text(), std::move(record));
    return inserted.first->second.state;
}

const ArrayState* ArrayRuntimeStore::find(const ParameterId& id) const noexcept {
    const auto found = records_.find(id.text());
    if (found == records_.end()) {
        return nullptr;
    }
    return &found->second.state;
}

const ArrayRuntimeMeta* ArrayRuntimeStore::meta(const ParameterId& id) const noexcept {
    const auto found = records_.find(id.text());
    if (found == records_.end()) {
        return nullptr;
    }
    return &found->second.meta;
}

Result<void> ArrayRuntimeStore::observeRead(const ParameterId& id, const ScalarValue::Kind kind,
                                            const std::vector<ScalarValue>& elements, const RuntimeTime readAt) {
    ArrayState& state = ensure(id, elements.size(), kind);
    if (state.size() != elements.size()) {
        return Result<void>::failure(ErrorCode::PayloadSizeMismatch);
    }
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const Result<void> applied = state.setRead(index, elements[index]);
        if (!applied.ok()) {
            return applied;
        }
    }
    ArrayRuntimeMeta& meta = records_.at(id.text()).meta;
    meta.stale = false;
    meta.readAt = readAt;
    meta.error.reset();
    return Result<void>::success();
}

void ArrayRuntimeStore::failObservation(const ParameterId& id, const ErrorCode error) {
    ensure(id, 0, ScalarValue::Kind::Float32);
    records_.at(id.text()).meta.error = error;
}

std::size_t ArrayRuntimeStore::dirtyCount(const ParameterId& id) const noexcept {
    const ArrayState* state = find(id);
    if (state == nullptr) {
        return 0;
    }
    std::size_t count = 0;
    for (std::size_t index = 0; index < state->size(); ++index) {
        const ArrayElement* element = state->element(index);
        if (element != nullptr && element->dirty) {
            ++count;
        }
    }
    return count;
}

bool ArrayRuntimeStore::hasAnyRead(const ParameterId& id) const noexcept {
    const ArrayState* state = find(id);
    if (state == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < state->size(); ++index) {
        const ArrayElement* element = state->element(index);
        if (element != nullptr && element->read) {
            return true;
        }
    }
    return false;
}

}  // namespace ernc::domain
