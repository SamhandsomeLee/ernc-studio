#pragma once

#include "ernc/domain/experiment_types.hpp"
#include "ernc/domain/types.hpp"

#include <QSqlDatabase>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ernc::application {

/// 创建实验时的元数据。
struct CreateExperimentRequest {
    std::int64_t projectId{};
    std::string_view name;
    std::optional<std::string_view> testCondition;
    std::optional<std::int64_t> parameterSnapshotId;
    std::optional<std::string_view> note;
};

/// 将 Region BIN 字节挂到实验上。
struct AttachRegionBinRequest {
    std::int64_t experimentId{};
    std::string_view fileName;
    std::span<const std::uint8_t> bytes;
    std::optional<std::string_view> regionPath;
};

/// 实验记录与工程内 BIN 资产的编排。不访问串口。
class ExperimentService {
public:
    /// @param database 已打开的 `project.sqlite`。
    /// @param projectRoot `.erncproj` 目录包根路径。
    ExperimentService(QSqlDatabase& database, std::filesystem::path projectRoot);

    /// @brief 插入实验行。
    ernc::domain::Result<std::int64_t> createExperiment(CreateExperimentRequest request);

    /// @brief 原子写 BIN 并登记 `data_asset`。
    ernc::domain::Result<std::int64_t> attachRegionBin(AttachRegionBinRequest request);

    /// @brief 按库内路径与哈希核对磁盘文件。
    ernc::domain::Result<void> verifyAsset(std::int64_t assetId);

    /// @brief 列出实验资产。
    ernc::domain::Result<std::vector<ernc::domain::DataAssetSummary>> listAssets(std::int64_t experimentId);

private:
    static std::string isoUtcNow();

    QSqlDatabase& database_;
    std::filesystem::path projectRoot_;
};

}  // namespace ernc::application
