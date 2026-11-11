#include "ernc/domain/analysis/dataset.hpp"

#include <cstdint>
#include <limits>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// @brief 检查 shape 能否正好盖住这些 float。
/// @param shape 各级长度。空表示一个没有数组维度的 float。
/// @param sampleCount values 的个数。
/// @return 空 shape 且正好 1 个值，或每一维都大于 0 且乘积等于 sampleCount 时为 true。
/// 某一维为 0，或乘积超出 64 位时为 false，避免截断后少算样本。
bool shapeCoversSamples(const std::vector<std::uint32_t>& shape, std::size_t sampleCount) noexcept {
    if (shape.empty()) {
        return sampleCount == 1U;
    }
    std::uint64_t product = 1U;
    for (const std::uint32_t dimension : shape) {
        if (dimension == 0U || product > std::numeric_limits<std::uint64_t>::max() / dimension) {
            return false;
        }
        product *= dimension;
    }
    return product == static_cast<std::uint64_t>(sampleCount);
}

}  // namespace

Result<AnalysisDataset> AnalysisDataset::fromDecoded(const DecodedField& field) {
    if (!shapeCoversSamples(field.shape, field.values.size())) {
        return Result<AnalysisDataset>::failure(ErrorCode::InvalidParameter);
    }
    return Result<AnalysisDataset>::success(AnalysisDataset{field.path, field.shape, field.values});
}

AnalysisDataset::AnalysisDataset(std::string path, std::vector<std::uint32_t> shape, std::vector<float> values)
    : path_{std::move(path)}, shape_{std::move(shape)}, values_{std::move(values)} {}

const std::string& AnalysisDataset::path() const noexcept {
    return path_;
}

const std::vector<std::uint32_t>& AnalysisDataset::shape() const noexcept {
    return shape_;
}

std::size_t AnalysisDataset::sampleCount() const noexcept {
    return values_.size();
}

Result<float> AnalysisDataset::valueAt(std::size_t storageIndex) const {
    if (storageIndex >= values_.size()) {
        return Result<float>::failure(ErrorCode::InvalidParameter);
    }
    return Result<float>::success(values_[storageIndex]);
}

}  // namespace ernc::domain
