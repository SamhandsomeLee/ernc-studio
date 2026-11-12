#include "ernc/application/project/project_session.hpp"
#include "ernc/infrastructure/persistence/migrator.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {

using ernc::application::ProjectSession;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::infrastructure::kLatestSchemaVersion;

constexpr const char* kCatalogVersion = "0.2.0";
constexpr const char* kProjectName = "ExampleVehicle";

/// @brief 报告当前用例失败。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 系统临时目录下的工程父路径；不访问用户主目录。
struct TempWorkspace {
    QTemporaryDir qtDir;
    std::filesystem::path parent;

    TempWorkspace() {
        if (!qtDir.isValid()) {
            std::random_device device;
            parent = std::filesystem::temp_directory_path()
                / ("ernc-es084-" + std::to_string(device()));
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

    std::filesystem::path bundlePath() const {
        return parent / "Sample.erncproj";
    }

private:
    bool ownsFallback{false};
};

QString pathToQString(const std::filesystem::path& path) {
    return QString::fromStdWString(path.native());
}

bool directoryExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

/// @brief 校验 §14.1 目录树与 manifest 必填字段。
bool bundleLayoutValid(const std::filesystem::path& root) {
    const std::filesystem::path subdirs[] = {
        root / "data" / "regions",
        root / "data" / "experiments",
        root / "data" / "exports",
        root / "logs",
        root / "attachments",
    };
    for (const std::filesystem::path& dir : subdirs) {
        if (!directoryExists(dir)) {
            return false;
        }
    }
    if (!std::filesystem::exists(root / "project.sqlite")) {
        return false;
    }
    QFile manifest(pathToQString(root / "manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(manifest.readAll());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject object = doc.object();
    return object.contains(QStringLiteral("manifest_version"))
        && object.contains(QStringLiteral("name")) && object.contains(QStringLiteral("created_at"))
        && object.contains(QStringLiteral("updated_at"))
        && object.contains(QStringLiteral("sqlite_schema_version"))
        && object.contains(QStringLiteral("catalog_version"));
}

/// @brief 独立打开 `project.sqlite` 核对 schema 与 `project` 行数。
bool sqliteMatchesContract(const std::filesystem::path& root) {
    const QString connectionName = QStringLiteral("ernc_project_test_verify");
    {
        QSqlDatabase existing = QSqlDatabase::database(connectionName);
        if (existing.isValid()) {
            existing.close();
            QSqlDatabase::removeDatabase(connectionName);
        }
    }
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(pathToQString(root / "project.sqlite"));
    if (!database.open()) {
        return false;
    }
    QSqlQuery versionQuery(database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        database.close();
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }
    const int userVersion = versionQuery.value(0).toInt();
    if (userVersion != kLatestSchemaVersion) {
        database.close();
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }
    QSqlQuery countQuery(database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM project")) || !countQuery.next()) {
        database.close();
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }
    const bool ok = countQuery.value(0).toInt() == 1;
    database.close();
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool createEmptyProject() {
    TempWorkspace workspace;
    ProjectSession session;
    const Result created = session.create(workspace.bundlePath(), kProjectName, kCatalogVersion);
    if (!created.ok()) {
        return fail("create_empty_project");
    }
    if (!session.isOpen() || created.value().name != kProjectName
        || created.value().catalogVersion != kCatalogVersion
        || created.value().sqliteSchemaVersion != kLatestSchemaVersion) {
        return fail("create_empty_project");
    }
    if (!bundleLayoutValid(workspace.bundlePath()) || !sqliteMatchesContract(workspace.bundlePath())) {
        return fail("create_empty_project");
    }
    return session.close().ok();
}

bool closeAndReopen() {
    TempWorkspace workspace;
    ProjectSession session;
    const Result created = session.create(workspace.bundlePath(), kProjectName, kCatalogVersion);
    if (!created.ok()) {
        return fail("close_and_reopen");
    }
    const std::int64_t projectId = created.value().projectId;
    if (!session.close().ok()) {
        return fail("close_and_reopen");
    }
    if (session.isOpen()) {
        return fail("close_and_reopen");
    }

    ProjectSession reopened;
    const Result opened = reopened.open(workspace.bundlePath());
    if (!opened.ok() || !reopened.isOpen()) {
        return fail("close_and_reopen");
    }
    const auto& summary = opened.value();
    return summary.projectId == projectId && summary.name == kProjectName
        && summary.catalogVersion == kCatalogVersion && summary.sqliteSchemaVersion == kLatestSchemaVersion;
}

bool setUserVersion(const std::filesystem::path& sqlitePath, int version) {
    const QString connectionName = QStringLiteral("ernc_project_test_bump");
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(pathToQString(sqlitePath));
    if (!database.open()) {
        return false;
    }
    QSqlQuery query(database);
    const bool ok = query.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
    database.close();
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool schemaVersionGuard() {
    TempWorkspace workspace;
    {
        ProjectSession session;
        if (!session.create(workspace.bundlePath(), kProjectName, kCatalogVersion).ok()) {
            return fail("schema_version_guard");
        }
        if (!session.close().ok()) {
            return fail("schema_version_guard");
        }
    }
    if (!setUserVersion(workspace.bundlePath() / "project.sqlite", 999)) {
        return fail("schema_version_guard");
    }
    ProjectSession session;
    const Result opened = session.open(workspace.bundlePath());
    return !opened.ok() && opened.error() == ErrorCode::InvalidParameter;
}

bool runCase(std::string_view name) {
    if (name == "create_empty_project") {
        return createEmptyProject();
    }
    if (name == "close_and_reopen") {
        return closeAndReopen();
    }
    if (name == "schema_version_guard") {
        return schemaVersionGuard();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_project_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QCoreApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
