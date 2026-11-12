#include "ernc/infrastructure/persistence/migrator.hpp"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::infrastructure::kLatestSchemaVersion;
using ernc::infrastructure::Migration;
using ernc::infrastructure::migrate;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 一个独立的 :memory: SQLite 连接。
///
/// 构造时注册唯一连接名并打开；析构时先释放连接引用再移除注册。
/// 不创建文件，不访问用户目录。不可拷贝，避免两个对象管理同一连接名。
class TestConnection {
public:
    /// @brief 建立内存连接。
    /// @param suffix 连接名后缀，只用于区分同一进程内的用例。
    explicit TestConnection(const QString& suffix) {
        name_ = QStringLiteral("ernc_storage_test_%1").arg(suffix);
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
        database_.setDatabaseName(QStringLiteral(":memory:"));
        open_ = database_.open();
    }

    /// @brief 关闭并移除连接。
    ~TestConnection() {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name_);
    }

    TestConnection(const TestConnection&) = delete;
    TestConnection& operator=(const TestConnection&) = delete;

    /// @brief 借出连接。
    /// @return 指向内部 QSqlDatabase。不转移所有权。
    QSqlDatabase& db() noexcept { return database_; }

    /// @brief 连接是否打开成功。
    /// @return 打开成功且当前仍打开时为 true。
    bool isOpen() const noexcept { return open_ && database_.isOpen(); }

private:
    /// 本对象注册的连接名。
    QString name_;

    /// 内存数据库连接。本对象持有。
    QSqlDatabase database_;

    /// 构造时 open() 的结果。
    bool open_{false};
};

/// @brief 读取 user_version。
/// @param database 连接，只在本次调用期间借用。
/// @return 当前版本；查询失败时为 -1。
int userVersion(QSqlDatabase& database) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

/// @brief 数据库中的用户表名。
/// @param database 连接，只在本次调用期间借用。
/// @return 不含 sqlite_ 内部表的表名列表。查询失败时为空。
QStringList tableNames(QSqlDatabase& database) {
    QStringList names;
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"))) {
        while (query.next()) {
            names.append(query.value(0).toString());
        }
    }
    return names;
}

/// @brief 一张表的列名。
/// @param database 连接，只在本次调用期间借用。
/// @param table 表名，只在本次调用期间借用。
/// @return 按 PRAGMA 顺序排列的列名。查询失败时为空。
QStringList columnsOf(QSqlDatabase& database, const QString& table) {
    QStringList names;
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        while (query.next()) {
            names.append(query.value(1).toString());
        }
    }
    return names;
}

/// @brief 数据库中的迁移索引名。
/// @param database 连接，只在本次调用期间借用。
/// @return 以 idx_ 开头的索引名。查询失败时为空。
QStringList indexNames(QSqlDatabase& database) {
    QStringList names;
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'idx_%'"))) {
        while (query.next()) {
            names.append(query.value(0).toString());
        }
    }
    return names;
}

/// @brief 检查一张表的列集合。
/// @param database 连接，只在本次调用期间借用。
/// @param table 表名，只在本次调用期间借用。
/// @param expected 期望列名，只在本次调用期间借用。
/// @return 列数和每个期望列都存在时为 true。不检查顺序。
bool columnsMatch(QSqlDatabase& database, const char* table, std::initializer_list<const char*> expected) {
    const QStringList actual = columnsOf(database, QString::fromLatin1(table));
    if (actual.size() != static_cast<int>(expected.size())) {
        return false;
    }
    for (const char* column : expected) {
        if (!actual.contains(QString::fromLatin1(column))) {
            return false;
        }
    }
    return true;
}

/// @brief 内存数据库可以迁移到最新版本并建出七张表。
/// @return 版本、表名和连接类型都符合约定时为 true。
bool memoryDatabaseMigrates() {
    TestConnection connection(QStringLiteral("migrate"));
    if (!connection.isOpen()) {
        return fail("memory_database_migrates");
    }
    const Result<int> result = migrate(connection.db());
    if (!result.ok() || result.value() != kLatestSchemaVersion || userVersion(connection.db()) != kLatestSchemaVersion) {
        return fail("memory_database_migrates");
    }
    const QStringList tables = tableNames(connection.db());
    const QStringList expected = {QStringLiteral("project"), QStringLiteral("device_profile"),
        QStringLiteral("parameter_snapshot"), QStringLiteral("parameter_value"), QStringLiteral("experiment"),
        QStringLiteral("data_asset"), QStringLiteral("operation_log")};
    for (const QString& table : expected) {
        if (!tables.contains(table)) {
            return fail("memory_database_migrates");
        }
    }
    return true;
}

/// @brief 重复迁移保持幂等。
/// @return 两次都成功且版本不变时为 true。
bool migrateIsIdempotent() {
    TestConnection connection(QStringLiteral("idempotent"));
    if (!connection.isOpen()) {
        return fail("migrate_is_idempotent");
    }
    const Result<int> first = migrate(connection.db());
    const Result<int> second = migrate(connection.db());
    return first.ok() && second.ok() && first.value() == kLatestSchemaVersion
        && second.value() == kLatestSchemaVersion && userVersion(connection.db()) == kLatestSchemaVersion;
}

/// @brief 数据库版本高于应用支持时拒绝迁移。
/// @return 返回 InvalidParameter 且版本保持 999 时为 true。
bool newerSchemaRejected() {
    TestConnection connection(QStringLiteral("newer"));
    if (!connection.isOpen()) {
        return fail("newer_schema_rejected");
    }
    QSqlQuery query(connection.db());
    if (!query.exec(QStringLiteral("PRAGMA user_version = 999"))) {
        return fail("newer_schema_rejected");
    }
    const Result<int> result = migrate(connection.db());
    return !result.ok() && result.error() == ErrorCode::InvalidParameter && userVersion(connection.db()) == 999;
}

/// @brief 外键约束在迁移后的连接上生效。
/// @return foreign_keys 为 1 且缺失父行的插入失败时为 true。
bool foreignKeysEnforced() {
    TestConnection connection(QStringLiteral("foreign_keys"));
    if (!connection.isOpen() || !migrate(connection.db()).ok()) {
        return fail("foreign_keys_enforced");
    }
    QSqlQuery pragma(connection.db());
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys")) || !pragma.next() || pragma.value(0).toInt() != 1) {
        return fail("foreign_keys_enforced");
    }
    QSqlQuery insert(connection.db());
    insert.prepare(QStringLiteral(
        "INSERT INTO parameter_value (snapshot_id, parameter_id, data_type, value_json, raw_payload, validity) "
        "VALUES (999, 'x', 'float32', '1.0', NULL, 'valid')"));
    return !insert.exec();
}

/// @brief 一条迁移失败时整条回滚，之前的迁移保留。
/// @return 返回失败、版本停在 1、kept 存在且 dropped 不存在时为 true。
bool failedMigrationRollsBack() {
    TestConnection connection(QStringLiteral("rollback"));
    if (!connection.isOpen()) {
        return fail("failed_migration_rolls_back");
    }
    static constexpr std::string_view versionOne[] = {"CREATE TABLE kept (id INTEGER PRIMARY KEY)"};
    static constexpr std::string_view versionTwo[] = {
        "CREATE TABLE dropped (id INTEGER PRIMARY KEY)",
        "THIS IS NOT VALID SQL",
    };
    static constexpr Migration migrations[] = {
        {1, "kept", versionOne},
        {2, "bad", versionTwo},
    };
    const Result<int> result = migrate(connection.db(), migrations);
    if (result.ok() || userVersion(connection.db()) != 1) {
        return fail("failed_migration_rolls_back");
    }
    const QStringList tables = tableNames(connection.db());
    return tables.contains(QStringLiteral("kept")) && !tables.contains(QStringLiteral("dropped"));
}

/// @brief 空的、起步晚于 1 的、重复版本的迁移列表都被拒绝。
/// @return 三种情况都返回 InvalidParameter 且版本保持 0 时为 true。
bool invalidMigrationListRejected() {
    TestConnection connection(QStringLiteral("invalid_list"));
    if (!connection.isOpen()) {
        return fail("invalid_migration_list_rejected");
    }
    static constexpr std::string_view statements[] = {"CREATE TABLE never (id INTEGER)"};
    static constexpr Migration startsAtTwo[] = {{2, "two", statements}};
    static constexpr Migration duplicate[] = {{1, "one", statements}, {1, "again", statements}};
    const ernc::domain::Result<int> empty = migrate(connection.db(), std::span<const Migration>{});
    const ernc::domain::Result<int> late = migrate(connection.db(), startsAtTwo);
    const ernc::domain::Result<int> repeated = migrate(connection.db(), duplicate);
    return !empty.ok() && empty.error() == ErrorCode::InvalidParameter && !late.ok()
        && late.error() == ErrorCode::InvalidParameter && !repeated.ok()
        && repeated.error() == ErrorCode::InvalidParameter && userVersion(connection.db()) == 0;
}

/// @brief 未打开的连接被拒绝。
/// @return 返回 InvalidParameter 时为 true。
bool closedConnectionRejected() {
    TestConnection connection(QStringLiteral("closed"));
    if (!connection.isOpen()) {
        return fail("closed_connection_rejected");
    }
    connection.db().close();
    const ernc::domain::Result<int> result = migrate(connection.db());
    return !result.ok() && result.error() == ErrorCode::InvalidParameter;
}

/// @brief 七张表的列与设计 §14.2 一致。
/// @return 每张表的列集合都匹配时为 true。
bool expectedColumns() {
    TestConnection connection(QStringLiteral("columns"));
    if (!connection.isOpen() || !migrate(connection.db()).ok()) {
        return fail("expected_columns");
    }
    QSqlDatabase& database = connection.db();
    return columnsMatch(database, "project",
               {"id", "name", "created_at", "updated_at", "schema_version", "catalog_version"})
        && columnsMatch(database, "device_profile",
               {"id", "device_name", "protocol_id", "slave_address", "baud_rate", "data_bits", "parity", "stop_bits",
                   "firmware_version"})
        && columnsMatch(database, "parameter_snapshot",
               {"id", "project_id", "created_at", "source", "firmware_version", "catalog_version", "note"})
        && columnsMatch(database, "parameter_value",
               {"snapshot_id", "parameter_id", "data_type", "value_json", "raw_payload", "validity"})
        && columnsMatch(database, "experiment",
               {"id", "project_id", "name", "started_at", "ended_at", "test_condition", "parameter_snapshot_id", "note"})
        && columnsMatch(database, "data_asset",
               {"id", "experiment_id", "asset_kind", "relative_path", "format", "byte_size", "sha256",
                   "dimensions_json", "metadata_json"})
        && columnsMatch(database, "operation_log",
               {"id", "timestamp", "session_id", "command_id", "operation", "parameter_id", "slot_address", "result",
                   "duration_ms", "details_json"});
}

/// @brief 迁移建立了四个查询索引。
/// @return 四个索引都存在时为 true。
bool indexesCreated() {
    TestConnection connection(QStringLiteral("indexes"));
    if (!connection.isOpen() || !migrate(connection.db()).ok()) {
        return fail("indexes_created");
    }
    const QStringList indexes = indexNames(connection.db());
    return indexes.contains(QStringLiteral("idx_parameter_snapshot_project"))
        && indexes.contains(QStringLiteral("idx_experiment_project"))
        && indexes.contains(QStringLiteral("idx_data_asset_experiment"))
        && indexes.contains(QStringLiteral("idx_operation_log_timestamp"));
}

/// @brief 按用例名运行。
/// @param name 用例名。所有用例只使用内存数据库，不读写用户文件。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "memory_database_migrates") {
        return memoryDatabaseMigrates();
    }
    if (name == "migrate_is_idempotent") {
        return migrateIsIdempotent();
    }
    if (name == "newer_schema_rejected") {
        return newerSchemaRejected();
    }
    if (name == "foreign_keys_enforced") {
        return foreignKeysEnforced();
    }
    if (name == "failed_migration_rolls_back") {
        return failedMigrationRollsBack();
    }
    if (name == "invalid_migration_list_rejected") {
        return invalidMigrationListRejected();
    }
    if (name == "closed_connection_rejected") {
        return closedConnectionRejected();
    }
    if (name == "expected_columns") {
        return expectedColumns();
    }
    if (name == "indexes_created") {
        return indexesCreated();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_storage_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QCoreApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
