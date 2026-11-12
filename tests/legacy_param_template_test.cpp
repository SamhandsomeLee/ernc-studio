#include "ernc/application/project/legacy_param_template_bridge.hpp"
#include "ernc/application/project/parameter_snapshot.hpp"
#include "ernc/application/project/project_session.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/infrastructure/legacy_param_template.hpp"

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
using ernc::application::exportParamsWithTemplate;
using ernc::application::importParamsToSnapshot;
using ernc::application::importParamsToStaged;
using ernc::application::kLegacyParamsImportSnapshotSource;
using ernc::domain::CatalogLoad;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::infrastructure::LegacyExportTemplate;
using ernc::infrastructure::LegacyParamsLoad;
using ernc::infrastructure::LegacyTemplateLoad;
using ernc::infrastructure::loadExportTemplateFile;
using ernc::infrastructure::loadLegacyParamsFile;
using ernc::infrastructure::parseExportTemplate;
using ernc::infrastructure::parseLegacyParamsFile;

constexpr const char* kCatalogVersion = "0.2.0";
constexpr const char* kFirmwareVersion = "test-fw-es088";
constexpr const char* kProjectName = "LegacyParamsTemplateTest";

bool fail(const char* name) {
  std::cerr << "failed: " << name << '\n';
  return false;
}

QString pathToQString(const std::filesystem::path& path) {
  return QString::fromStdWString(path.native());
}

RuntimeTime at(int seconds) { return RuntimeTime{std::chrono::seconds{seconds}}; }

struct TempWorkspace {
  QTemporaryDir qtDir;
  std::filesystem::path parent;
  bool ownsFallback{false};

  TempWorkspace() {
    if (!qtDir.isValid()) {
      std::random_device device;
      parent = std::filesystem::temp_directory_path() / ("ernc-es088-proj-" + std::to_string(device()));
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

  std::filesystem::path bundlePath() const { return parent / "Legacy.erncproj"; }
};

struct TestDatabase {
  QString connectionName;
  QSqlDatabase database;

  explicit TestDatabase(const std::filesystem::path& sqlitePath, QString suffix)
      : connectionName(std::move(suffix)) {
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

bool minimalTemplateParses(const ParameterCatalog& catalog, const char* tplPath) {
  constexpr const char* name = "minimal_template_parses";
  const LegacyTemplateLoad loaded = loadExportTemplateFile(tplPath, catalog);
  if (!loaded.ok() || loaded.template_.rowLimit != 2U || loaded.template_.names.size() != 2U) {
    return fail(name);
  }
  return loaded.template_.names[0] == "ANC_OFF" && loaded.template_.names[1] == "RNC_param.alpha1";
}

bool unknownTemplateLineFails(const ParameterCatalog& catalog) {
  constexpr const char* name = "unknown_template_line_fails";
  constexpr std::string_view text = "1,\nMissing.Param,\n";
  const LegacyTemplateLoad loaded = parseExportTemplate(text, catalog);
  if (loaded.ok() || loaded.report.unknownTemplateLines.empty()) {
    return fail(name);
  }
  return loaded.report.unknownTemplateLines[0] == "Missing.Param"
         && loaded.error == ErrorCode::InvalidParameter;
}

bool exportMissingValueWritesZero(const ParameterCatalog& catalog, const LegacyExportTemplate& template_) {
  constexpr const char* name = "export_missing_value_writes_zero";
  ParameterRuntimeStore store;
  const Result<std::string> exported = exportParamsWithTemplate(catalog, store, template_);
  if (!exported.ok()) {
    return fail(name);
  }
  const LegacyParamsLoad parsed = parseLegacyParamsFile(exported.value(), template_.rowLimit);
  if (!parsed.ok() || parsed.file.values.size() != 2U) {
    return fail(name);
  }
  return parsed.file.values[0] == 0.0 && parsed.file.values[1] == 0.0;
}

bool exportStagedThenDevice(const ParameterCatalog& catalog, const LegacyExportTemplate& template_) {
  constexpr const char* name = "export_staged_then_device";
  ParameterRuntimeStore store;
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  const ParameterId alpha = ParameterId::parse("RNC_param.alpha1").value();
  store.stage(ancOff, ScalarValue::fromFloat(0.f), at(1));
  store.observe(alpha, ScalarValue::fromFloat(2.5f), at(1));
  const Result<std::string> exported = exportParamsWithTemplate(catalog, store, template_);
  if (!exported.ok()) {
    return fail(name);
  }
  const LegacyParamsLoad parsed = parseLegacyParamsFile(exported.value(), template_.rowLimit);
  return parsed.ok() && parsed.file.values[0] == 0.0 && parsed.file.values[1] == 2.5;
}

bool importToStagedCase(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                        const LegacyExportTemplate& template_, const char* paramsPath) {
  constexpr const char* name = "import_to_staged";
  const LegacyParamsLoad loaded = loadLegacyParamsFile(paramsPath, template_.rowLimit);
  if (!loaded.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  const Result<ernc::infrastructure::LegacyParamJsonReport> applied =
      importParamsToStaged(catalog, layout, store, template_, loaded.file, at(2));
  if (!applied.ok() || applied.value().appliedCount != 2U) {
    return fail(name);
  }
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  const ParameterId alpha = ParameterId::parse("RNC_param.alpha1").value();
  const auto* anc = store.find(ancOff);
  const auto* alphaState = store.find(alpha);
  if (anc == nullptr || alphaState == nullptr || !anc->stagedValue || !alphaState->stagedValue) {
    return fail(name);
  }
  const Result<float> ancVal = anc->stagedValue->asFloat();
  const Result<float> alphaVal = alphaState->stagedValue->asFloat();
  return ancVal.ok() && alphaVal.ok() && ancVal.value() == 1.f && alphaVal.value() == 1.5f;
}

bool importDoesNotWriteDevice(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                              const LegacyExportTemplate& template_, const char* paramsPath) {
  constexpr const char* name = "import_does_not_write_device";
  const LegacyParamsLoad loaded = loadLegacyParamsFile(paramsPath, template_.rowLimit);
  if (!loaded.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  if (!importParamsToStaged(catalog, layout, store, template_, loaded.file, at(1)).ok()) {
    return fail(name);
  }
  for (const std::string& entryName : template_.names) {
    const Result<ParameterId> id = ParameterId::parse(entryName);
    if (!id.ok()) {
      continue;
    }
    const auto* state = store.find(id.value());
    if (state != nullptr && state->deviceValue.has_value()) {
      return fail(name);
    }
  }
  return true;
}

bool paramsRowCountMismatchFails(const LegacyExportTemplate& template_) {
  constexpr const char* name = "params_row_count_mismatch_fails";
  constexpr std::string_view text = "1,\n";
  const LegacyParamsLoad loaded = parseLegacyParamsFile(text, template_.rowLimit);
  return !loaded.ok() && loaded.error == ErrorCode::InvalidParameter;
}

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

bool importToSnapshotPersists(const ParameterCatalog& catalog, const LegacyExportTemplate& template_,
                              const char* paramsPath) {
  constexpr const char* name = "import_to_snapshot_persists";
  const LegacyParamsLoad loaded = loadLegacyParamsFile(paramsPath, template_.rowLimit);
  if (!loaded.ok()) {
    return fail(name);
  }
  TempWorkspace workspace;
  const Result<std::int64_t> projectId = openProject(workspace);
  if (!projectId.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  TestDatabase connection(workspace.bundlePath() / "project.sqlite", QStringLiteral("es088_import_snap"));
  ParameterSnapshotService service(connection.database);
  CaptureSnapshotRequest request;
  request.projectId = projectId.value();
  request.firmwareVersion = kFirmwareVersion;
  request.catalogVersion = catalog.catalogVersion();
  const Result<std::int64_t> snapshotId =
      importParamsToSnapshot(service, request, catalog, store, template_, loaded.file);
  if (!snapshotId.ok()) {
    return fail(name);
  }
  QSqlQuery sourceQuery(connection.database);
  sourceQuery.prepare(QStringLiteral("SELECT source FROM parameter_snapshot WHERE id = :id"));
  sourceQuery.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  if (!sourceQuery.exec() || !sourceQuery.next()) {
    return fail(name);
  }
  const QString source = sourceQuery.value(0).toString();
  if (source != QString::fromUtf8(kLegacyParamsImportSnapshotSource.data(),
                                  static_cast<int>(kLegacyParamsImportSnapshotSource.size()))) {
    return fail(name);
  }
  QSqlQuery rowQuery(connection.database);
  rowQuery.prepare(QStringLiteral(
      "SELECT value_json FROM parameter_value WHERE snapshot_id = :id AND parameter_id = :pid"));
  rowQuery.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  rowQuery.bindValue(QStringLiteral(":pid"), QStringLiteral("ANC_OFF"));
  if (!rowQuery.exec() || !rowQuery.next()) {
    return fail(name);
  }
  return rowQuery.value(0).toString().toFloat() == 1.f;
}

bool runCase(std::string_view caseName, const ParameterCatalog& catalog, const FirmwareLayout& layout,
             const LegacyExportTemplate& template_, const char* tplPath, const char* paramsPath) {
  if (caseName == "minimal_template_parses") {
    return minimalTemplateParses(catalog, tplPath);
  }
  if (caseName == "unknown_template_line_fails") {
    return unknownTemplateLineFails(catalog);
  }
  if (caseName == "export_missing_value_writes_zero") {
    return exportMissingValueWritesZero(catalog, template_);
  }
  if (caseName == "export_staged_then_device") {
    return exportStagedThenDevice(catalog, template_);
  }
  if (caseName == "import_to_staged") {
    return importToStagedCase(catalog, layout, template_, paramsPath);
  }
  if (caseName == "import_does_not_write_device") {
    return importDoesNotWriteDevice(catalog, layout, template_, paramsPath);
  }
  if (caseName == "params_row_count_mismatch_fails") {
    return paramsRowCountMismatchFails(template_);
  }
  if (caseName == "import_to_snapshot_persists") {
    return importToSnapshotPersists(catalog, template_, paramsPath);
  }
  std::cerr << "unknown case: " << caseName << '\n';
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr || argv[4] == nullptr
      || argv[5] == nullptr) {
    std::cerr << "usage: ernc_legacy_param_template_tests <case> <catalog.json> <layout_manifest.json> "
                 "<template.tpl> <params.params>\n";
    return 2;
  }
  QCoreApplication application(argc, argv);
  const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadFile(argv[2]);
  const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadFile(argv[3]);
  if (!loadedCatalog.ok() || !loadedLayout.ok()) {
    std::cerr << "catalog or layout load failed\n";
    return 2;
  }
  const LegacyTemplateLoad loadedTemplate = loadExportTemplateFile(argv[4], loadedCatalog.catalog());
  if (!loadedTemplate.ok() && std::string_view(argv[1]) != "unknown_template_line_fails") {
    std::cerr << "template load failed\n";
    return 2;
  }
  const LegacyExportTemplate templateDoc =
      loadedTemplate.ok() ? loadedTemplate.template_ : LegacyExportTemplate{};
  return runCase(argv[1], loadedCatalog.catalog(), loadedLayout.layout(), templateDoc, argv[4], argv[5]) ? 0 : 1;
}
