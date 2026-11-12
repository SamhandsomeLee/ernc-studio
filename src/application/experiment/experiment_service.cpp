#include "ernc/application/experiment/experiment_service.hpp"

#include "ernc/infrastructure/persistence/experiment_asset_store.hpp"
#include "ernc/infrastructure/persistence/experiment_repository.hpp"

#include <QDateTime>

#include <string>

namespace ernc::application {
namespace {

using ernc::domain::DataAssetSummary;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::infrastructure::ExperimentAssetStore;
using ernc::infrastructure::ExperimentRepository;
using ernc::infrastructure::kBinAssetFormat;
using ernc::infrastructure::kRegionBinAssetKind;

}  // namespace

std::string ExperimentService::isoUtcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}

ExperimentService::ExperimentService(QSqlDatabase& database, std::filesystem::path projectRoot)
    : database_(database), projectRoot_(std::move(projectRoot)) {}

Result<std::int64_t> ExperimentService::createExperiment(CreateExperimentRequest request) {
    if (request.projectId <= 0 || request.name.empty()) {
        return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
    }
    ExperimentRepository repository(database_);
    return repository.insertExperiment(request.projectId, request.name, isoUtcNow(), request.testCondition,
                                       request.parameterSnapshotId, request.note);
}

Result<std::int64_t> ExperimentService::attachRegionBin(AttachRegionBinRequest request) {
    if (request.experimentId <= 0 || request.fileName.empty()) {
        return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
    }
    const Result<std::string> relative = ExperimentAssetStore::relativeBinPath(request.experimentId, request.fileName);
    if (!relative.ok()) {
        return Result<std::int64_t>::failure(relative.error());
    }
    const std::string sha = ExperimentAssetStore::sha256Hex(request.bytes);
    const std::int64_t byteSize = static_cast<std::int64_t>(request.bytes.size());

    const Result<void> written =
        ExperimentAssetStore::writeBinAtomic(projectRoot_, relative.value(), request.bytes);
    if (!written.ok()) {
        return Result<std::int64_t>::failure(written.error());
    }

    if (!database_.isOpen()) {
        std::error_code ec;
        std::filesystem::remove(projectRoot_ / relative.value(), ec);
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }
    if (!database_.transaction()) {
        std::error_code ec;
        std::filesystem::remove(projectRoot_ / relative.value(), ec);
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }

    std::optional<std::string_view> metadata;
    std::string metadataJson;
    if (request.regionPath && !request.regionPath->empty()) {
        metadataJson = std::string(R"({"region_path":")");
        metadataJson.append(request.regionPath->data(), request.regionPath->size());
        metadataJson.append(R"("})");
        metadata = metadataJson;
    }

    ExperimentRepository repository(database_);
    const Result<std::int64_t> assetId =
        repository.insertDataAsset(request.experimentId, kRegionBinAssetKind, relative.value(), kBinAssetFormat,
                                   byteSize, sha, std::nullopt, metadata);
    if (!assetId.ok()) {
        database_.rollback();
        std::error_code ec;
        std::filesystem::remove(projectRoot_ / relative.value(), ec);
        return assetId;
    }
    if (!database_.commit()) {
        database_.rollback();
        std::error_code ec;
        std::filesystem::remove(projectRoot_ / relative.value(), ec);
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }
    return assetId;
}

Result<void> ExperimentService::verifyAsset(std::int64_t assetId) {
    if (assetId <= 0) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    ExperimentRepository repository(database_);
    const Result<DataAssetSummary> asset = repository.loadDataAsset(assetId);
    if (!asset.ok()) {
        return Result<void>::failure(asset.error());
    }
    return ExperimentAssetStore::verifySha256(projectRoot_, asset.value().relativePath, asset.value().sha256,
                                              asset.value().byteSize);
}

Result<std::vector<DataAssetSummary>> ExperimentService::listAssets(std::int64_t experimentId) {
    if (experimentId <= 0) {
        return Result<std::vector<DataAssetSummary>>::failure(ErrorCode::InvalidParameter);
    }
    ExperimentRepository repository(database_);
    return repository.listDataAssets(experimentId);
}

}  // namespace ernc::application
