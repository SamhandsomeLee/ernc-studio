#pragma once

#include "ernc/domain/project_bundle.hpp"
#include "ernc/domain/types.hpp"

#include <QJsonObject>
#include <QSqlDatabase>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ernc::infrastructure {

/// manifest 内独立的 v1 参数文档。字段缺失时兼容旧工程。
struct ProjectParameterJson {
    /// 归档 Read 原值。Undefined 表示缺失，保存时保留原字段。
    QJsonValue read{QJsonValue::Undefined};

    /// 用户 Set 原值。Undefined 表示缺失，保存时保留原字段。
    QJsonValue staged{QJsonValue::Undefined};
};

/// 单个 `.erncproj` 目录包的文件树、manifest 与 `project.sqlite` 连接。
///
/// 不拥有调用线程契约以外的资源；`QSqlDatabase` 连接由本对象独占直到 `close()`。
/// 打开工程不会写硬件。创建失败时尽量删除未完成的根目录。
class ProjectStore {
public:
    ProjectStore() = default;

    /// 在打开连接的线程销毁，释放本实例的 SQLite 连接名。
    ~ProjectStore() { (void)close(); }

    /// 禁止复制；连接名与打开状态不能共享。
    ProjectStore(const ProjectStore&) = delete;
    ProjectStore& operator=(const ProjectStore&) = delete;

    /// @brief 当前是否持有打开的数据库连接。
    bool isOpen() const noexcept;

    /// @brief 在磁盘上创建空工程包并迁移数据库。
    /// @param root 目标目录路径，通常以 `.erncproj` 结尾；不存在时创建。
    /// @param name 工程名。
    /// @param catalogVersion 参数目录版本字符串。
    /// @return 成功时返回打开摘要；任一步失败时清理根目录并返回错误。
    ernc::domain::Result<ernc::domain::ProjectBundle> createEmpty(
        const std::filesystem::path& root, std::string_view name, std::string_view catalogVersion);

    /// @brief 打开已有工程包：读 manifest、校验结构、迁移 SQLite。
    /// @param root 工程包根目录。
    /// @return 成功时返回摘要；manifest/版本/结构不合法时为 InvalidParameter 或 DatabaseFailed。
    ernc::domain::Result<ernc::domain::ProjectBundle> open(const std::filesystem::path& root);

    /// @brief 刷新数据库与 manifest 的时间戳，原子替换 manifest。
    /// @param parameters 本次调用借用的 Read/Set 文档。Undefined 的字段不覆盖。
    /// @note manifest 提交失败时原文档仍有效，数据库时间戳可能已更新。
    ernc::domain::Result<void> save(const ProjectParameterJson& parameters = {});

    /// @brief 一次读取打开工程的两个参数文档，缺失字段返回 Undefined。
    ernc::domain::Result<ProjectParameterJson> readParameters() const;

    /// @brief 关闭连接并释放 Qt 连接名；可重复调用。
    ernc::domain::Result<void> close();

    /// @brief 当前打开的摘要；未打开时无值。
    const std::optional<ernc::domain::ProjectBundle>& summary() const noexcept { return summary_; }

    /// @brief 工程已打开时把 SQLite 连接借给 fn；未打开时返回 DatabaseFailed。
    template <typename Fn>
    auto withDatabase(Fn&& fn) -> decltype(fn(std::declval<QSqlDatabase&>())) {
        using Return = decltype(fn(std::declval<QSqlDatabase&>()));
        if (!isOpen()) {
            return Return::failure(ernc::domain::ErrorCode::DatabaseFailed);
        }
        return fn(database_);
    }

private:
    /// @brief 同目录临时文件写完后原子提交。失败保留原 manifest。
    /// @param root 已创建的工程根路径，仅在调用期间借用。
    /// @param object 要完整替换 manifest 的内容，仅在调用期间借用。
    ernc::domain::Result<void> writeManifestAtomic(const std::filesystem::path& root, const QJsonObject& object);
    ernc::domain::Result<QJsonObject> readManifest(const std::filesystem::path& root) const;
    ernc::domain::Result<void> ensureBundleLayout(const std::filesystem::path& root, bool create) const;
    ernc::domain::Result<void> openDatabase(const std::filesystem::path& sqlitePath);
    void releaseDatabase() noexcept;
    ernc::domain::Result<ernc::domain::ProjectBundle> loadProjectRow();
    static std::string isoUtcNow();

    QString connectionName_;
    QSqlDatabase database_;
    std::optional<ernc::domain::ProjectBundle> summary_;
};

}  // namespace ernc::infrastructure
