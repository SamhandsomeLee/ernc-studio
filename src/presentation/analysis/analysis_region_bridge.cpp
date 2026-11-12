#include "analysis/analysis_region_bridge.hpp"

#include "analysis/analysis_panel.hpp"

#include "ernc/domain/analysis/dataset.hpp"
#include "ernc/domain/check_data_codec.hpp"
#include "ernc/domain/ernc_tf_codec.hpp"

#include <string_view>

namespace ernc::presentation::AnalysisRegionBridge {
namespace {

using ernc::domain::AnalysisDataset;
using ernc::domain::DecodedField;
using ernc::domain::DecodedRegion;
using ernc::domain::ErrorCode;
using ernc::domain::RegionCatalog;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;
using ernc::domain::SubRegion;

/// @brief 把区域定义收成子字段描述，供单字段解码用。
/// @param region 当前定义，只借用。
/// @return 字段几何与 region 一致。
SubRegion subRegionFrom(const RegionDefinition& region) {
    SubRegion sub;
    sub.path = region.path;
    sub.slot = region.slot;
    sub.slotCount = region.slotCount;
    sub.size = region.size;
    sub.offset = region.offset;
    sub.typeName = region.typeName;
    sub.scalarType = region.scalarType;
    sub.shape = region.shape;
    sub.evidence = region.evidence;
    sub.writable = region.writable;
    return sub;
}

/// @brief 父区域路径是不是 check_data。
/// @param path 完整路径，只借用。
bool isCheckDataRoot(std::string_view path) noexcept {
    return path == "check_data";
}

/// @brief 父区域路径是不是 ERNC_TF。
/// @param path 完整路径，只借用。
bool isErncTfRoot(std::string_view path) noexcept {
    return path == "ERNC_TF";
}

/// @brief 路径是不是谱半径区域本身。
/// @param path 完整路径，只借用。
bool isSpecRadiusRegion(std::string_view path) noexcept {
    return path == "ERNC_TF.SpecRadius";
}

/// @brief 从一条解码字段构造数据集并交给分析页。
/// @param field 已解码字段，只借用。
/// @param panel 分析页，只借用。
/// @return fromDecoded 或 addDataset 失败时为对应错误。
Result<void> pushField(const DecodedField& field, AnalysisPanel& panel) {
    const Result<AnalysisDataset> built = AnalysisDataset::fromDecoded(field);
    if (!built.ok() || built.tryValue() == nullptr) {
        return Result<void>::failure(built.ok() ? ErrorCode::Unknown : built.error());
    }
    return panel.ingestField(built.value());
}

/// @brief 解码整块 check_data 并更新分析页。
/// @param region 父区域定义，只借用。
/// @param bytes 区域字节，只借用。
/// @param panel 分析页，只借用。
Result<void> applyCheckDataRegion(const RegionDefinition& region, std::span<const std::uint8_t> bytes, AnalysisPanel& panel) {
    const Result<DecodedRegion> decoded = ernc::domain::decodeCheckData(region, bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return Result<void>::failure(decoded.ok() ? ErrorCode::Unknown : decoded.error());
    }
    for (const DecodedField& field : decoded.value().fields) {
        const Result<void> pushed = pushField(field, panel);
        if (!pushed.ok()) {
            return pushed;
        }
    }
    return Result<void>::success();
}

/// @brief 解码单条 check_data 子字段。
/// @param field 子字段定义，只借用。
/// @param bytes 字段字节，只借用。
/// @param panel 分析页，只借用。
Result<void> applyCheckDataField(const SubRegion& field, std::span<const std::uint8_t> bytes, AnalysisPanel& panel) {
    const Result<DecodedField> decoded = ernc::domain::decodeCheckDataField(field, bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return Result<void>::failure(decoded.ok() ? ErrorCode::Unknown : decoded.error());
    }
    return pushField(decoded.value(), panel);
}

/// @brief 解码整块 ERNC_TF 并更新分析页。
/// @param region 父区域定义，只借用。
/// @param bytes 区域字节，只借用。
/// @param panel 分析页，只借用。
Result<void> applyErncTfRegion(const RegionDefinition& region, std::span<const std::uint8_t> bytes, AnalysisPanel& panel) {
    const Result<DecodedRegion> decoded = ernc::domain::decodeErncTf(region, bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return Result<void>::failure(decoded.ok() ? ErrorCode::Unknown : decoded.error());
    }
    for (const DecodedField& field : decoded.value().fields) {
        const Result<void> pushed = pushField(field, panel);
        if (!pushed.ok()) {
            return pushed;
        }
    }
    return Result<void>::success();
}

/// @brief 解码单条 ERNC_TF 子字段或独立谱半径区域。
/// @param field 子字段定义，只借用。
/// @param bytes 字段字节，只借用。
/// @param panel 分析页，只借用。
Result<void> applyErncTfField(const SubRegion& field, std::span<const std::uint8_t> bytes, AnalysisPanel& panel) {
    const Result<DecodedField> decoded = ernc::domain::decodeErncTfField(field, bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return Result<void>::failure(decoded.ok() ? ErrorCode::Unknown : decoded.error());
    }
    return pushField(decoded.value(), panel);
}

}  // namespace

Result<void> applyBytes(const RegionCatalog& catalog, const RegionDefinition& region, std::span<const std::uint8_t> bytes,
                        AnalysisPanel& panel) {
    if (region.size != bytes.size()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    panel.prepareIngest(region.path);
    const std::string_view path{region.path};
    if (isCheckDataRoot(path)) {
        return applyCheckDataRegion(region, bytes, panel);
    }
    if (path.rfind("check_data.", 0) == 0) {
        return applyCheckDataField(subRegionFrom(region), bytes, panel);
    }
    if (isErncTfRoot(path)) {
        return applyErncTfRegion(region, bytes, panel);
    }
    if (isSpecRadiusRegion(path) || path.rfind("ERNC_TF.", 0) == 0) {
        (void)catalog;
        return applyErncTfField(subRegionFrom(region), bytes, panel);
    }
    return Result<void>::failure(ErrorCode::InvalidParameter);
}

}  // namespace ernc::presentation::AnalysisRegionBridge
