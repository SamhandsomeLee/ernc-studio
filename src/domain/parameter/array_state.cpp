#include "ernc/domain/array_state.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {

ArrayState::ArrayState(std::size_t count, ScalarValue::Kind kind)
    : kind_{kind}, elements_(count) {}

std::size_t ArrayState::size() const noexcept {
    return elements_.size();
}

ScalarValue::Kind ArrayState::kind() const noexcept {
    return kind_;
}

Result<void> ArrayState::setRead(std::size_t index, const ScalarValue& value) {
    if (index >= elements_.size() || value.kind() != kind_) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    elements_[index].read = value;
    return Result<void>::success();
}

Result<void> ArrayState::setValue(std::size_t index, const ScalarValue& value) {
    if (index >= elements_.size() || value.kind() != kind_) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    elements_[index].set = value;
    elements_[index].dirty = true;
    return Result<void>::success();
}

void ArrayState::reset() {
    for (ArrayElement& element : elements_) {
        if (element.read) {
            element.set = element.read;
        } else {
            element.set.reset();
        }
        element.dirty = false;
    }
}

const ArrayElement* ArrayState::element(std::size_t index) const noexcept {
    if (index >= elements_.size()) {
        return nullptr;
    }
    return &elements_[index];
}

}  // namespace ernc::domain
