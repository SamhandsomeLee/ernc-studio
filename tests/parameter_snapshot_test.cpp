#include "ernc/application/project/parameter_snapshot.hpp"
#include "ernc/application/project/project_session.hpp"
#include "ernc/domain/catalog.hpp"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QString>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {

using ernc::application::CaptureSnapshotRequest;
using ernc::application::ParameterSnapshotService;
using ernc::application::ProjectSession;
using ernc::application::kRuntimeDeviceReadSource;
using ernc::domain::CatalogLoad;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::SnapshotDiff;

constexpr const char* kCatalogVersion = "0.2.0";
constexpr const char* kFirmwareVersion = "test-fw-1";
constexpr const char* kProjectName = "SnapshotTest";

/// @brief 报告当前用例失败。
bool fail(const char* name) {
  std::cerr << "failed: " << name << '\n';
  return false;
}

QString pathToQString(const std::filesystem::path& path) {
  return QString::fromStdWString(path.native());
}

struct TempWorkspace {
  QTemporaryDir qtDir;
  std::filesystem::path parent;

  TempWorkspace() {
    if (!qtDir.isValid()) {
      std::random_device device;
      parent = std::filesystem::temp_directory_path()
          / ("ernc-es085-" + std::to_string(device()));
      std::filesystem::create_directories(parent);
      ownsFallback = true;
    } else {
      parent = std::filesystem::path(qtDir.path().toStdWString());
    }
  }

  ~TempWorkspace() {
    if (ownsFallback) {
      std::error_code ignored;
      std::filesystem::remove_all(parent, ignored);
    }
  }

  std::filesystem::path bundlePath() const { return parent / "Sample.erncproj"; }

 private:
  bool ownsFallback{false};
};

struct TestDatabase {
  QString connectionName;
  QSqlDatabase database;

  explicit TestDatabase(const std::filesystem::path& sqlitePath, QString suffix) : connectionName(std::move(suffix)) {
    database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(pathToQString(sqlitePath));
    database.open();
  }

  ~TestDatabase() {
    if (database.isValid()) {
      database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
  }
};

RuntimeTime at(int seconds) { return RuntimeTime{std::chrono::seconds{seconds}}; }

/// @brief 创建工程并返回 projectId。
Result<std::int64_t> openProject(TempWorkspace& workspace) {
  ProjectSession session;
  const Result created = session.create(workspace.bundlePath(), kProjectName, kCatalogVersion);
  if (!created.ok()) {
    return Result<std::int64_t>::failure(created.error());
  }
  const std::int64_t projectId = created.value().projectId;
  if (!session.close().ok()) {
    return Result<std::int64_t>::failure(ErrorCode::DatabaseFailed);
  }
  return Result<std::int64_t>::success(projectId);
}

bool snapshotSourceMatches(const QSqlDatabase& database, std::int64_t snapshotId) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("SELECT source FROM parameter_snapshot WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId));
  if (!query.exec() || !query.next()) {
    return false;
  }
  return query.value(0).toString() == QString::fromUtf8(kRuntimeDeviceReadSource.data(),
                                                      static_cast<int>(kRuntimeDeviceReadSource.size()));
}

bool capturePersistsScalarValues(const ParameterCatalog& catalog) {
  TempWorkspace workspace;
  const Result<std::int64_t> projectId = openProject(workspace);
  if (!projectId.ok()) {
    return fail("capture_persists_scalar_values");
  }
  ParameterRuntimeStore store;
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  const ParameterId step = ParameterId::parse("Step").value();
  store.observe(ancOff, ScalarValue::fromFloat(0.f), at(1));
  store.stage(step, ScalarValue::fromInt32(3), at(2));

  TestDatabase connection(workspace.bundlePath() / "project.sqlite", QStringLiteral("es085_capture"));
  if (!connection.database.isOpen()) {
    return fail("capture_persists_scalar_values");
  }

  ParameterSnapshotService service(connection.database);
  CaptureSnapshotRequest request;
  request.projectId = projectId.value();
  request.firmwareVersion = kFirmwareVersion;
  request.catalogVersion = catalog.catalogVersion();
  const Result<std::int64_t> snapshotId = service.capture(request, catalog, store);
  if (!snapshotId.ok() || !snapshotSourceMatches(connection.database, snapshotId.value())) {
    return fail("capture_persists_scalar_values");
  }

  QSqlQuery countQuery(connection.database);
  countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM parameter_value WHERE snapshot_id = :id"));
  countQuery.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  if (!countQuery.exec() || !countQuery.next() || countQuery.value(0).toInt() != 1) {
    return fail("capture_persists_scalar_values");
  }

  QSqlQuery rowQuery(connection.database);
  rowQuery.prepare(QStringLiteral(
      "SELECT parameter_id, data_type, value_json, validity FROM parameter_value WHERE snapshot_id = :id"));
  rowQuery.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  if (!rowQuery.exec() || !rowQuery.next()) {
    return fail("capture_persists_scalar_values");
  }
  if (rowQuery.value(0).toString() != QStringLiteral("ANC_OFF")
      || rowQuery.value(1).toString() != QStringLiteral("float32")
      || rowQuery.value(3).toString() != QStringLiteral("observed")) {
    return fail("capture_persists_scalar_values");
  }
  return rowQuery.value(2).toString().toFloat() == 0.f;
}

bool diffAgainstStoreByParameterId(const ParameterCatalog& catalog) {
  TempWorkspace workspace;
  const Result<std::int64_t> projectId = openProject(workspace);
  if (!projectId.ok()) {
    return fail("diff_against_store_by_parameter_id");
  }
  ParameterRuntimeStore store;
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  store.observe(ancOff, ScalarValue::fromFloat(1.f), at(3));

  TestDatabase connection(workspace.bundlePath() / "project.sqlite", QStringLiteral("es085_diff"));
  ParameterSnapshotService service(connection.database);
  CaptureSnapshotRequest request;
  request.projectId = projectId.value();
  request.firmwareVersion = kFirmwareVersion;
  request.catalogVersion = catalog.catalogVersion();
  const Result<std::int64_t> snapshotId = service.capture(request, catalog, store);
  if (!snapshotId.ok()) {
    return fail("diff_against_store_by_parameter_id");
  }

  store.observe(ancOff, ScalarValue::fromFloat(2.f), at(4));
  const Result<SnapshotDiff> diff = service.diffAgainstStore(snapshotId.value(), store);
  if (!diff.ok() || diff.value().entries.size() != 1U) {
    return fail("diff_against_store_by_parameter_id");
  }
  const auto& entry = diff.value().entries.front();
  if (entry.parameterId != ancOff || !entry.snapshotValue || !entry.storeValue) {
    return fail("diff_against_store_by_parameter_id");
  }
  const Result<float> snap = entry.snapshotValue->asFloat();
  const Result<float> live = entry.storeValue->asFloat();
  return snap.ok() && live.ok() && snap.value() == 1.f && live.value() == 2.f;
}

bool hydrateStoreDoesNotMarkDirty(const ParameterCatalog& catalog) {
  TempWorkspace workspace;
  const Result<std::int64_t> projectId = openProject(workspace);
  if (!projectId.ok()) {
    return fail("hydrate_store_does_not_mark_dirty");
  }

  ParameterRuntimeStore captureStore;
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  captureStore.observe(ancOff, ScalarValue::fromFloat(5.f), at(10));

  TestDatabase connection(workspace.bundlePath() / "project.sqlite", QStringLiteral("es085_hydrate"));
  ParameterSnapshotService service(connection.database);
  CaptureSnapshotRequest request;
  request.projectId = projectId.value();
  request.firmwareVersion = kFirmwareVersion;
  request.catalogVersion = catalog.catalogVersion();
  const Result<std::int64_t> snapshotId = service.capture(request, catalog, captureStore);
  if (!snapshotId.ok()) {
    return fail("hydrate_store_does_not_mark_dirty");
  }

  ParameterRuntimeStore viewStore;
  viewStore.observe(ancOff, ScalarValue::fromFloat(9.f), at(11));
  viewStore.stage(ancOff, ScalarValue::fromFloat(7.f), at(12));

  const Result<void> hydrated =
      service.hydrateStoreForView(snapshotId.value(), catalog, viewStore, at(20));
  if (!hydrated.ok()) {
    return fail("hydrate_store_does_not_mark_dirty");
  }
  const auto* state = viewStore.find(ancOff);
  if (state == nullptr || !state->deviceValue || !state->stagedValue || !state->dirty || !state->stagedAt
      || *state->stagedAt != at(12)) {
    return fail("hydrate_store_does_not_mark_dirty");
  }
  const Result<float> device = state->deviceValue->asFloat();
  const Result<float> staged = state->stagedValue->asFloat();
  return device.ok() && staged.ok() && device.value() == 5.f && staged.value() == 7.f;
}

bool emptyStoreCaptureZeroRows(const ParameterCatalog& catalog) {
  TempWorkspace workspace;
  const Result<std::int64_t> projectId = openProject(workspace);
  if (!projectId.ok()) {
    return fail("empty_store_capture_fails_or_zero_rows");
  }
  ParameterRuntimeStore store;
  TestDatabase connection(workspace.bundlePath() / "project.sqlite", QStringLiteral("es085_empty"));
  ParameterSnapshotService service(connection.database);
  CaptureSnapshotRequest request;
  request.projectId = projectId.value();
  request.firmwareVersion = kFirmwareVersion;
  request.catalogVersion = catalog.catalogVersion();
  const Result<std::int64_t> snapshotId = service.capture(request, catalog, store);
  if (!snapshotId.ok()) {
    return fail("empty_store_capture_fails_or_zero_rows");
  }
  QSqlQuery query(connection.database);
  query.prepare(QStringLiteral("SELECT COUNT(*) FROM parameter_value WHERE snapshot_id = :id"));
  query.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  return query.exec() && query.next() && query.value(0).toInt() == 0;
}

bool runCase(std::string_view name, const ParameterCatalog& catalog) {
  if (name == "capture_persists_scalar_values") {
    return capturePersistsScalarValues(catalog);
  }
  if (name == "diff_against_store_by_parameter_id") {
    return diffAgainstStoreByParameterId(catalog);
  }
  if (name == "hydrate_store_does_not_mark_dirty") {
    return hydrateStoreDoesNotMarkDirty(catalog);
  }
  if (name == "empty_store_capture_fails_or_zero_rows") {
    return emptyStoreCaptureZeroRows(catalog);
  }
  std::cerr << "unknown case: " << name << '\n';
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
    std::cerr << "usage: ernc_parameter_snapshot_tests <case> <catalog.json>\n";
    return 2;
  }
  const std::string caseName = argv[1];
  QCoreApplication application(argc, argv);
  const CatalogLoad loaded = ParameterCatalogLoader::loadFile(argv[2]);
  if (!loaded.ok()) {
    std::cerr << "catalog load failed\n";
    return 2;
  }
  return runCase(caseName, loaded.catalog()) ? 0 : 1;
}
