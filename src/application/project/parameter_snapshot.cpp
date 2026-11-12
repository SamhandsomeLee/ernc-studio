#include "ernc/application/project/parameter_snapshot.hpp"

#include "ernc/infrastructure/persistence/snapshot_repository.hpp"

#include <QDateTime>
#include <QJsonValue>

#include <map>
#include <string>

namespace ernc::application {
namespace {

using ernc::domain::DataType;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterDefinition;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::SnapshotDiff;
using ernc::domain::SnapshotParameterDiff;
using ernc::domain::ValueShape;
using ernc::infrastructure::PersistedParameterValue;
using ernc::infrastructure::SnapshotRepository;

std::string isoUtcNow() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}

std::optional<std::string_view> dataTypeToken(DataType type) {
  switch (type) {
    case DataType::Float32:
      return std::string_view{"float32"};
    case DataType::Int32:
      return std::string_view{"int32"};
    case DataType::UInt32:
      return std::string_view{"uint32"};
  }
  return std::nullopt;
}

Result<std::string> scalarToJson(const ScalarValue& value, DataType dataType) {
  switch (dataType) {
    case DataType::Float32: {
      const Result<float> held = value.asFloat();
      if (!held.ok()) {
        return Result<std::string>::failure(ErrorCode::InvalidParameter);
      }
      return Result<std::string>::success(
          QString::number(static_cast<double>(held.value()), 'g', 9).toStdString());
    }
    case DataType::Int32: {
      const Result<std::int32_t> held = value.asInt32();
      if (!held.ok()) {
        return Result<std::string>::failure(ErrorCode::InvalidParameter);
      }
      return Result<std::string>::success(QJsonValue(static_cast<qint64>(held.value())).toString().toStdString());
    }
    case DataType::UInt32: {
      const Result<std::uint32_t> held = value.asUInt32();
      if (!held.ok()) {
        return Result<std::string>::failure(ErrorCode::InvalidParameter);
      }
      return Result<std::string>::success(
          QString::number(static_cast<qulonglong>(held.value())).toStdString());
    }
  }
  return Result<std::string>::failure(ErrorCode::InvalidParameter);
}

Result<ScalarValue> scalarFromJson(std::string_view json, std::string_view dataType) {
  bool ok = false;
  const double number = QString::fromUtf8(json.data(), static_cast<int>(json.size())).toDouble(&ok);
  if (!ok) {
    return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
  }
  if (dataType == "float32") {
    return Result<ScalarValue>::success(ScalarValue::fromFloat(static_cast<float>(number)));
  }
  if (dataType == "int32") {
    return Result<ScalarValue>::success(ScalarValue::fromInt32(static_cast<std::int32_t>(number)));
  }
  if (dataType == "uint32") {
    return Result<ScalarValue>::success(ScalarValue::fromUInt32(static_cast<std::uint32_t>(number)));
  }
  return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
}

bool isScalarDefinition(const ParameterDefinition& definition) {
  return definition.shape.has_value() && *definition.shape == ValueShape::Scalar && definition.dataType.has_value();
}

bool valuesEqual(const ScalarValue& left, const ScalarValue& right) {
  if (left.kind() != right.kind()) {
    return false;
  }
  switch (left.kind()) {
    case ScalarValue::Kind::Float32: {
      const Result<float> a = left.asFloat();
      const Result<float> b = right.asFloat();
      return a.ok() && b.ok() && a.value() == b.value();
    }
    case ScalarValue::Kind::Int32: {
      const Result<std::int32_t> a = left.asInt32();
      const Result<std::int32_t> b = right.asInt32();
      return a.ok() && b.ok() && a.value() == b.value();
    }
    case ScalarValue::Kind::UInt32: {
      const Result<std::uint32_t> a = left.asUInt32();
      const Result<std::uint32_t> b = right.asUInt32();
      return a.ok() && b.ok() && a.value() == b.value();
    }
  }
  return false;
}

}  // namespace

ParameterSnapshotService::ParameterSnapshotService(QSqlDatabase& database) : database_(database) {}

Result<std::int64_t> ParameterSnapshotService::capture(const CaptureSnapshotRequest& request,
                                                       const ParameterCatalog& catalog,
                                                       const ParameterRuntimeStore& store) {
  if (request.projectId <= 0 || request.firmwareVersion.empty() || request.catalogVersion.empty()) {
    return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
  }
  if (!database_.isOpen()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  if (!database_.transaction()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  SnapshotRepository repository(database_);
  const Result<std::int64_t> snapshotId = repository.insertSnapshot(
      request.projectId, isoUtcNow(), kRuntimeDeviceReadSource, request.firmwareVersion, request.catalogVersion,
      request.note);
  if (!snapshotId.ok()) {
    database_.rollback();
    return snapshotId;
  }
  for (const ParameterDefinition& definition : catalog.entries()) {
    if (!isScalarDefinition(definition)) {
      continue;
    }
    const auto* state = store.find(definition.id);
    if (state == nullptr || !state->deviceValue) {
      continue;
    }
    const std::optional<std::string_view> typeToken = dataTypeToken(*definition.dataType);
    if (!typeToken) {
      continue;
    }
    const Result<std::string> json = scalarToJson(*state->deviceValue, *definition.dataType);
    if (!json.ok()) {
      return Result<std::int64_t>::failure(json.error());
    }
    const std::string_view validity = state->stale ? std::string_view{"stale"} : std::string_view{"observed"};
    const Result<void> inserted = repository.insertParameterValue(snapshotId.value(), definition.id, *typeToken,
                                                                  json.value(), validity);
    if (!inserted.ok()) {
      database_.rollback();
      return Result<std::int64_t>::failure(inserted.error());
    }
  }
  if (!database_.commit()) {
    database_.rollback();
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  return snapshotId;
}

Result<void> ParameterSnapshotService::hydrateStoreForView(std::int64_t snapshotId, const ParameterCatalog& catalog,
                                                           ParameterRuntimeStore& store, RuntimeTime observedAt) {
  if (snapshotId <= 0) {
    return Result<void>::failure(ErrorCode::InvalidParameter);
  }
  SnapshotRepository repository(database_);
  const Result<std::vector<PersistedParameterValue>> loaded = repository.loadParameterValues(snapshotId);
  if (!loaded.ok()) {
    return Result<void>::failure(loaded.error());
  }
  for (const PersistedParameterValue& row : loaded.value()) {
    const ParameterDefinition* definition = catalog.find(row.parameterId.text());
    if (definition == nullptr || !isScalarDefinition(*definition)) {
      continue;
    }
    const Result<ScalarValue> decoded = scalarFromJson(row.valueJson, row.dataType);
    if (!decoded.ok()) {
      return Result<void>::failure(decoded.error());
    }
    store.observe(row.parameterId, decoded.value(), observedAt);
  }
  return Result<void>::success();
}

Result<SnapshotDiff> ParameterSnapshotService::diffAgainstStore(std::int64_t snapshotId,
                                                                const ParameterRuntimeStore& store) {
  if (snapshotId <= 0) {
    return Result<SnapshotDiff>::failure(ErrorCode::InvalidParameter);
  }
  SnapshotRepository repository(database_);
  const Result<std::vector<PersistedParameterValue>> loaded = repository.loadParameterValues(snapshotId);
  if (!loaded.ok()) {
    return Result<SnapshotDiff>::failure(loaded.error());
  }
  std::map<std::string, std::optional<ScalarValue>> snapshotValues;
  for (const PersistedParameterValue& row : loaded.value()) {
    const Result<ScalarValue> decoded = scalarFromJson(row.valueJson, row.dataType);
    if (!decoded.ok()) {
      return Result<SnapshotDiff>::failure(decoded.error());
    }
    snapshotValues.emplace(row.parameterId.text(), decoded.value());
  }
  SnapshotDiff diff;
  for (const auto& [parameterText, snapshotValue] : snapshotValues) {
    std::optional<ScalarValue> storeValue;
    const Result<ParameterId> id = ParameterId::parse(parameterText);
    if (!id.ok() || !snapshotValue) {
      continue;
    }
    const auto* state = store.find(id.value());
    if (state != nullptr && state->deviceValue) {
      storeValue = *state->deviceValue;
    }
    if (storeValue && valuesEqual(*snapshotValue, *storeValue)) {
      continue;
    }
    diff.entries.push_back(SnapshotParameterDiff{
        id.value(),
        snapshotValue,
        storeValue,
    });
  }
  return Result<SnapshotDiff>::success(std::move(diff));
}

}  // namespace ernc::application
