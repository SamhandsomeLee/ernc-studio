#include "ernc/infrastructure/filesystem/project_store.hpp"

#include "ernc/infrastructure/persistence/migrator.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QString>

#include <atomic>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ProjectBundle;
using ernc::domain::ErrorCode;
using ernc::domain::Result;

inline constexpr int kManifestFormatVersion = 1;

const char* kManifestFile = "manifest.json";
const char* kSqliteFile = "project.sqlite";
const char* kManifestTempFile = "manifest.json.tmp";

std::atomic<std::uint64_t> gConnectionCounter{0};

QString pathToQString(const std::filesystem::path& path) {
    return QString::fromStdWString(path.native());
}

bool directoryExists(const std::filesystem::path& path) {
    std::error_code ignored;
    return std::filesystem::is_directory(path, ignored);
}

}  // namespace

std::string ProjectStore::isoUtcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}

bool ProjectStore::isOpen() const noexcept {
    return database_.isValid() && database_.isOpen();
}

Result<void> ProjectStore::ensureBundleLayout(const std::filesystem::path& root, bool create) const {
    const std::filesystem::path subdirs[] = {
        root / "data" / "regions",
        root / "data" / "experiments",
        root / "data" / "exports",
        root / "logs",
        root / "attachments",
    };
    for (const std::filesystem::path& dir : subdirs) {
        if (create) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec) {
                return Result<void>::failure(ErrorCode::FileIoFailed);
            }
        } else if (!directoryExists(dir)) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
    }
    const std::filesystem::path sqlite = root / kSqliteFile;
    if (!create && !std::filesystem::exists(sqlite)) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::filesystem::path manifest = root / kManifestFile;
    if (!create && !std::filesystem::exists(manifest)) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    return Result<void>::success();
}

Result<QJsonObject> ProjectStore::readManifest(const std::filesystem::path& root) const {
    const std::filesystem::path manifest = root / kManifestFile;
    QFile file(pathToQString(manifest));
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QJsonObject>::failure(ErrorCode::FileIoFailed);
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return Result<QJsonObject>::failure(ErrorCode::InvalidParameter);
    }
    return Result<QJsonObject>::success(doc.object());
}

Result<void> ProjectStore::writeManifestAtomic(const std::filesystem::path& root, const QJsonObject& object) {
    const std::filesystem::path temp = root / kManifestTempFile;
    const std::filesystem::path manifest = root / kManifestFile;
    {
        QFile file(pathToQString(temp));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        const QJsonDocument doc(object);
        if (file.write(doc.toJson(QJsonDocument::Compact)) < 0) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
    }
    QFile::remove(pathToQString(manifest));
    if (!QFile::rename(pathToQString(temp), pathToQString(manifest))) {
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    return Result<void>::success();
}

void ProjectStore::releaseDatabase() noexcept {
    if (database_.isValid()) {
        database_.close();
        database_ = QSqlDatabase();
        if (!connectionName_.isEmpty()) {
            QSqlDatabase::removeDatabase(connectionName_);
            connectionName_.clear();
        }
    }
    summary_.reset();
}

Result<void> ProjectStore::openDatabase(const std::filesystem::path& sqlitePath) {
    releaseDatabase();
    const std::uint64_t id = gConnectionCounter.fetch_add(1, std::memory_order_relaxed);
    connectionName_ = QStringLiteral("ernc_project_store_%1").arg(id);
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(pathToQString(sqlitePath));
    if (!database_.open()) {
        releaseDatabase();
        return Result<void>::failure(ErrorCode::DatabaseFailed);
    }
    return Result<void>::success();
}

Result<ProjectBundle> ProjectStore::loadProjectRow() {
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id, name, schema_version, catalog_version FROM project LIMIT 1"))
        || !query.next()) {
        return Result<ProjectBundle>::failure(ErrorCode::DatabaseFailed);
    }
    ProjectBundle project;
    project.projectId = query.value(0).toLongLong();
    project.name = query.value(1).toString().toStdString();
    project.sqliteSchemaVersion = query.value(2).toInt();
    project.catalogVersion = query.value(3).toString().toStdString();
    return Result<ProjectBundle>::success(std::move(project));
}

Result<ProjectBundle> ProjectStore::createEmpty(
    const std::filesystem::path& root, std::string_view name, std::string_view catalogVersion) {
    if (isOpen()) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }
    if (name.empty() || catalogVersion.empty()) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }
    std::error_code ec;
    if (std::filesystem::exists(root, ec)) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }
    std::filesystem::create_directories(root, ec);
    if (ec) {
        return Result<ProjectBundle>::failure(ErrorCode::FileIoFailed);
    }
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };

    if (!ensureBundleLayout(root, true).ok()) {
        cleanup();
        return Result<ProjectBundle>::failure(ErrorCode::FileIoFailed);
    }

    const std::filesystem::path sqlitePath = root / kSqliteFile;
    if (!openDatabase(sqlitePath).ok()) {
        cleanup();
        return Result<ProjectBundle>::failure(ErrorCode::DatabaseFailed);
    }

    const Result<int> migrated = migrate(database_);
    if (!migrated.ok()) {
        releaseDatabase();
        cleanup();
        return Result<ProjectBundle>::failure(migrated.error());
    }
    if (migrated.value() != kLatestSchemaVersion) {
        releaseDatabase();
        cleanup();
        return Result<ProjectBundle>::failure(ErrorCode::DatabaseFailed);
    }

    const std::string now = isoUtcNow();
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO project (name, created_at, updated_at, schema_version, catalog_version) "
        "VALUES (:name, :created, :updated, :schema, :catalog)"));
    insert.bindValue(QStringLiteral(":name"), QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    insert.bindValue(QStringLiteral(":created"), QString::fromStdString(now));
    insert.bindValue(QStringLiteral(":updated"), QString::fromStdString(now));
    insert.bindValue(QStringLiteral(":schema"), kLatestSchemaVersion);
    insert.bindValue(QStringLiteral(":catalog"),
        QString::fromUtf8(catalogVersion.data(), static_cast<int>(catalogVersion.size())));
    if (!insert.exec()) {
        releaseDatabase();
        cleanup();
        return Result<ProjectBundle>::failure(ErrorCode::DatabaseFailed);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("manifest_version"), kManifestFormatVersion);
    manifest.insert(QStringLiteral("name"), QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    manifest.insert(QStringLiteral("created_at"), QString::fromStdString(now));
    manifest.insert(QStringLiteral("updated_at"), QString::fromStdString(now));
    manifest.insert(QStringLiteral("sqlite_schema_version"), kLatestSchemaVersion);
    manifest.insert(QStringLiteral("catalog_version"),
        QString::fromUtf8(catalogVersion.data(), static_cast<int>(catalogVersion.size())));

    if (!writeManifestAtomic(root, manifest).ok()) {
        releaseDatabase();
        cleanup();
        return Result<ProjectBundle>::failure(ErrorCode::FileIoFailed);
    }

    ProjectBundle bundle;
    bundle.root = root;
    bundle.name = std::string(name);
    bundle.catalogVersion = std::string(catalogVersion);
    bundle.sqliteSchemaVersion = kLatestSchemaVersion;
    bundle.projectId = insert.lastInsertId().toLongLong();
    summary_ = bundle;
    return Result<ProjectBundle>::success(bundle);
}

Result<ProjectBundle> ProjectStore::open(const std::filesystem::path& root) {
    if (isOpen()) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }
    if (!directoryExists(root)) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }
    if (!ensureBundleLayout(root, false).ok()) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }

    const Result<QJsonObject> manifest = readManifest(root);
    if (!manifest.ok()) {
        return Result<ProjectBundle>::failure(manifest.error());
    }
    const QJsonObject object = manifest.value();
    const int manifestSchema = object.value(QStringLiteral("sqlite_schema_version")).toInt(-1);
    if (manifestSchema < 0) {
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }

    const std::filesystem::path sqlitePath = root / kSqliteFile;
    if (!openDatabase(sqlitePath).ok()) {
        return Result<ProjectBundle>::failure(ErrorCode::DatabaseFailed);
    }

    const Result<int> migrated = migrate(database_);
    if (!migrated.ok()) {
        releaseDatabase();
        return Result<ProjectBundle>::failure(migrated.error());
    }
    if (migrated.value() != manifestSchema) {
        releaseDatabase();
        return Result<ProjectBundle>::failure(ErrorCode::InvalidParameter);
    }

    const Result<ProjectBundle> row = loadProjectRow();
    if (!row.ok()) {
        releaseDatabase();
        return Result<ProjectBundle>::failure(row.error());
    }

    ProjectBundle bundle = row.value();
    bundle.root = root;
    bundle.name = object.value(QStringLiteral("name")).toString().toStdString();
    bundle.catalogVersion = object.value(QStringLiteral("catalog_version")).toString().toStdString();
    bundle.sqliteSchemaVersion = migrated.value();
    summary_ = bundle;
    return Result<ProjectBundle>::success(bundle);
}

Result<void> ProjectStore::save() {
    if (!isOpen() || !summary_.has_value()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::string now = isoUtcNow();
    QSqlQuery update(database_);
    update.prepare(QStringLiteral("UPDATE project SET updated_at = :updated WHERE id = :id"));
    update.bindValue(QStringLiteral(":updated"), QString::fromStdString(now));
    update.bindValue(QStringLiteral(":id"), summary_->projectId);
    if (!update.exec()) {
        return Result<void>::failure(ErrorCode::DatabaseFailed);
    }

    const Result<QJsonObject> manifest = readManifest(summary_->root);
    if (!manifest.ok()) {
        return Result<void>::failure(manifest.error());
    }
    QJsonObject object = manifest.value();
    object.insert(QStringLiteral("updated_at"), QString::fromStdString(now));
    if (!writeManifestAtomic(summary_->root, object).ok()) {
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    return Result<void>::success();
}

Result<void> ProjectStore::close() {
    releaseDatabase();
    return Result<void>::success();
}

}  // namespace ernc::infrastructure
