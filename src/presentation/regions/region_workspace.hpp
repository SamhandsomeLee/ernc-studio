#pragma once

#include <QWidget>

#ifdef slots
#undef slots
#endif

#include "ernc/application/region_reader.hpp"
#include "ernc/application/region_writer.hpp"
#include "ernc/application/transfer_cancellation.hpp"
#include "ernc/domain/region_catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
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

    /// 区域目录。本工作区不拥有。
    const ernc::domain::RegionCatalog& catalog_;

    /// 区域读。本工作区不拥有。
    ernc::application::RegionReader* reader_;

    /// 区域写。本工作区不拥有。
    ernc::application::RegionWriter* writer_;

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

    /// 状态标签。
    QLabel* statusLabel_;

    /// 进度标签。
    QLabel* progressLabel_;

    /// 写入按钮。没有预览时不可用。
    QPushButton* writeButton_;

    /// 中央页面栈。
    QStackedWidget* stack_;

    /// 谱半径页。由 Qt 父子关系拥有。不是谱半径时隐藏。
    SpecRadiusPage* specPage_;
};

}  // namespace ernc::presentation
