#include "ernc/domain/check_data_codec.hpp"

#include "region_bytes.hpp"

#include <array>
#include <string_view>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// check_data 的 20 个子字段。顺序与布局 children 一致，不表示单位。
constexpr std::array<std::string_view, 20> kChildren{
    "check_data.OutputCheck",
    "check_data.RefCheck",
    "check_data.ErrCheck",
    "check_data.ENCRefCheckSin",
    "check_data.ENCRefCheckCos",
    "check_data.ENCEngineSpeedCheck",
    "check_data.ENCErrCheck",
    "check_data.ENCOutputCheck",
    "check_data.HornRefCheck_T",
    "check_data.HornRefCheck_F",
    "check_data.xPowCheck",
    "check_data.MiuCheck",
    "check_data.ENCEchoGainCheck",
    "check_data.OrderSpkoutmaxCheck",
    "check_data.DataSpeedCheck",
    "check_data.DataSinCheck",
    "check_data.DataCosCheck",
    "check_data.RNCAdapCheckRefNum",
    "check_data.RNCAdapCheckSpkNum",
    "check_data.RNCAdapCheck",
};

/// @brief 路径是不是这 20 个子字段之一。
/// @param path 字段路径，只在本次调用期间借用。
/// @return 在名单里时为 true。
bool isChild(std::string_view path) noexcept {
    return detail::indexOf(path, kChildren) != kChildren.size();
}

}  // namespace

Result<DecodedField> decodeCheckDataField(const SubRegion& field, std::span<const std::uint8_t> bytes) {
    if (!isChild(field.path)) {
        return Result<DecodedField>::failure(ErrorCode::InvalidParameter);
    }
    return detail::decodeCheckedField(field, bytes);
}

Result<DecodedRegion> decodeCheckData(const RegionDefinition& region, std::span<const std::uint8_t> bytes) {
    return detail::decodeCheckedRegion(region, bytes, "check_data", kChildren);
}

}  // namespace ernc::domain
