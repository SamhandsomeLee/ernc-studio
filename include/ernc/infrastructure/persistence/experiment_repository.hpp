#pragma once

#include "ernc/domain/experiment_types.hpp"
#include "ernc/domain/types.hpp"

#include <QSqlDatabase>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {

/// `experiment` 与 `data_asset` 表的 SQLite 访问。不打开连接。
class ExperimentRepository {
public:
    /// @param database 已打开的 `project.sqlite` 连接。
    explicit ExperimentRepository(QSqlDatabase& database);

    /// @brief 插入实验记录。
    ernc::domain::Result<std::int64_t> insertExperiment(
        std::int64_t projectId, std::string_view name, std::string_view startedAt,
        std::optional<std::string_view> testCondition, std::optional<std::int64_t> parameterSnapshotId,
        std::optional<std::string_view> note);

    /// @brief 插入数据资产元数据；`relative_path` 须为工程内相对路径。
    ernc::domain::Result<std::int64_t> insertDataAsset(
        std::int64_t experimentId, std::string_view assetKind, std::string_view relativePath,
        std::string_view format, std::int64_t byteSize, std::string_view sha256,
        std::optional<std::string_view> dimensionsJson, std::optional<std::string_view> metadataJson);

    /// @brief 按 id 读取资产。
    ernc::domain::Result<ernc::domain::DataAssetSummary> loadDataAsset(std::int64_t assetId);

    /// @brief 某实验下的全部资产。
    ernc::domain::Result<std::vector<ernc::domain::DataAssetSummary>> listDataAssets(std::int64_t experimentId);

private:
    QSqlDatabase& database_;
};

}  // namespace ernc::infrastructure
