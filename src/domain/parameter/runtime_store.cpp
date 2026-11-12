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
    state.projectReadValue.reset();
}

bool ParameterRuntimeStore::restoreProjectRead(const ParameterId& id, const ScalarValue& value) {
    ParameterRuntimeState& state = slot(id);
    if (state.deviceValue && !state.stale) {
        return false;
    }
    state.projectReadValue = value;
    return true;
}

void ParameterRuntimeStore::clearProjectReads() noexcept {
    for (auto& entry : records_) {
        entry.second.projectReadValue.reset();
    }
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

void ParameterRuntimeStore::markClean(const ParameterId& id) noexcept {
    const auto found = records_.find(id.text());
    if (found == records_.end()) {
        return;
    }
    found->second.dirty = false;
}

void ParameterRuntimeStore::markOffline() noexcept {
    for (auto& entry : records_) {
        entry.second.stale = true;
    }
}

void ParameterRuntimeStore::clearStaged() noexcept {
    for (auto& [id, state] : records_) {
        state.stagedValue.reset();
        state.stagedAt.reset();
        state.dirty = false;
    }
}

std::size_t ParameterRuntimeStore::size() const noexcept {
    return records_.size();
}

}  // namespace ernc::domain
