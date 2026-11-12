#include "shell/main_window.hpp"

#include "ernc/application/project/legacy_param_json_bridge.hpp"
#include "ernc/application/project/project_session.hpp"
#include "workspace/editor_tabs.hpp"

#include <QAction>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QStringList>
#include <QtConcurrentRun>

#include <chrono>
#include <filesystem>
#include <type_traits>

namespace ernc::presentation {
namespace {

using ernc::application::ProjectSession;
using ernc::application::captureReadParameters;
using ernc::application::captureStagedParameters;
using ernc::application::exportStagedThenDevice;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::ProjectBundle;
using ernc::domain::Result;
using ernc::infrastructure::LegacyParamJsonReport;

/// 目录工程打开后的摘要与文档，均为值副本。SQLite 连接留在 Storage Worker。
struct ProjectFileData {
    /// manifest 与数据库验证后的工程摘要。
    ProjectBundle project;
    /// manifest 中的 v1 Read 和 Set 文档及逐项解析结果。旧工程的缺失文档为空。
    ernc::application::LoadedProjectParameters parameters;
};

/// @brief 在 Storage Worker 执行一次本地文件操作，主线程继续处理绘制和设备事件。
/// @param owner 主窗口，仅在 UI 线程借用。任务结束前暂缓用户输入，避免重复操作。
/// @param label 操作进度文字，仅用于本次对话框。
/// @param work 拥有参数副本的可调用对象。不得持有 QWidget、运行态或其他线程的 SQLite 连接。
/// @return Worker 完成后返回其结果。开始磁盘提交后没有用户取消入口。
template <typename Work>
auto runFileJob(QWidget& owner, const QString& label, Work work) {
    QProgressDialog progress(label, QString(), 0, 0, &owner,
                             Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    progress.setWindowTitle(QStringLiteral("ERNC Studio"));
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::WindowModal);

    QEventLoop loop;
    QFutureWatcher<std::invoke_result_t<Work>> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(std::move(work)));
    if (!watcher.isFinished()) {
        progress.show();
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return watcher.result();
}

/// @brief 将 Qt 选择的路径转为 Windows 原生路径。
/// @param path 本次调用借用的 UTF-16 文件或目录路径。
std::filesystem::path nativePath(const QString& path) {
    return std::filesystem::path(path.toStdWString());
}

/// @brief 为保存对话框补后缀，保留已匹配的大小写写法。
/// @param path 用户选择的非空路径，移入返回值。
/// @param suffix 包括点号的扩展名，仅在本次调用借用。
QString withSuffix(QString path, const QString& suffix) {
    if (!path.endsWith(suffix, Qt::CaseInsensitive)) {
        path += suffix;
    }
    return path;
}

/// @brief 显示本地文件失败。路径与内容按纯文本呈现。
/// @param owner 主窗口，显示期间借用，须在 UI 主线程调用。
/// @param operation 失败操作的标题，只在本次调用借用。
/// @param path 用户选择的路径，只在本次调用借用。
/// @param error 服务返回的失败类别。
void showFileFailure(QWidget& owner, const QString& operation, const QString& path, ErrorCode error) {
    QString reason = QStringLiteral("文件或工程的格式不受支持，或目标已存在。");
    if (error == ErrorCode::DatabaseFailed) {
        reason = QStringLiteral("无法打开或更新工程数据库。");
    } else if (error == ErrorCode::FileIoFailed) {
        reason = QStringLiteral("无法读写文件。请检查路径和权限。");
    }
    QMessageBox dialog(QMessageBox::Warning, operation, reason, QMessageBox::Ok, &owner);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setInformativeText(path);
    dialog.exec();
}

/// @brief 把部分成功或未导入任何值的报告显示在 UI 主线程。
/// @param owner 借用的主窗口，用于状态栏和报告对话框。
/// @param title 当前导入或打开动作的标题，只在本次调用借用。
/// @param report 逐项报告，含未知名称、不可暂存项和格式问题，只在本次调用借用。
/// @param summary 操作成功的数量和目标列，只在本次调用借用。
/// @param showEmpty 为 true 时，零项导入也显示提示。
void showParameterReport(QMainWindow& owner, const QString& title, const LegacyParamJsonReport& report,
                         const QString& summary, bool showEmpty) {
    QStringList details;
    const auto append = [&details](const QString& label, const std::vector<std::string>& names) {
        if (!names.empty()) {
            details.append(label);
            for (const auto& name : names) {
                details.append(QString::fromStdString(name));
            }
        }
    };
    append(QStringLiteral("未知参数"), report.unknownNames);
    append(QStringLiteral("跳过的参数"), report.skippedNames);
    append(QStringLiteral("格式错误的记录"), report.malformedEntries);
    append(QStringLiteral("未识别的字段"), report.unrecognizedTopLevelFields);

    if (!details.isEmpty() || (showEmpty && report.appliedCount == 0)) {
        QMessageBox dialog(QMessageBox::Information, title, summary, QMessageBox::Ok, &owner);
        if (!details.isEmpty()) {
            dialog.setInformativeText(QStringLiteral("有未应用的内容，可展开查看详情。"));
            dialog.setDetailedText(details.join(u'\n'));
        }
        dialog.exec();
    } else if (showEmpty) {
        owner.statusBar()->showMessage(summary, 5000);
    }
}

}  // namespace

void MainWindow::addFileMenu() {
    QMenu* menu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction* create = menu->addAction(QStringLiteral("新建工程…"), this, &MainWindow::createProject);
    openProjectAction_ = menu->addAction(QStringLiteral("打开工程…"), this, &MainWindow::openProject);
    saveProjectAction_ = menu->addAction(QStringLiteral("保存工程"), this, &MainWindow::saveProject);
    closeProjectAction_ = menu->addAction(QStringLiteral("关闭工程"), this, &MainWindow::closeProject);
    menu->addSeparator();
    QAction* importAction = menu->addAction(QStringLiteral("导入 JSON 参数…"), this, &MainWindow::importParameters);
    QAction* exportAction = menu->addAction(QStringLiteral("导出 JSON 参数…"), this, &MainWindow::exportParameters);

    const bool canStage = catalog_ != nullptr && firmwareLayout_.has_value();
    create->setEnabled(canStage);
    openProjectAction_->setEnabled(canStage);
    importAction->setEnabled(canStage);
    exportAction->setEnabled(catalog_ != nullptr);
    updateProjectActions();
}

void MainWindow::createProject() {
    const QString selected = QFileDialog::getSaveFileName(this, QStringLiteral("新建工程目录"),
        QStringLiteral("未命名.erncproj"), QStringLiteral("ERNC 工程 (*.erncproj)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (selected.isEmpty() || !confirmLeavingProject()) {
        return;
    }
    const QString path = withSuffix(selected, QStringLiteral(".erncproj"));
    const auto created = runFileJob(*this, QStringLiteral("正在新建工程…"),
        [root = nativePath(path), name = QFileInfo(path).completeBaseName().toStdString(), version = catalog_->catalogVersion()] {
            ProjectSession session;
            return session.create(root, name, version);
        });
    if (!created.ok()) {
        showFileFailure(*this, QStringLiteral("新建工程失败"), path, created.error());
        return;
    }
    if (project_) {
        parameterStore_.clearStaged();
        parameterStore_.clearProjectReads();
        editorTabs_->refreshLocalParameters();
    }
    project_ = created.value();
    savedProjectRead_ = captureReadParameters(*catalog_, parameterStore_).params;
    savedProjectSet_ = captureStagedParameters(*catalog_, parameterStore_).params;
    updateProjectActions();
    statusBar()->showMessage(QStringLiteral("已新建工程 %1").arg(path), 5000);
}

void MainWindow::openProject() {
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 .erncproj 工程目录"));
    if (path.isEmpty()) {
        return;
    }
    const auto root = nativePath(QDir::cleanPath(path));
    if (!QString::fromStdWString(root.filename().wstring()).endsWith(QStringLiteral(".erncproj"), Qt::CaseInsensitive)) {
        showFileFailure(*this, QStringLiteral("请选择 .erncproj 工程目录"), path, ErrorCode::InvalidParameter);
        return;
    }
    if (!confirmLeavingProject()) {
        return;
    }
    if (!project_ && !captureStagedParameters(*catalog_, parameterStore_).params.empty()
        && QMessageBox::question(this, QStringLiteral("打开工程"),
            QStringLiteral("打开工程会替换当前工作区的 Set 值。继续打开吗？"),
            QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Open) {
        return;
    }
    const auto opened = runFileJob(*this, QStringLiteral("正在打开工程…"), [root] {
        ProjectSession session;
        auto project = session.open(root);
        if (!project.ok()) {
            return Result<ProjectFileData>::failure(project.error());
        }
        auto parameters = session.loadParameters();
        if (!parameters.ok()) {
            return Result<ProjectFileData>::failure(parameters.error());
        }
        return Result<ProjectFileData>::success({std::move(project.value()), std::move(parameters.value())});
    });
    if (!opened.ok()) {
        showFileFailure(*this, QStringLiteral("打开工程失败"), path, opened.error());
        return;
    }

    // 先校验 Set，再替换工作区。设备读数和观察时间仍由设备会话维护。
    const auto& parameters = opened.value().parameters;
    ParameterRuntimeStore restored;
    const auto now = std::chrono::system_clock::now();
    const auto report = ernc::application::importToStaged(*catalog_, *firmwareLayout_, restored,
        parameters.staged.document, now, &parameters.staged.report, ernc::application::StagedParameterSource::Project);
    if (!report.ok()) {
        showFileFailure(*this, QStringLiteral("打开工程失败"), path, report.error());
        return;
    }
    parameterStore_.clearStaged();
    parameterStore_.clearProjectReads();
    const auto readReport = ernc::application::restoreProjectReads(*catalog_, *firmwareLayout_, parameterStore_, parameters.read);
    for (const auto& definition : catalog_->entries()) {
        const auto* state = restored.find(definition.id);
        if (state != nullptr && state->stagedValue) {
            parameterStore_.stage(definition.id, *state->stagedValue, now);
        }
    }
    project_ = opened.value().project;
    savedProjectRead_ = captureReadParameters(*catalog_, parameterStore_).params;
    savedProjectSet_ = captureStagedParameters(*catalog_, parameterStore_).params;
    editorTabs_->refreshLocalParameters();
    updateProjectActions();
    statusBar()->showMessage(QStringLiteral("已打开工程 %1，恢复 %2 项历史 Read 和 %3 项 Set")
                             .arg(path).arg(readReport.appliedCount).arg(report.value().appliedCount), 5000);
    showParameterReport(*this, QStringLiteral("打开工程"), readReport,
        QStringLiteral("已恢复 %1 项历史 Read。").arg(readReport.appliedCount), false);
    showParameterReport(*this, QStringLiteral("打开工程"), report.value(),
        QStringLiteral("已恢复 %1 项 Set 值。").arg(report.value().appliedCount), false);
}

bool MainWindow::saveProject() {
    if (!project_) {
        return false;
    }
    auto read = captureReadParameters(*catalog_, parameterStore_);
    auto staged = captureStagedParameters(*catalog_, parameterStore_);
    const QString path = QString::fromStdWString(project_->root.wstring());
    const auto saved = runFileJob(*this, QStringLiteral("正在保存工程…"), [root = project_->root, read, staged] {
        ProjectSession session;
        const auto opened = session.open(root);
        if (!opened.ok()) {
            return Result<void>::failure(opened.error());
        }
        return session.saveParameters(read, staged);
    });
    if (!saved.ok()) {
        showFileFailure(*this, QStringLiteral("保存工程失败"), path, saved.error());
        return false;
    }
    savedProjectRead_ = std::move(read.params);
    savedProjectSet_ = std::move(staged.params);
    statusBar()->showMessage(QStringLiteral("已保存工程 %1（%2 项 Read，%3 项 Set）")
                             .arg(path).arg(savedProjectRead_.size()).arg(savedProjectSet_.size()), 5000);
    return true;
}

bool MainWindow::confirmLeavingProject() {
    if (!project_ || (captureReadParameters(*catalog_, parameterStore_).params == savedProjectRead_
                   && captureStagedParameters(*catalog_, parameterStore_).params == savedProjectSet_)) {
        return true;
    }
    const auto action = QMessageBox::question(this, QStringLiteral("工程有未保存的更改"),
        QStringLiteral("保存当前工程的 Read 和 Set 值吗？"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    return action == QMessageBox::Discard || (action == QMessageBox::Save && saveProject());
}

bool MainWindow::closeProject() {
    if (!project_ || !confirmLeavingProject()) {
        return false;
    }
    project_.reset();
    savedProjectRead_.clear();
    savedProjectSet_.clear();
    parameterStore_.clearStaged();
    parameterStore_.clearProjectReads();
    editorTabs_->refreshLocalParameters();
    updateProjectActions();
    statusBar()->showMessage(QStringLiteral("已关闭工程"), 5000);
    return true;
}

void MainWindow::updateProjectActions() {
    saveProjectAction_->setEnabled(project_.has_value());
    closeProjectAction_->setEnabled(project_.has_value());
    setWindowTitle(project_ ? QStringLiteral("ERNC Studio — %1").arg(QString::fromStdString(project_->name))
                           : QStringLiteral("ERNC Studio"));
    updateWorkspaceContext();
}

void MainWindow::importParameters() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入 JSON 参数"), {}, QStringLiteral("JSON 参数 (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    const auto loaded = runFileJob(*this, QStringLiteral("正在读取 JSON 参数…"), [path = nativePath(path)] {
        return ernc::infrastructure::loadLegacyParamJsonFile(path);
    });
    if (!loaded.ok()) {
        showFileFailure(*this, QStringLiteral("导入 JSON 参数失败"), path, loaded.error);
        return;
    }
    const auto applied = ernc::application::importToStaged(*catalog_, *firmwareLayout_, parameterStore_, loaded.document,
                                                         std::chrono::system_clock::now(), &loaded.report);
    if (!applied.ok()) {
        showFileFailure(*this, QStringLiteral("导入 JSON 参数失败"), path, applied.error());
        return;
    }
    editorTabs_->refreshLocalParameters();
    showParameterReport(*this, QStringLiteral("导入 JSON 参数"), applied.value(),
        QStringLiteral("已暂存 %1 项参数到 Set。").arg(applied.value().appliedCount), true);
}

void MainWindow::exportParameters() {
    const QString directory = project_
        ? QString::fromStdWString((project_->root / "data" / "exports").wstring()) : QString();
    QFileDialog dialog(this, QStringLiteral("导出 JSON 参数（优先 Set，其次 Read）"), directory,
                       QStringLiteral("JSON 参数 (*.json)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("json"));
    dialog.selectFile(QStringLiteral("参数.json"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString path = dialog.selectedFiles().constFirst();
    auto document = exportStagedThenDevice(*catalog_, parameterStore_);
    if (!document.ok() || document.value().params.empty()) {
        QMessageBox::information(this, QStringLiteral("导出 JSON 参数"), QStringLiteral("当前没有可导出的标量参数值。"));
        return;
    }
    std::size_t staleReads = 0;
    for (const auto& definition : catalog_->entries()) {
        const auto* state = parameterStore_.find(definition.id);
        if (state != nullptr && !state->stagedValue && state->displayedRead() && (state->stale || state->projectReadValue)
            && (!definition.shape || definition.shape == ernc::domain::ValueShape::Scalar)) {
            ++staleReads;
        }
    }
    const auto count = document.value().params.size();
    const auto written = runFileJob(*this, QStringLiteral("正在导出 JSON 参数…"),
        [path = nativePath(path), values = std::move(document.value())] {
            return ernc::infrastructure::writeLegacyParamJsonFile(path, values);
        });
    if (!written.ok()) {
        showFileFailure(*this, QStringLiteral("导出 JSON 参数失败"), path, written.error());
        return;
    }
    statusBar()->showMessage(QStringLiteral("已导出 %1 项参数，其中 %2 项为过期设备读数。%3")
                             .arg(count).arg(staleReads).arg(path), 5000);
}

}  // namespace ernc::presentation
