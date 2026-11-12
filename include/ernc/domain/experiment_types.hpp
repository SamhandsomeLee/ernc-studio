#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ernc::domain {

/// 持久化后的实验摘要。
struct ExperimentSummary {
    std::int64_t experimentId{};
    std::int64_t projectId{};
    std::string name;
    std::string startedAt;
    std::optional<std::string> endedAt;
    std::optional<std::string> testCondition;
    std::optional<std::int64_t> parameterSnapshotId;
    std::optional<std::string> note;
};

/// 持久化后的数据资产行。
struct DataAssetSummary {
    std::int64_t assetId{};
    std::int64_t experimentId{};
    std::string assetKind;
    std::string relativePath;
    std::string format;
    std::int64_t byteSize{};
    std::string sha256;
    std::optional<std::string> dimensionsJson;
    std::optional<std::string> metadataJson;

    friend bool operator==(const DataAssetSummary& left, const DataAssetSummary& right) {
        return left.assetId == right.assetId && left.experimentId == right.experimentId
            && left.assetKind == right.assetKind && left.relativePath == right.relativePath
            && left.format == right.format && left.byteSize == right.byteSize && left.sha256 == right.sha256
            && left.dimensionsJson == right.dimensionsJson && left.metadataJson == right.metadataJson;
    }
};

}  // namespace ernc::domain
