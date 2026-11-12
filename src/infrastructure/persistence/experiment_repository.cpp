#include "ernc/infrastructure/persistence/experiment_repository.hpp"

#include <QSqlQuery>
#include <QString>
#include <QVariant>

namespace ernc::infrastructure {
namespace {

using ernc::domain::DataAssetSummary;
using ernc::domain::ErrorCode;
using ernc::domain::Result;

QString viewToQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

std::optional<std::string> optionalString(const QVariant& value) {
    if (value.isNull()) {
        return std::nullopt;
    }
    return value.toString().toStdString();
}

}  // namespace

ExperimentRepository::ExperimentRepository(QSqlDatabase& database) : database_(database) {}

Result<std::int64_t> ExperimentRepository::insertExperiment(
    std::int64_t projectId, std::string_view name, std::string_view startedAt,
    std::optional<std::string_view> testCondition, std::optional<std::int64_t> parameterSnapshotId,
    std::optional<std::string_view> note) {
    if (projectId <= 0 || name.empty() || startedAt.empty()) {
        return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
    }
    if (!database_.isOpen()) {
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO experiment "
        "(project_id, name, started_at, ended_at, test_condition, parameter_snapshot_id, note) "
        "VALUES (:project, :name, :started, NULL, :condition, :snapshot, :note)"));
    query.bindValue(QStringLiteral(":project"), static_cast<qlonglong>(projectId));
    query.bindValue(QStringLiteral(":name"), viewToQString(name));
    query.bindValue(QStringLiteral(":started"), viewToQString(startedAt));
    if (testCondition && !testCondition->empty()) {
        query.bindValue(QStringLiteral(":condition"), viewToQString(*testCondition));
    } else {
        query.bindValue(QStringLiteral(":condition"), QVariant());
    }
    if (parameterSnapshotId && *parameterSnapshotId > 0) {
        query.bindValue(QStringLiteral(":snapshot"), static_cast<qlonglong>(*parameterSnapshotId));
    } else {
        query.bindValue(QStringLiteral(":snapshot"), QVariant());
    }
    if (note && !note->empty()) {
        query.bindValue(QStringLiteral(":note"), viewToQString(*note));
    } else {
        query.bindValue(QStringLiteral(":note"), QVariant());
    }
    if (!query.exec()) {
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }
    return Result<std::int64_t>::success(query.lastInsertId().toLongLong());
}

Result<std::int64_t> ExperimentRepository::insertDataAsset(
    std::int64_t experimentId, std::string_view assetKind, std::string_view relativePath,
    std::string_view format, std::int64_t byteSize, std::string_view sha256,
    std::optional<std::string_view> dimensionsJson, std::optional<std::string_view> metadataJson) {
    if (experimentId <= 0 || assetKind.empty() || relativePath.empty() || format.empty() || byteSize < 0
        || sha256.empty()) {
        return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
    }
    if (!database_.isOpen()) {
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO data_asset "
        "(experiment_id, asset_kind, relative_path, format, byte_size, sha256, dimensions_json, metadata_json) "
        "VALUES (:experiment, :kind, :path, :format, :size, :sha, :dims, :meta)"));
    query.bindValue(QStringLiteral(":experiment"), static_cast<qlonglong>(experimentId));
    query.bindValue(QStringLiteral(":kind"), viewToQString(assetKind));
    query.bindValue(QStringLiteral(":path"), viewToQString(relativePath));
    query.bindValue(QStringLiteral(":format"), viewToQString(format));
    query.bindValue(QStringLiteral(":size"), static_cast<qlonglong>(byteSize));
    query.bindValue(QStringLiteral(":sha"), viewToQString(sha256));
    if (dimensionsJson && !dimensionsJson->empty()) {
        query.bindValue(QStringLiteral(":dims"), viewToQString(*dimensionsJson));
    } else {
        query.bindValue(QStringLiteral(":dims"), QVariant());
    }
    if (metadataJson && !metadataJson->empty()) {
        query.bindValue(QStringLiteral(":meta"), viewToQString(*metadataJson));
    } else {
        query.bindValue(QStringLiteral(":meta"), QVariant());
    }
    if (!query.exec()) {
        return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
    }
    return Result<std::int64_t>::success(query.lastInsertId().toLongLong());
}

Result<DataAssetSummary> ExperimentRepository::loadDataAsset(std::int64_t assetId) {
    if (assetId <= 0) {
        return Result<DataAssetSummary>::failure(ErrorCode::InvalidParameter);
    }
    if (!database_.isOpen()) {
        return Result<DataAssetSummary>::failure(ErrorCode::DatabaseFailed);
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, experiment_id, asset_kind, relative_path, format, byte_size, sha256, dimensions_json, metadata_json "
        "FROM data_asset WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(assetId));
    if (!query.exec() || !query.next()) {
        return Result<DataAssetSummary>::failure(ErrorCode::DatabaseFailed);
    }
    DataAssetSummary summary;
    summary.assetId = query.value(0).toLongLong();
    summary.experimentId = query.value(1).toLongLong();
    summary.assetKind = query.value(2).toString().toStdString();
    summary.relativePath = query.value(3).toString().toStdString();
    summary.format = query.value(4).toString().toStdString();
    summary.byteSize = query.value(5).toLongLong();
    summary.sha256 = query.value(6).toString().toStdString();
    summary.dimensionsJson = optionalString(query.value(7));
    summary.metadataJson = optionalString(query.value(8));
    return Result<DataAssetSummary>::success(std::move(summary));
}

Result<std::vector<DataAssetSummary>> ExperimentRepository::listDataAssets(std::int64_t experimentId) {
    if (experimentId <= 0) {
        return Result<std::vector<DataAssetSummary>>::failure(ErrorCode::InvalidParameter);
    }
    if (!database_.isOpen()) {
        return Result<std::vector<DataAssetSummary>>::failure(ErrorCode::DatabaseFailed);
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, experiment_id, asset_kind, relative_path, format, byte_size, sha256, dimensions_json, metadata_json "
        "FROM data_asset WHERE experiment_id = :experiment ORDER BY id"));
    query.bindValue(QStringLiteral(":experiment"), static_cast<qlonglong>(experimentId));
    if (!query.exec()) {
        return Result<std::vector<DataAssetSummary>>::failure(ErrorCode::DatabaseFailed);
    }
    std::vector<DataAssetSummary> assets;
    while (query.next()) {
        DataAssetSummary summary;
        summary.assetId = query.value(0).toLongLong();
        summary.experimentId = query.value(1).toLongLong();
        summary.assetKind = query.value(2).toString().toStdString();
        summary.relativePath = query.value(3).toString().toStdString();
        summary.format = query.value(4).toString().toStdString();
        summary.byteSize = query.value(5).toLongLong();
        summary.sha256 = query.value(6).toString().toStdString();
        summary.dimensionsJson = optionalString(query.value(7));
        summary.metadataJson = optionalString(query.value(8));
        assets.push_back(std::move(summary));
    }
    return Result<std::vector<DataAssetSummary>>::success(std::move(assets));
}

}  // namespace ernc::infrastructure
