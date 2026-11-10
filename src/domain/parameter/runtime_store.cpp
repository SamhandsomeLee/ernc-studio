#include "ernc/domain/runtime_store.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {

const ParameterRuntimeState* ParameterRuntimeStore::find(const ParameterId& id) const noexcept {
    const auto found = records_.find(id.text());
    if (found == records_.end()) {
        return nullptr;
    }
    return &found->second;
}

ParameterRuntimeState& ParameterRuntimeStore::slot(const ParameterId& id) {
    return records_[id.text()];
}

void ParameterRuntimeStore::observe(const ParameterId& id, const ScalarValue& value, RuntimeTime readAt) {
    ParameterRuntimeState& state = slot(id);
    state.deviceValue = value;
    state.readAt = readAt;
    state.stale = false;
    state.error.reset();
}

void ParameterRuntimeStore::failObservation(const ParameterId& id, ErrorCode error) {
    slot(id).error = error;
}

void ParameterRuntimeStore::stage(const ParameterId& id, const ScalarValue& value, RuntimeTime stagedAt) {
    ParameterRuntimeState& state = slot(id);
    state.stagedValue = value;
    state.stagedAt = stagedAt;
    state.dirty = true;
}

void ParameterRuntimeStore::markOffline() noexcept {
    for (auto& entry : records_) {
        entry.second.stale = true;
    }
}

std::size_t ParameterRuntimeStore::size() const noexcept {
    return records_.size();
}

}  // namespace ernc::domain
