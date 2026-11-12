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

/// 单个 `.erncproj` 目录包的文件树、manifest 与 `project.sqlite` 连接。
///
/// 不拥有调用线程契约以外的资源；`QSqlDatabase` 连接由本对象独占直到 `close()`。
/// 打开工程不会写硬件。创建失败时尽量删除未完成的根目录。
class ProjectStore {
public:
    ProjectStore() = default;

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

    /// @brief 刷新 `project.updated_at` 与 manifest 的 `updated_at`。
    ernc::domain::Result<void> save();

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
