#include "ernc/application/project/legacy_param_json_bridge.hpp"
#include "ernc/application/project/parameter_snapshot.hpp"
#include "ernc/application/project/project_session.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QString>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {

using ernc::application::CaptureSnapshotRequest;
using ernc::application::ParameterSnapshotService;
using ernc::application::ProjectSession;
using ernc::application::captureReadParameters;
using ernc::application::exportStagedThenDevice;
using ernc::application::importToSnapshot;
using ernc::application::importToStaged;
using ernc::application::kLegacyJsonImportSnapshotSource;
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
using ernc::infrastructure::LegacyParamJsonDocument;
using ernc::infrastructure::LegacyParamJsonLoad;
using ernc::infrastructure::kLegacyParamJsonVersion;
using ernc::infrastructure::loadLegacyParamJsonFile;
using ernc::infrastructure::parseLegacyParamJson;
using ernc::infrastructure::serializeLegacyParamJson;

constexpr const char* kCatalogVersion = "0.2.0";
constexpr const char* kFirmwareVersion = "test-fw-es087";
constexpr const char* kProjectName = "LegacyParamJsonTest";

/// @brief 报告当前用例失败。
bool fail(const char* name) {
  std::cerr << "failed: " << name << '\n';
  return false;
}

QString pathToQString(const std::filesystem::path& path) {
  return QString::fromStdWString(path.native());
}

RuntimeTime at(int seconds) { return RuntimeTime{std::chrono::seconds{seconds}}; }

/// @brief 用完就删掉的临时目录。
struct TempDir {
  std::filesystem::path dir;

  TempDir() {
    std::random_device device;
    dir = std::filesystem::temp_directory_path() / ("ernc-es087-" + std::to_string(device()));
    std::filesystem::create_directories(dir);
  }

  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
  }
};

struct TempWorkspace {
  QTemporaryDir qtDir;
  std::filesystem::path parent;

  TempWorkspace() {
    if (!qtDir.isValid()) {
      std::random_device device;
      parent = std::filesystem::temp_directory_path() / ("ernc-es087-proj-" + std::to_string(device()));
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

 private:
  bool ownsFallback{false};
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


/// @brief 在参数列表里按名称找已解析标量。
const ernc::infrastructure::LegacyParamJsonEntry* findParam(const LegacyParamJsonDocument& document,
                                                            std::string_view name) {
  for (const auto& entry : document.params) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

/// @brief BEHAVIOR-MATRIX 最小夹具可以解析，两个标量值正确。
bool minimalFixtureParses(const char* fixturePath) {
  constexpr const char* name = "minimal_fixture_parses";
  const LegacyParamJsonLoad loaded = loadLegacyParamJsonFile(fixturePath);
  if (!loaded.ok() || loaded.document.version != kLegacyParamJsonVersion) {
    return fail(name);
  }
  if (loaded.document.params.size() != 2U || loaded.report.appliedCount != 0U) {
    return fail(name);
  }
  const auto* anc = findParam(loaded.document, "ANC_OFF");
  const auto* alpha = findParam(loaded.document, "RNC_param.alpha1");
  if (anc == nullptr || alpha == nullptr) {
    return fail(name);
  }
  const Result<float> ancFloat = anc->value.asFloat();
  const Result<float> alphaFloat = alpha->value.asFloat();
  return ancFloat.ok() && alphaFloat.ok() && ancFloat.value() == 0.f && alphaFloat.value() == 1.5f;
}

/// @brief 未知顶层字段和未知 name 进入报告；合法项仍能解析。
bool parseRejectsAndReports(const ParameterCatalog& catalog) {
  constexpr const char* name = "parse_rejects_and_reports";
  constexpr std::string_view text = R"({
    "version": "1.0.0",
    "note": "extra",
    "params": [
      { "name": "ANC_OFF", "value": 1 },
      { "no_name": 0 },
      { "name": "Missing.Param", "value": 2 }
    ]
  })";
  const LegacyParamJsonLoad loaded = parseLegacyParamJson(text, &catalog);
  if (!loaded.ok()) {
    return fail(name);
  }
  if (loaded.report.unrecognizedTopLevelFields.size() != 1U
      || loaded.report.unrecognizedTopLevelFields[0] != "note") {
    return fail(name);
  }
  if (loaded.report.unknownNames.size() != 1U || loaded.report.unknownNames[0] != "Missing.Param") {
    return fail(name);
  }
  if (loaded.report.malformedEntries.empty()) {
    return fail(name);
  }
  const auto* anc = findParam(loaded.document, "ANC_OFF");
  if (anc == nullptr) {
    return fail(name);
  }
  const Result<float> value = anc->value.asFloat();
  return value.ok() && value.value() == 1.f;
}

/// @brief JSON 数组值和区域名跳过并写入 skippedNames。
bool arraysAndBlobSkippedWithReport(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
  constexpr const char* name = "arrays_and_blob_skipped_with_report";
  constexpr std::string_view text = R"({
    "version": "1.0.0",
    "params": [
      { "name": "ANC_OFF", "value": 0 },
      { "name": "Can_Msg", "value": [1, 2, 3] },
      { "name": "check_data", "value": 0 }
    ]
  })";
  const LegacyParamJsonLoad loaded = parseLegacyParamJson(text);
  if (!loaded.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  const Result<ernc::infrastructure::LegacyParamJsonReport> applied =
      importToStaged(catalog, layout, store, loaded.document, at(1), &loaded.report);
  if (!applied.ok()) {
    return fail(name);
  }
  const auto& report = applied.value();
  if (report.appliedCount != 1U || report.skippedNames.size() < 2U) {
    return fail(name);
  }
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  const auto* state = store.find(ancOff);
  if (state == nullptr || !state->stagedValue || state->deviceValue.has_value()) {
    return fail(name);
  }
  const Result<float> staged = state->stagedValue->asFloat();
  return staged.ok() && staged.value() == 0.f;
}

/// @brief version 不是 1.0.0 时解析失败。
bool invalidVersionFails() {
  constexpr const char* name = "invalid_version_fails";
  constexpr std::string_view text = R"({"version":"2.0.0","params":[]})";
  const LegacyParamJsonLoad loaded = parseLegacyParamJson(text);
  if (loaded.ok() || loaded.error == ErrorCode::Unknown) {
    return fail(name);
  }
  return loaded.error == ErrorCode::InvalidParameter;
}

/// @brief 桥接器把 JSON 写入 staged，不设置 deviceValue。
bool importToStagedCase(const ParameterCatalog& catalog, const FirmwareLayout& layout, const char* fixturePath) {
  constexpr const char* name = "import_to_staged";
  const LegacyParamJsonLoad loaded = loadLegacyParamJsonFile(fixturePath);
  if (!loaded.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  const Result<ernc::infrastructure::LegacyParamJsonReport> applied =
      importToStaged(catalog, layout, store, loaded.document, at(5));
  if (!applied.ok() || applied.value().appliedCount != 2U) {
    return fail(name);
  }
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  const ParameterId alpha = ParameterId::parse("RNC_param.alpha1").value();
  const auto* anc = store.find(ancOff);
  const auto* alphaState = store.find(alpha);
  if (anc == nullptr || alphaState == nullptr || !anc->stagedValue || !alphaState->stagedValue || !anc->dirty
      || !alphaState->dirty) {
    return fail(name);
  }
  const Result<float> ancStaged = anc->stagedValue->asFloat();
  const Result<float> alphaStaged = alphaState->stagedValue->asFloat();
  return ancStaged.ok() && alphaStaged.ok() && ancStaged.value() == 0.f && alphaStaged.value() == 1.5f;
}

/// @brief 导入只改 staged，不假装设备已读回。
bool importDoesNotWriteDevice(const ParameterCatalog& catalog, const FirmwareLayout& layout, const char* fixturePath) {
  constexpr const char* name = "import_does_not_write_device";
  const LegacyParamJsonLoad loaded = loadLegacyParamJsonFile(fixturePath);
  if (!loaded.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  if (!importToStaged(catalog, layout, store, loaded.document, at(1)).ok()) {
    return fail(name);
  }
  for (const auto& entry : loaded.document.params) {
    const Result<ParameterId> id = ParameterId::parse(entry.name);
    if (!id.ok()) {
      continue;
    }
    const auto* state = store.find(id.value());
    if (state == nullptr) {
      continue;
    }
    if (state->deviceValue.has_value()) {
      return fail(name);
    }
  }
  return true;
}

/// @brief 工程 Read 捕获与 JSON 导出在空 Store 下包含相同标量集合。
bool captureReadAllScalarsIncludesUnread(const ParameterCatalog& catalog) {
  constexpr const char* name = "capture_read_all_scalars_includes_unread";
  ParameterRuntimeStore store;
  const LegacyParamJsonDocument read = captureReadParameters(catalog, store);
  const Result<LegacyParamJsonDocument> merged = exportStagedThenDevice(catalog, store);
  if (!merged.ok()) {
    return fail(name);
  }
  if (read.params.size() != merged.value().params.size()) {
    return fail(name);
  }
  const auto* bypass = findParam(read, "Bypass_mod");
  if (bypass == nullptr) {
    return fail(name);
  }
  const Result<std::uint32_t> number = bypass->value.asUInt32();
  return number.ok() && number.value() == 0U;
}

/// @brief 未读标量仍导出，数值为 0。
bool exportAllScalarsIncludesUnread(const ParameterCatalog& catalog) {
  constexpr const char* name = "export_all_scalars_includes_unread";
  ParameterRuntimeStore store;
  const Result<LegacyParamJsonDocument> exported = exportStagedThenDevice(catalog, store);
  if (!exported.ok()) {
    return fail(name);
  }
  const auto* bypass = findParam(exported.value(), "Bypass_mod");
  if (bypass == nullptr) {
    return fail(name);
  }
  const Result<std::uint32_t> number = bypass->value.asUInt32();
  return number.ok() && number.value() == 0U;
}

/// @brief staged 优先导出，再 parse，标量往返一致。
bool exportStagedThenDeviceRoundTrip(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                     const char* fixturePath) {
  constexpr const char* name = "export_staged_then_device_round_trip";
  const LegacyParamJsonLoad loaded = loadLegacyParamJsonFile(fixturePath);
  if (!loaded.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  const ParameterId ancOff = ParameterId::parse("ANC_OFF").value();
  const ParameterId alpha = ParameterId::parse("RNC_param.alpha1").value();
  store.observe(ancOff, ScalarValue::fromFloat(9.f), at(1));
  if (!importToStaged(catalog, layout, store, loaded.document, at(2)).ok()) {
    return fail(name);
  }
  const Result<LegacyParamJsonDocument> exported = exportStagedThenDevice(catalog, store);
  if (!exported.ok() || exported.value().version != kLegacyParamJsonVersion) {
    return fail(name);
  }
  const Result<std::string> text = serializeLegacyParamJson(exported.value());
  if (!text.ok()) {
    return fail(name);
  }
  const LegacyParamJsonLoad roundTrip = parseLegacyParamJson(text.value());
  if (!roundTrip.ok()) {
    return fail(name);
  }
  const auto* anc = findParam(roundTrip.document, "ANC_OFF");
  const auto* alphaEntry = findParam(roundTrip.document, "RNC_param.alpha1");
  if (anc == nullptr || alphaEntry == nullptr) {
    return fail(name);
  }
  const Result<float> ancFloat = anc->value.asFloat();
  const Result<float> alphaFloat = alphaEntry->value.asFloat();
  return ancFloat.ok() && alphaFloat.ok() && ancFloat.value() == 0.f && alphaFloat.value() == 1.5f;
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

/// @brief 导入后进快照，source 为 legacy_json_import。
bool importToSnapshotPersists(const ParameterCatalog& catalog, const FirmwareLayout& layout, const char* fixturePath) {
  constexpr const char* name = "import_to_snapshot_persists";
  const LegacyParamJsonLoad loaded = loadLegacyParamJsonFile(fixturePath);
  if (!loaded.ok()) {
    return fail(name);
  }
  TempWorkspace workspace;
  const Result<std::int64_t> projectId = openProject(workspace);
  if (!projectId.ok()) {
    return fail(name);
  }
  ParameterRuntimeStore store;
  TestDatabase connection(workspace.bundlePath() / "project.sqlite", QStringLiteral("es087_import_snap"));
  ParameterSnapshotService service(connection.database);
  CaptureSnapshotRequest request;
  request.projectId = projectId.value();
  request.firmwareVersion = kFirmwareVersion;
  request.catalogVersion = catalog.catalogVersion();
  const Result<std::int64_t> snapshotId =
      importToSnapshot(service, request, catalog, store, loaded.document, at(3));
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
  if (source != QString::fromUtf8(kLegacyJsonImportSnapshotSource.data(),
                                static_cast<int>(kLegacyJsonImportSnapshotSource.size()))) {
    return fail(name);
  }
  QSqlQuery countQuery(connection.database);
  countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM parameter_value WHERE snapshot_id = :id"));
  countQuery.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  if (!countQuery.exec() || !countQuery.next() || countQuery.value(0).toInt() < 1) {
    return fail(name);
  }
  QSqlQuery rowQuery(connection.database);
  rowQuery.prepare(QStringLiteral(
      "SELECT parameter_id, value_json FROM parameter_value WHERE snapshot_id = :id AND parameter_id = :pid"));
  rowQuery.bindValue(QStringLiteral(":id"), static_cast<qlonglong>(snapshotId.value()));
  rowQuery.bindValue(QStringLiteral(":pid"), QStringLiteral("ANC_OFF"));
  if (!rowQuery.exec() || !rowQuery.next()) {
    return fail(name);
  }
  return rowQuery.value(1).toString().toFloat() == 0.f;
}

/// @brief 导出文本为 2 空格缩进的多行布局，便于人工查看。
bool serializeMultilineLayout() {
  constexpr const char* name = "serialize_multiline_layout";
  LegacyParamJsonDocument document;
  document.version = std::string(kLegacyParamJsonVersion);
  document.params.push_back({ "ANC_OFF", ScalarValue::fromFloat(0.f) });
  document.params.push_back({ "ENC_OFF", ScalarValue::fromUInt32(1U) });
  const Result<std::string> text = serializeLegacyParamJson(document);
  if (!text.ok()) {
    return fail(name);
  }
  const std::string& json = text.value();
  if (json.find("{\n  \"version\"") == std::string::npos
      || json.find("\n  \"params\": [\n") == std::string::npos
      || json.find("\n    {\n      \"name\"") == std::string::npos
      || json.find("\n  ]\n}\n") == std::string::npos) {
    return fail(name);
  }
  return true;
}

bool runCase(std::string_view caseName, const ParameterCatalog& catalog, const FirmwareLayout& layout,
             const char* fixturePath) {
  if (caseName == "minimal_fixture_parses") {
    return minimalFixtureParses(fixturePath);
  }
  if (caseName == "parse_rejects_and_reports") {
    return parseRejectsAndReports(catalog);
  }
  if (caseName == "arrays_and_blob_skipped_with_report") {
    return arraysAndBlobSkippedWithReport(catalog, layout);
  }
  if (caseName == "invalid_version_fails") {
    return invalidVersionFails();
  }
  if (caseName == "import_to_staged") {
    return importToStagedCase(catalog, layout, fixturePath);
  }
  if (caseName == "import_does_not_write_device") {
    return importDoesNotWriteDevice(catalog, layout, fixturePath);
  }
  if (caseName == "capture_read_all_scalars_includes_unread") {
    return captureReadAllScalarsIncludesUnread(catalog);
  }
  if (caseName == "export_all_scalars_includes_unread") {
    return exportAllScalarsIncludesUnread(catalog);
  }
  if (caseName == "export_staged_then_device_round_trip") {
    return exportStagedThenDeviceRoundTrip(catalog, layout, fixturePath);
  }
  if (caseName == "serialize_multiline_layout") {
    return serializeMultilineLayout();
  }
  if (caseName == "import_to_snapshot_persists") {
    return importToSnapshotPersists(catalog, layout, fixturePath);
  }
  std::cerr << "unknown case: " << caseName << '\n';
  return false;
}

}  // namespace

/// @brief 运行 ES-087 旧版参数 JSON 用例。
/// argv[1] 用例名；argv[2] catalog；argv[3] layout；argv[4] legacy_params_v1_minimal.json。
int main(int argc, char** argv) {
  if (argc != 5 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr || argv[4] == nullptr) {
    std::cerr << "usage: ernc_legacy_param_json_tests <case> <catalog.json> <layout_manifest.json> <fixture.json>\n";
    return 2;
  }
  QCoreApplication application(argc, argv);
  const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadFile(argv[2]);
  const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadFile(argv[3]);
  if (!loadedCatalog.ok() || !loadedLayout.ok()) {
    std::cerr << "catalog or layout load failed\n";
    return 2;
  }
  return runCase(argv[1], loadedCatalog.catalog(), loadedLayout.layout(), argv[4]) ? 0 : 1;
}
