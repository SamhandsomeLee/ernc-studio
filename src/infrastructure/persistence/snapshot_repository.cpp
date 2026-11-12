#include "ernc/infrastructure/persistence/snapshot_repository.hpp"

#include <QSqlQuery>
#include <QString>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::Result;

QString viewToQString(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

SnapshotRepository::SnapshotRepository(QSqlDatabase& database) : database_(database) {}

Result<std::int64_t> SnapshotRepository::insertSnapshot(std::int64_t projectId, std::string_view createdAt,
                                                          std::string_view source, std::string_view firmwareVersion,
                                                          std::string_view catalogVersion,
                                                          std::optional<std::string_view> note) {
  if (projectId <= 0 || createdAt.empty() || source.empty() || firmwareVersion.empty() || catalogVersion.empty()) {
    return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
  }
  if (!database_.isOpen()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  QSqlQuery query(database_);
  if (note && !note->empty()) {
    query.prepare(QStringLiteral(
        "INSERT INTO parameter_snapshot "
        "(project_id, created_at, source, firmware_version, catalog_version, note) "
        "VALUES (:project, :created, :source, :firmware, :catalog, :note)"));
    query.bindValue(QStringLiteral(":note"), viewToQString(*note));
  } else {
    query.prepare(QStringLiteral(
        "INSERT INTO parameter_snapshot "
        "(project_id, created_at, source, firmware_version, catalog_version, note) "
        "VALUES (:project, :created, :source, :firmware, :catalog, NULL)"));
  }
  query.bindValue(QStringLiteral(":project"), static_cast<qlonglong>(projectId));
  query.bindValue(QStringLiteral(":created"), viewToQString(createdAt));
  query.bindValue(QStringLiteral(":source"), viewToQString(source));
  query.bindValue(QStringLiteral(":firmware"), viewToQString(firmwareVersion));
  query.bindValue(QStringLiteral(":catalog"), viewToQString(catalogVersion));
  if (!query.exec()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  return Result<std::int64_t>::success(query.lastInsertId().toLongLong());
}

Result<void> SnapshotRepository::insertParameterValue(std::int64_t snapshotId, const ParameterId& parameterId,
                                                      std::string_view dataType, std::string_view valueJson,
                                                      std::string_view validity) {
  if (snapshotId <= 0 || dataType.empty() || valueJson.empty() || validity.empty()) {
    return Result<void>::failure(ErrorCode::InvalidParameter);
  }
  if (!database_.isOpen()) {
    return Result<void>::failure(ErrorCode::DatabaseFailed);
  }
  QSqlQuery query(database_);
  query.prepare(QStringLiteral(
      "INSERT INTO parameter_value "
      "(snapshot_id, parameter_id, data_type, value_json, raw_payload, validity) "
      "VALUES (:snapshot, :parameter, :type, :json, NULL, :validity)"));
  query.bindValue(QStringLiteral(":snapshot"), static_cast<qlonglong>(snapshotId));
  query.bindValue(QStringLiteral(":parameter"), QString::fromStdString(parameterId.text()));
  query.bindValue(QStringLiteral(":type"), viewToQString(dataType));
  query.bindValue(QStringLiteral(":json"), viewToQString(valueJson));
  query.bindValue(QStringLiteral(":validity"), viewToQString(validity));
  if (!query.exec()) {
    return Result<void>::failure(ErrorCode::DatabaseFailed);
  }
  return Result<void>::success();
}

Result<std::vector<PersistedParameterValue>> SnapshotRepository::loadParameterValues(std::int64_t snapshotId) {
  if (snapshotId <= 0) {
    return Result<std::vector<PersistedParameterValue>>::failure(ErrorCode::InvalidParameter);
  }
  if (!database_.isOpen()) {
    return Result<std::vector<PersistedParameterValue>>::failure(ErrorCode::DatabaseFailed);
  }
  QSqlQuery query(database_);
  query.prepare(QStringLiteral(
      "SELECT parameter_id, data_type, value_json, validity "
      "FROM parameter_value WHERE snapshot_id = :snapshot ORDER BY parameter_id"));
  query.bindValue(QStringLiteral(":snapshot"), static_cast<qlonglong>(snapshotId));
  if (!query.exec()) {
    return Result<std::vector<PersistedParameterValue>>::failure(ErrorCode::DatabaseFailed);
  }
  std::vector<PersistedParameterValue> values;
  while (query.next()) {
    const QString parameterText = query.value(0).toString();
    const Result<ParameterId> parsed = ParameterId::parse(parameterText.toStdString());
    if (!parsed.ok()) {
      return Result<std::vector<PersistedParameterValue>>::failure(ErrorCode::DatabaseFailed);
    }
    values.push_back(PersistedParameterValue{
        parsed.value(),
        query.value(1).toString().toStdString(),
        query.value(2).toString().toStdString(),
        query.value(3).toString().toStdString(),
    });
  }
  return Result<std::vector<PersistedParameterValue>>::success(std::move(values));
}

Result<std::int64_t> SnapshotRepository::countParameterValues(std::int64_t snapshotId) {
  if (snapshotId <= 0) {
    return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
  }
  if (!database_.isOpen()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  QSqlQuery query(database_);
  query.prepare(QStringLiteral("SELECT COUNT(*) FROM parameter_value WHERE snapshot_id = :snapshot"));
  query.bindValue(QStringLiteral(":snapshot"), static_cast<qlonglong>(snapshotId));
  if (!query.exec() || !query.next()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  return Result<std::int64_t>::success(query.value(0).toLongLong());
}

}  // namespace ernc::infrastructure
