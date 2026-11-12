#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ernc::domain {

/// 工程包在磁盘上的只读摘要。不含数据库句柄，不能据此直接写 SQL。
///
/// `root` 是 `.erncproj` 目录包路径。打开/保存工程不会写硬件。
struct ProjectBundle {
    /// 工程包根目录，例如 `D:/tmp/Demo.erncproj/`。
    std::filesystem::path root;

    /// `project` 表主键。关闭工程后不应继续使用。
    std::int64_t projectId{};

    /// 工程显示名；来自 manifest 与 `project.name`。
    std::string name;

    /// SQLite schema 版本，应对 `PRAGMA user_version` 与 manifest `sqlite_schema_version`。
    int sqliteSchemaVersion{};

    /// 参数目录版本字符串，写入 `project.catalog_version`。
    std::string catalogVersion;
};

}  // namespace ernc::domain
