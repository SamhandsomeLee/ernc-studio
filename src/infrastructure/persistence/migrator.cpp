#include "ernc/infrastructure/persistence/migrator.hpp"

#include "migrations/migrations.hpp"

#include <QSqlQuery>
#include <QString>

#include <span>
#include <string>
#include <string_view>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;

/// @brief 执行一条 SQL 语句。
/// @param database 调用方连接，只在本次调用期间借用。本函数不提交事务。
/// @param sql 单条语句文本，只在本次调用期间借用。空文本交给驱动判断。
/// @return 执行成功时为成功结果。驱动拒绝语句时为 DatabaseFailed。
Result<void> execStatement(QSqlDatabase& database, std::string_view sql) {
    QSqlQuery query(database);
    if (!query.exec(QString::fromUtf8(sql.data(), static_cast<int>(sql.size())))) {
        return Result<void>::failure(ErrorCode::DatabaseFailed);
    }
    return Result<void>::success();
}

/// @brief 读取 SQLite 的 user_version。
/// @param database 调用方连接，只在本次调用期间借用。
/// @return 成功时为当前版本。查询失败时为 DatabaseFailed。
Result<int> readUserVersion(QSqlDatabase& database) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        return Result<int>::failure(ErrorCode::DatabaseFailed);
    }
    return Result<int>::success(query.value(0).toInt());
}

/// @brief 检查迁移列表是否从 1 开始严格递增。
/// @param migrations 迁移列表，只在本次调用期间借用。
/// @return 列表非空、首版本为 1 且每个版本都更大时为 true。
bool validMigrations(std::span<const Migration> migrations) noexcept {
    if (migrations.empty() || migrations.front().version != 1) {
        return false;
    }
    int previous = 0;
    for (const Migration& migration : migrations) {
        if (migration.version <= previous) {
            return false;
        }
        previous = migration.version;
    }
    return true;
}

}  // namespace

ernc::domain::Result<int> migrate(QSqlDatabase& database) {
    return migrate(database, builtinMigrations());
}

ernc::domain::Result<int> migrate(QSqlDatabase& database, std::span<const Migration> migrations) {
    if (!database.isValid() || !database.isOpen() || !validMigrations(migrations)) {
        return Result<int>::failure(ErrorCode::InvalidParameter);
    }
    // foreign_keys 是连接级开关，必须在事务外设置；SQLite 默认关闭。
    if (!execStatement(database, "PRAGMA foreign_keys = ON").ok()) {
        return Result<int>::failure(ErrorCode::DatabaseFailed);
    }
    const Result<int> version = readUserVersion(database);
    if (!version.ok()) {
        return Result<int>::failure(ErrorCode::DatabaseFailed);
    }
    const int current = version.value();
    const int latest = migrations.back().version;
    if (current < 0 || current > latest) {
        return Result<int>::failure(ErrorCode::InvalidParameter);
    }

    int applied = current;
    for (const Migration& migration : migrations) {
        if (migration.version <= current) {
            continue;
        }
        if (!database.transaction()) {
            return Result<int>::failure(ErrorCode::DatabaseFailed);
        }
        bool ok = true;
        for (const std::string_view statement : migration.statements) {
            if (!execStatement(database, statement).ok()) {
                ok = false;
                break;
            }
        }
        if (ok) {
            const std::string setVersion = "PRAGMA user_version = " + std::to_string(migration.version);
            ok = execStatement(database, setVersion).ok();
        }
        if (!ok || !database.commit()) {
            database.rollback();
            return Result<int>::failure(ErrorCode::DatabaseFailed);
        }
        applied = migration.version;
    }
    return Result<int>::success(applied);
}

}  // namespace ernc::infrastructure
