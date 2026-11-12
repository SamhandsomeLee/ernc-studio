#include "ernc/domain/array_runtime_store.hpp"

#include "ernc/domain/types.hpp"

#include <chrono>
#include <iostream>

namespace {

using ernc::domain::ArrayRuntimeStore;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool observe_and_dirty() {
    ArrayRuntimeStore store;
    const Result<ParameterId> id = ParameterId::parse("SpkOrderAmpThre");
    if (!id.ok()) {
        return fail("parse-id");
    }
    std::vector<ScalarValue> values{ScalarValue::fromFloat(1.0F), ScalarValue::fromFloat(2.0F),
                                    ScalarValue::fromFloat(3.0F)};
    const RuntimeTime now = std::chrono::system_clock::now();
    const Result<void> observed = store.observeRead(id.value(), ScalarValue::Kind::Float32, values, now);
    if (!observed.ok() || store.meta(id.value()) == nullptr || store.meta(id.value())->stale) {
        return fail("observe");
    }
    ernc::domain::ArrayState& state = store.ensure(id.value(), 3, ScalarValue::Kind::Float32);
    if (!state.setValue(1, ScalarValue::fromFloat(9.0F)).ok() || store.dirtyCount(id.value()) != 1U) {
        return fail("dirty");
    }
    return true;
}

}  // namespace

int main() {
    if (!observe_and_dirty()) {
        return 1;
    }
    std::cout << "array runtime store tests passed\n";
    return 0;
}
