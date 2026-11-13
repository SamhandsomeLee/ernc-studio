#pragma once

#include <QWidget>

#ifdef slots
#pragma push_macro("slots")
#undef slots
#define ERNC_RESTORE_QT_SLOTS_MACRO
#endif

#include "ernc/application/region_reader.hpp"
#include "ernc/application/region_writer.hpp"
#include "ernc/application/transfer_cancellation.hpp"
#include "ernc/domain/region_catalog.hpp"

#ifdef ERNC_RESTORE_QT_SLOTS_MACRO
#pragma pop_macro("slots")
#undef ERNC_RESTORE_QT_SLOTS_MACRO
#endif

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QStackedWidget;

namespace ernc::presentation {

class SpecRadiusPage;

/// @brief 区域选择和传输动作。
///
/// 必须在 UI 主线程创建和调用。不包含 QSerialPort，也不拼 Modbus 帧。
/// 导入只装入文件字节。预览不入队。写入只在已有预览后调用 RegionWriter::start。
class RegionWorkspace final : public QWidget {
public:
    /// @brief 绑定目录、区域读和区域写。
    /// @param catalog 区域目录，只借用。必须比本工作区活得久。
    /// @param reader 区域读，只借用。为空时读取和取消读失败。
    /// @param writer 区域写，只借用。为空时预览和写入失败。
    /// @param parent Qt 父对象。非空时由父子关系拥有本工作区。必须在 UI 主线程创建。
    RegionWorkspace(const ernc::domain::RegionCatalog& catalog, ernc::application::RegionReader* reader,
                    ernc::application::RegionWriter* writer, QWidget* parent = nullptr);

    /// @brief 选中三块区域之一。
    /// @param path ERNC_TF、check_data 或 ERNC_TF.SpecRadius。
    /// @return 目录里有这块区域时为 true。切换后清掉已导入字节和预览。
    bool selectRegion(const QString& path);

    /// @brief 选中子字段。空文本表示整块区域。
    /// @param path 子字段完整路径。空表示整段。
    /// @return 空路径或该子字段存在时为 true。切换后清掉已导入字节和预览。
    bool selectField(const QString& path);

    /// @brief 对当前选择调用 RegionReader::start。
    /// @return start 的结果。证据不足时失败，进度保持空闲。这不是设备响应。
    ernc::domain::Result<void> readSelected();

    /// @brief 只读导入 BIN，不调用写入。
    /// @param source 用户的 .bin 路径。只读打开。
    /// @return 长度符合当前区域时成功。状态变为已导入、尚未写入。失败时不改已有字节。
    ernc::domain::Result<void> importBin(const std::filesystem::path& source);

    /// @brief 只读导入 TXT，不调用写入。
    /// @param source 用户的 .txt 路径。只读打开。
    /// @return 个数符合当前区域时成功。float 会收成区域字节。失败时不改已有字节。
    ernc::domain::Result<void> importTxt(const std::filesystem::path& source);

    /// @brief 按旧版规则识别并导入一组 BIN/TXT，可把 ERNC_TF 实部和虚部一次送入曲线分析。
    /// @param sources 文件列表。整区按精确长度识别；同长度子字段优先用文件名和当前选择消歧。
    /// @return 每个文件都识别并载入时成功。失败文件不会覆盖此前已经完整导入的数据。
    ernc::domain::Result<void> importFiles(const std::vector<std::filesystem::path>& sources);

    /// @brief 对当前字节做写预览，不入队。
    /// @return 有完整字节时为预览。没有字节时为 InvalidParameter。
    ernc::domain::Result<ernc::application::RegionWritePreview> previewSelected();

    /// @brief 在已有预览后调用 RegionWriter::start。
    /// @return 没有预览时为 InvalidParameter，并且不调用 start。
    /// 正式目录上的区域会拒绝开始写，不入队。
    ernc::domain::Result<void> writeSelected();

    /// @brief 把当前完整字节导出为 BIN。
    /// @param destination 新的 .bin 路径。不修改导入来源。
    /// @return 字节长度等于区域 size 时成功。否则为 InvalidParameter，不创建文件。
    ernc::domain::Result<void> exportBin(const std::filesystem::path& destination);

    /// @brief 把当前完整字节导出成旧版 TXT。整区带三行头，子字段只有 value, 行。
    ernc::domain::Result<void> exportTxt(const std::filesystem::path& destination);

    /// @brief 拒绝把取消记录导出成 BIN。
    /// @param record 不完整传输记录。canSaveAsCompleteBin 为 false。
    /// @param destination 本来要写的路径。本函数不创建它。
    /// @return 恒为 InvalidParameter。
    ernc::domain::Result<void> exportCancelled(const ernc::application::RegionTransferRecord& record,
                                               const std::filesystem::path& destination);

    /// @brief 取消当前读或写。
    /// 不调用调度器的 cancel()。完整快照不会被拆掉。
    void cancelTransfer();

    /// @brief 当前状态文字。
    /// @return 空闲、已导入尚未写入、预览或拒绝原因。
    QString statusText() const;

    /// @brief 当前进度文字。
    /// @return 没有在途传输时为「空闲」。
    QString progressText() const;

    /// @brief 写入动作现在能不能按。
    /// @return 已有一份对应当前字节的预览时为 true。仅导入时为 false。
    bool writeActionEnabled() const noexcept;

    /// @brief 谱半径表是否正在显示。
    /// @return 当前选择是 120 点 ERNC_TF.SpecRadius 时为 true。
    bool specRadiusVisible() const noexcept;

    /// @brief 谱半径表的行数。
    /// @return 表可见时为 120。不可见时为 0。
    int specRadiusRows() const noexcept;

    /// @brief 有完整可读字节时通知分析页。
    /// @param handler 回调。空表示不再通知。当前区域定义与字节只在回调期间有效。
    void setBytesAvailableHandler(
        std::function<void(const ernc::domain::RegionDefinition&, std::span<const std::uint8_t>)> handler);

    /// @brief 记录 UI 操作用于会话日志复现。
    /// @param recorder 收到 action、detail JSON、成败和错误码。空表示不记录。
    void setUiActivityRecorder(
        std::function<void(std::string_view action, std::string_view detail, bool success, ernc::domain::ErrorCode error)>
            recorder);

    /// @brief 绑定设备读取入口。回调由桌面壳提供，工作区不依赖串口或 Worker 实现。
    void setReadHandlers(std::function<ernc::domain::Result<void>(const QString&)> start,
                         std::function<void()> cancel);

    /// @brief 接收设备门面发来的大区域读取进度。
    void noteReadProgress(const QString& path, quint64 filledBytes, quint64 plannedBytes);

    /// @brief 接收设备门面发来的完整结果。失败或取消不会发布部分文件。
    void noteReadFinished(bool success, const QString& path, const QByteArray& bytes, int errorCode);

    /// @brief 绑定设备写入入口（preview 仍可在本地用 `previewRegionTransfer`）。
    void setWriteHandlers(std::function<ernc::domain::Result<void>(const QString& path, const QByteArray& bytes)> start,
                          std::function<void()> cancel);

    /// @brief 把区域范围、状态与进度汇总到主窗口状态栏等外部展示。
    /// @param sink 收到一行纯文本摘要。空函数表示只更新内部 `statusText` / `progressText`。
    void setStatusSink(std::function<void(const QString&)> sink);

    /// @brief 用当前范围与状态/进度重新推送状态栏摘要。
    void refreshStatusPresentation();

    /// @brief 接收设备门面发来的大区域写入进度。
    void noteWriteProgress(const QString& path, quint64 verifiedBytes, quint64 plannedBytes);

    /// @brief 接收设备门面发来的区域写结束。失败或取消不假装 Verified。
    void noteWriteFinished(bool success, const QString& path, int errorCode);

private:
    void notifyUiAction(std::string_view action, std::string_view detail, bool success, ernc::domain::ErrorCode error);

    /// 导入完成后的通知。空表示不通知。
    std::function<void(const ernc::domain::RegionDefinition&, std::span<const std::uint8_t>)> bytesHandler_;

    /// 会话 UI 记录。空表示不记录。
    std::function<void(std::string_view action, std::string_view detail, bool success, ernc::domain::ErrorCode error)>
        uiRecorder_;

    /// @brief 导入成功后把字节交给已注册的处理器。
    void emitBytesAvailable();

    /// @brief 按当前区域和子字段确定要操作的定义。
    /// @return 有选择时指向内部副本。没有时为空。
    const ernc::domain::RegionDefinition* selectedRegion() const noexcept;

    /// @brief 清掉导入字节和预览，并刷新按钮。
    void clearImported();

    /// @brief 重建子字段列表和中央页面。
    void refreshSelection();

    /// @brief 取当前要预览或导出的字节。
    /// @return 谱半径 Set 齐全时用 Set。否则用已导入字节。长度不符时为空。
    std::optional<std::vector<std::uint8_t>> currentBytes() const;

    /// @brief 按读或写的进度刷新文字。
    void refreshProgress();

    /// @brief 当前数据范围的一行摘要（区域 · 字段）。
    QString currentScopeSummary() const;

    /// @brief 根据 `status_`、`progress_` 与范围刷新外部状态栏。
    void publishRegionStatus();

    /// @brief 切换大区域传输期间的控件可用性。
    void setReadRunning(bool running);
    void setWriteRunning(bool running);
    void setTransferRunning(bool running);

    /// 区域目录。本工作区不拥有。
    const ernc::domain::RegionCatalog& catalog_;

    /// 区域读。本工作区不拥有。
    ernc::application::RegionReader* reader_;

    /// 区域写。本工作区不拥有。
    ernc::application::RegionWriter* writer_;

    /// 设备读开始/取消回调。串口线程归属由调用方保证。
    std::function<ernc::domain::Result<void>(const QString&)> startRead_;
    std::function<void()> cancelRead_;

    /// 设备写开始/取消回调。
    std::function<ernc::domain::Result<void>(const QString&, const QByteArray&)> startWrite_;
    std::function<void()> cancelWrite_;

    /// 当前大区域写路径。
    QString activeWritePath_;

    /// 大区域写进行中。
    bool writeRunning_{false};

    /// 当前区域路径。
    QString regionPath_;

    /// 当前子字段路径。空表示整段。
    QString fieldPath_;

    /// 当前操作对象的副本。
    std::optional<ernc::domain::RegionDefinition> selected_;

    /// 已导入或尚未写入的字节。
    std::optional<std::vector<std::uint8_t>> imported_;

    /// 预览所针对的字节。与当前字节不一致时写入不可用。
    std::optional<std::vector<std::uint8_t>> previewBytes_;

    /// 最近一次预览。
    std::optional<ernc::application::RegionWritePreview> preview_;

    /// 状态文字。
    QString status_;

    /// 进度文字。
    QString progress_;

    /// 区域下拉框。由 Qt 父子关系拥有。
    QComboBox* regionBox_;

    /// 子字段下拉框。由 Qt 父子关系拥有。
    QComboBox* fieldBox_;

    /// 汇总状态展示回调。不拥有。
    std::function<void(const QString&)> statusSink_;

    /// 西门子式确定进度条：以 4-byte slot 为单位显示已完成/总量。
    QProgressBar* progressBar_;

    QPushButton* readButton_;
    QPushButton* importButton_;
    QPushButton* exportButton_;
    QPushButton* cancelButton_;

    /// 写入按钮。没有预览时不可用。
    QPushButton* writeButton_;

    /// 中央页面栈。
    QStackedWidget* stack_;

    /// 谱半径页。由 Qt 父子关系拥有。不是谱半径时隐藏。
    SpecRadiusPage* specPage_;

    /// 为 true 时锁住范围和文件动作，直到完成、失败或取消。
    bool readRunning_{false};

    /// 本次设备读取的完整路径，用于拒绝过期信号。
    QString activeReadPath_;
};

}  // namespace ernc::presentation
