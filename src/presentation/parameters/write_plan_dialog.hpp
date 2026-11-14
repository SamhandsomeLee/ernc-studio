#pragma once

#include "ernc/application/batch_write.hpp"
#include "ernc/domain/types.hpp"

#include <QDialog>
#include <QString>

#include <optional>
#include <vector>

class QCloseEvent;
class QLabel;
class QPushButton;
class QTableWidget;

namespace ernc::application {
class CommandScheduler;
class ParameterBatchWriter;
}  // namespace ernc::application

namespace ernc::domain {
class FirmwareLayout;
class ParameterCatalog;
class ParameterRuntimeStore;
}  // namespace ernc::domain

namespace ernc::presentation {

class DeviceController;

/// 写入预览表的列。顺序固定，不随搜索或表头排序改变。
enum class WritePlanColumn : int {
    /// 目录主键。
    Parameter = 0,

    /// 固件路径。不是 slot，也不是寄存器地址。
    Path = 1,

    /// 设备读数。没有读数时为「—」。过期读数会附上「数据已过期」。
    Read = 2,

    /// 用户暂存的目标。开关显示开、关或 Unknown。
    Set = 3,

    /// 排除原因，或进行中/写入已确认/读回不一致/失败等界面文字。
    Status = 4,

    /// 失败时的 ErrorCode 名字。进行中可以是命令号。空表示当前没有诊断。
    Diagnostic = 5
};

/// 当前分类或全工程的写入预览。确认前不发送；确认后只反映本次批量写的阶段。
/// 本对话框在 UI 主线程创建和使用。它不持有串口，也不组装 Modbus 帧。
class WritePlanDialog final : public QDialog {
public:
    /// 打开预览时借用的目录、状态和布局。指针都不由本对话框拥有。
    struct Sources {
        /// 参数目录。空指针时不生成计划。调用方保证它比本对话框活得久。
        const ernc::domain::ParameterCatalog* catalog = nullptr;

        /// 运行状态。空指针时不生成计划。调用方保证它比本对话框活得久。
        ernc::domain::ParameterRuntimeStore* store = nullptr;

        /// 固件布局。空指针时不生成计划。调用方保证它比本对话框活得久。
        const ernc::domain::FirmwareLayout* layout = nullptr;

        /// 项目树页面 id。`ALL` 表示全目录 dirty 标量；空文本不能预览。其它文本当作目录分类。
        QString pageId;
    };

    /// @brief 按当前页面生成一次写入预览。
    /// @param sources 目录、状态、布局和页面 id。指针只借用。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本对话框。必须在 UI 主线程创建。
    /// @note 构造只读 Store 和布局，不创建会话，也不发送 FC16。
    explicit WritePlanDialog(Sources sources, QWidget* parent = nullptr);

    /// @brief 绑定已经存在的批量写和它使用的调度器。
    /// @param writer 批量写，只借用。空指针表示还没有会话，确认时不发送。
    /// @param scheduler 与 writer 同一条端口的调度器，只借用。空指针时确认不发送。
    /// @note 本对话框不调用 pump()。调用方在调度器所属线程上推进命令后，再调用 noteProgress()。
    /// 当前产品窗口不绑定二者，因此不会打开串口。
    void setWriter(ernc::application::ParameterBatchWriter* writer, ernc::application::CommandScheduler* scheduler);

    /// @brief 确认当前预览。
    /// @note 未绑定写对象时只显示 DeviceDisconnected，不改各行的待写入状态。
    /// 已绑定时把打开时的分类交给 ParameterBatchWriter。这不是设备 ACK，也不是读回 Verified。
    /// 只在 UI 主线程调用。写对象若绑定，也必须能在这个线程上接受 start()。
    void confirmWrite();

    /// @brief 按批量写的已有结果和调度器阶段刷新状态列。
    /// @note 不发送命令，也不取走调度器结果。没有绑定写对象时什么都不做。只在 UI 主线程调用。
    void noteProgress();

    /// @brief 在 UI 主线程刷新当前批次的进度及结果。超时保留 Set，并提示先读回再重试。
    /// @param items 批量写结果快照，只在本次调用期间借用。
    /// @param inflight 已提交给调度器、尚无终态的命令编号，空表示没有在途写入。
    /// @param phase 在途命令的写入阶段，空表示没有可显示的 ACK 或读回阶段。
    void noteProgressFromWorker(const std::vector<ernc::application::BatchWriteItem>& items,
                                const std::optional<ernc::application::CommandId>& inflight,
                                const std::optional<ernc::application::WritePhase>& phase);

    /// @brief 绑定设备门面。串口模式用 remote 写入，本地 writer 可空。
    void setDeviceController(DeviceController* controller) noexcept;

    bool writeBusy() const;

    /// @brief 预览行数。
    /// @return 表格行数。没有待写参数时为 0。
    int planRowCount() const;

    /// @brief 按参数 id 查找预览行。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 找到时为行号。没有时为 -1。
    int rowOf(const QString& parameterId) const;

    /// @brief 读取一个单元格的显示文字。
    /// @param row 预览行。越界时返回空字符串。
    /// @param column WritePlanColumn 的整数值。未知列返回空字符串。
    /// @return 单元格文字。没有单元格时为空字符串。
    QString cell(int row, int column) const;

    /// @brief 确认按钮当前能否点击。
    /// @return 范围内至少有一个 1 slot 标量待写时为 true。空页面和只有排除项时为 false。
    bool confirmEnabled() const;

    /// @brief 对话框顶部的诊断文字。
    /// @return 未连接确认后为 DeviceDisconnected。没有诊断时为空。
    QString diagnosticText() const;

protected:
    /// @brief Esc 或关闭按钮不取消已入队的写入。操作进行时继续显示本批结果。
    void reject() override;

    /// @brief 写入还在进行时不关闭对话框。
    /// @param event Qt 关闭事件。写入未完成时忽略它，不把在途命令标成 Verified。
    /// @note 忽略关闭也不调用调度器的 cancel()。只在 UI 主线程调用。
    void closeEvent(QCloseEvent* event) override;

private:
    /// 预览里的一行。对象拥有参数标识副本。
    struct PlanRow {
        /// 目录主键。
        ernc::domain::ParameterId id;

        /// 为 true 时，确认后可以交给批量写。排除项和多 slot 为 false。
        bool send{false};
    };

    /// @brief 按页面和当前 Store 重做预览。
    /// @note 不发送命令。会清空上一份表格。只在 UI 主线程调用。
    void rebuild();

    /// 打开时借用的对象和页面 id。指针不拥有。
    Sources sources_;

    /// 为 true 时，sources_.pageId 是可以交给批量写的目录分类。
    bool scopeReady_{false};

    /// 仅在本对话框已成功启动写入后接收进度，避免上一次写入结果覆盖预览。
    bool started_{false};

    /// 与表格行一一对应的参数。本对话框拥有。
    std::vector<PlanRow> rows_;

    /// 批量写。不拥有。空表示确认不会发送。
    ernc::application::ParameterBatchWriter* writer_ = nullptr;

    /// 调度器。不拥有。只用来查看在途写的阶段。
    ernc::application::CommandScheduler* scheduler_ = nullptr;

    /// 串口等设备门面。不拥有。
    DeviceController* deviceController_{nullptr};

    /// 当前页面名称。由本对话框通过 Qt 父子关系拥有。
    QLabel* scopeLabel_ = nullptr;

    /// 整次确认的诊断。由本对话框通过 Qt 父子关系拥有。
    QLabel* diagnosticLabel_ = nullptr;

    /// Read、Set 和状态表。由本对话框通过 Qt 父子关系拥有。
    QTableWidget* table_ = nullptr;

    /// 确认按钮。由本对话框通过 Qt 父子关系拥有。
    QPushButton* confirmButton_ = nullptr;
};

}  // namespace ernc::presentation
