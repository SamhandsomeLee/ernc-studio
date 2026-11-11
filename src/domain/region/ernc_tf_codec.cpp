#include "ernc/domain/ernc_tf_codec.hpp"

#include "region_bytes.hpp"

#include <array>
#include <string_view>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// ERNC_TF 的四个子字段。顺序不表示轴名称。
constexpr std::array<std::string_view, 4> kChildren{
    "ERNC_TF.fSecPathTransR",
    "ERNC_TF.fSecPathTransI",
    "ERNC_TF.SpecRadius",
    "ERNC_TF.ENCTF",
};

/// @brief 路径是不是这四个子字段之一。
/// @param path 字段路径，只在本次调用期间借用。
/// @return 在名单里时为 true。
bool isChild(std::string_view path) noexcept {
    return detail::indexOf(path, kChildren) != kChildren.size();
}

}  // namespace

Result<DecodedField> decodeErncTfField(const SubRegion& field, std::span<const std::uint8_t> bytes) {
    if (!isChild(field.path)) {
        return Result<DecodedField>::failure(ErrorCode::InvalidParameter);
    }
    return detail::decodeCheckedField(field, bytes);
}

Result<DecodedRegion> decodeErncTf(const RegionDefinition& region, std::span<const std::uint8_t> bytes) {
    return detail::decodeCheckedRegion(region, bytes, "ERNC_TF", kChildren);
}

}  // namespace ernc::domain
