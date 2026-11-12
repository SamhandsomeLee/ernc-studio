#pragma once

#include "ernc/application/project/open_project.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/infrastructure/filesystem/project_store.hpp"

#include <QSqlDatabase>

#include <filesystem>
#include <utility>
#include <optional>
#include <string_view>

namespace ernc::application {

/// 应用层工程生命周期：至多一个打开的工程包。
///
/// 同步 I/O；最终应移到 Storage Worker，UI 线程不应长阻塞。
class ProjectSession {
public:
    ProjectSession() = default;

    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;

    /// @brief 是否已有打开的工程。
    bool isOpen() const noexcept { return store_.isOpen(); }

    /// @brief 创建空工程包并打开。
    ernc::domain::Result<OpenProject> create(
        const std::filesystem::path& root, std::string_view name, std::string_view catalogVersion);

    /// @brief 打开已有工程包。已有打开工程时返回 InvalidParameter。
    ernc::domain::Result<OpenProject> open(const std::filesystem::path& root);

    /// @brief 保存元数据；无打开工程时 InvalidParameter。
    ernc::domain::Result<void> save();

    /// @brief 关闭工程；幂等。
    ernc::domain::Result<void> close();

    /// @brief 只读视图；未打开时无值。
    const std::optional<OpenProject>& project() const noexcept {
        return store_.summary();
    }

    /// @brief 在工程已打开时对 `project.sqlite` 执行 fn；关闭后不可用。
    template <typename Fn>
    auto withDatabase(Fn&& fn) -> decltype(fn(std::declval<QSqlDatabase&>())) {
        return store_.withDatabase(std::forward<Fn>(fn));
    }

private:
    ernc::infrastructure::ProjectStore store_;
};

}  // namespace ernc::application
