#pragma once

#include "ernc/domain/types.hpp"

#include <QSqlDatabase>

#include <span>
#include <string_view>

namespace ernc::infrastructure {

/// 当前应用支持的最新 schema 版本。工程打开代码用它判断是否需要迁移。
inline constexpr int kLatestSchemaVersion = 1;

/// 一条版本化迁移。SQL 在编译期固定，不从用户目录或网络加载。
struct Migration {
    /// 目标版本。从 1 开始，列表内必须严格递增。
    int version{0};

    /// 稳定名字，只用于诊断和日志，不参与版本比较。
    std::string_view name;

    /// 该版本要执行的语句，按顺序在同一事务内执行。
    std::span<const std::string_view> statements;
};

/// @brief 在调用方连接上执行全部未应用的迁移。
///
/// 连接由调用方打开和关闭。通常传 `:memory:` 供测试使用，或由 Storage Worker 持有的文件连接。
/// 本函数不创建文件、不扫描用户目录，也不改变连接名。
/// 本函数会在该连接上执行 `PRAGMA foreign_keys = ON`；这是连接级副作用，后续使用同一连接时保持开启。
/// 每条迁移在自己的事务里执行；失败时回滚该条迁移并返回 DatabaseFailed。
/// 数据库 user_version 高于迁移列表的最高版本时返回 InvalidParameter，不做降级。
/// @param database 已打开的有效连接，只在本次调用期间借用。本函数不关闭它。
/// @return 成功时为应用后的 user_version。连接无效、列表为空或版本倒序时为 InvalidParameter。
ernc::domain::Result<int> migrate(QSqlDatabase& database);

/// @brief 用显式迁移列表执行迁移。
///
/// 供测试和未来分阶段迁移使用；生产路径使用无参重载。
/// @param database 已打开的有效连接，只在本次调用期间借用。
/// @param migrations 迁移列表，只在本次调用期间借用。版本必须从 1 开始严格递增。
/// @return 成功时为应用后的 user_version；失败语义与无参重载相同。
ernc::domain::Result<int> migrate(QSqlDatabase& database, std::span<const Migration> migrations);

}  // namespace ernc::infrastructure
