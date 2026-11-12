#pragma once

#include "ernc/application/project/open_project.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/infrastructure/filesystem/project_store.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"

#include <QSqlDatabase>

#include <filesystem>
#include <utility>
#include <optional>
#include <string_view>

namespace ernc::application {

/// 从同一工程 manifest 解析出的两列参数和逐项问题。
struct LoadedProjectParameters {
    /// 工程保存时的历史 Read，缺失字段对应空 v1 文档。
    ernc::infrastructure::LegacyParamJsonLoad read;

    /// 工程保存时的用户 Set，缺失字段对应空 v1 文档。
    ernc::infrastructure::LegacyParamJsonLoad staged;
};

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

    /// @brief 原子提交工程的 Read 和 Set 文档。需在打开连接的 Storage Worker 调用。
    /// @param read v1 标量 Read 原值，只在调用期间借用。空列表表示无已知读数。
    /// @param staged v1 标量 Set 原值，只在调用期间借用。空列表表示无暂存项。
    /// @return 失败时两份原文档均保留。
    ernc::domain::Result<void> saveParameters(const ernc::infrastructure::LegacyParamJsonDocument& read,
                                             const ernc::infrastructure::LegacyParamJsonDocument& staged);

    /// @brief 从 manifest 读取 Read 和 Set。旧工程缺失的文档按空处理。
    /// @return 任一文档格式或读取失败时返回错误，不修改运行态。
    ernc::domain::Result<LoadedProjectParameters> loadParameters() const;

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
