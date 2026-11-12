#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::presentation {

/// 窄屏分界。宽度小于它时收起项目树。单位是逻辑像素，不是物理像素。
inline constexpr int kShellCompactWidth = 1280;

/// 宽屏分界。达到它时显示右侧任务卡和操作中心。单位是逻辑像素。
inline constexpr int kShellWideWidth = 1600;

/// 默认窗口宽度上限。单位是逻辑像素。不超过屏幕可用宽度。
inline constexpr int kShellMaxWidth = 1920;

/// 默认窗口高度上限。单位是逻辑像素。不超过屏幕可用高度。
inline constexpr int kShellMaxHeight = 1080;

/// `QMainWindow::saveState` 使用的版本。恢复时版本不同则拒绝这份状态。
inline constexpr int kShellLayoutVersion = 1;

/// 四个停靠区是否显示。这里不保存它们的像素位置。
struct DockVisibility {
    /// 左侧项目树。
    bool projectTree = false;

    /// 右侧任务卡。
    bool taskCards = false;

    /// 与任务卡叠在一起的操作中心。
    bool operationCenter = false;

    /// 底部检查器。窄屏也保持可访问。
    bool inspector = false;
};

/// 预置要切换的编辑页。`Unchanged` 表示不动当前页签。
enum class ShellEditorPage {
    /// 保持当前页签。
    Unchanged,

    /// 切到 Function View。
    FunctionView,

    /// 切到 Parameter View。
    ParameterView,

    /// 切到曲线分析页。
    CurveAnalysis
};

/// 预置要切换的检查器页。`Unchanged` 表示不动当前标签。
enum class ShellInspectorPage {
    /// 保持当前标签。
    Unchanged,

    /// 切到 Properties。
    Properties,

    /// 切到 Diagnostics。
    Diagnostics
};

/// 一次可执行的工作区布局。它只描述开关和页签，不保存参数值。
struct WorkspaceLayout {
    /// 四个停靠区的可见性。
    DockVisibility docks;

    /// 右侧两个停靠区都可见时，是否把操作中心抬到前面。否则抬起任务卡。
    bool raiseOperationCenter = false;

    /// 要切换的编辑页。
    ShellEditorPage editorPage = ShellEditorPage::Unchanged;

    /// 要切换的检查器标签。
    ShellInspectorPage inspectorPage = ShellInspectorPage::Unchanged;
};

/// 三个预置工作区。曲线分析打开曲线分析页签。
enum class WorkspacePreset {
    /// 参数调试。显示项目树、任务卡和检查器，并打开 Parameter View。
    ParameterDebug,

    /// 曲线分析。保留项目树，并打开曲线分析页。
    CurveAnalysis,

    /// 故障诊断。打开 Diagnostics，右侧停在操作中心。
    FaultDiagnosis
};

/// 一条快捷键。保留项只记在表里，程序不绑定。
struct KeyBinding {
    /// Qt 能识别的按键文本，例如 `Ctrl+1`。
    std::string key;

    /// 动作名。不是按钮上的中文。
    std::string action;

    /// 为 true 时不创建快捷键。工程打开和保存属于这类。
    bool reserved = false;
};

/// 快捷键表的解析结果。`error` 为空表示成功。
struct ShellKeymap {
    /// 成功时的全部行，含保留项。失败时为空。
    std::vector<KeyBinding> bindings;

    /// 失败原因。成功时为空字符串。
    std::string error;
};

/// 屏幕可用区域裁剪后的窗口大小。单位是逻辑像素。
struct WindowSize {
    /// 窗口宽度。
    int width = kShellCompactWidth;

    /// 窗口高度。没有屏幕信息时用 800，这是原先的起步高度。
    int height = 800;
};

/// @brief 按逻辑宽度决定四个停靠区。
/// @param logicalWidth 窗口客户区宽度，单位是逻辑像素。不乘 DPI。
/// @return 检查器始终可见。1280 起显示项目树，1600 起显示右侧两个停靠区。
DockVisibility docksForWidth(int logicalWidth);

/// @brief 没有保存布局时使用的工作区。
/// @param logicalWidth 窗口客户区宽度，单位是逻辑像素。
/// @return 停靠区按 `docksForWidth`。不切换编辑页或检查器标签，右侧优先显示任务卡。
WorkspaceLayout layoutForWidth(int logicalWidth);

/// @brief 取出一个预置工作区。
/// @param preset 三个预置之一。
/// @return 该预置的停靠区和页签。不读取屏幕，也不写设置。
WorkspaceLayout presetLayout(WorkspacePreset preset);

/// @brief 把可用屏幕裁到 1920×1080 以内。
/// @param availableWidth 屏幕可用宽度，单位是逻辑像素。小于等于 0 时改用 1280。
/// @param availableHeight 屏幕可用高度，单位是逻辑像素。小于等于 0 时改用 800。
/// @return 不超过上限的窗口大小。调用方拥有返回值。
WindowSize defaultWindowSize(int availableWidth, int availableHeight);

/// @brief 解析快捷键表全文。
/// @param text 文件全文，只在本次调用期间借用。空行和以 `#` 开头的行被忽略。
/// @return 成功时 `error` 为空。任一行列数不对则整份失败，不返回部分结果。
ShellKeymap parseShellKeymap(std::string_view text);

/// @brief 从磁盘读取快捷键表并解析。
/// @param path 快捷键文件路径。调用方拥有。文件缺失或打不开时失败，不抛异常。
/// @return 解析结果。失败时 `bindings` 为空。
ShellKeymap loadShellKeymapFile(const std::filesystem::path& path);

} // namespace ernc::presentation
