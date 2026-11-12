#include "shell/layout_settings.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using ernc::presentation::DockVisibility;
using ernc::presentation::ShellEditorPage;
using ernc::presentation::ShellInspectorPage;
using ernc::presentation::ShellKeymap;
using ernc::presentation::WorkspaceLayout;
using ernc::presentation::WorkspacePreset;
using ernc::presentation::docksForWidth;
using ernc::presentation::parseShellKeymap;
using ernc::presentation::presetLayout;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 核对一个宽度上的四个停靠区。
/// @param width 逻辑宽度，单位是逻辑像素。
/// @param projectTree 项目树是否显示。
/// @param rightDocks 任务卡和操作中心是否一起显示。
/// @param inspector 检查器是否显示。
/// @return 四个开关都符合时为 true。
bool expectWidth(int width, bool projectTree, bool rightDocks, bool inspector) {
    const DockVisibility docks = docksForWidth(width);
    return docks.projectTree == projectTree && docks.taskCards == rightDocks
        && docks.operationCenter == rightDocks && docks.inspector == inspector;
}

/// @brief 1279、1280、1366、1599、1600 和 1920 的停靠区组合。
/// @return 六档都符合宽度规则时为 true。
bool widthBands() {
    if (!expectWidth(1279, false, false, true) || !expectWidth(1280, true, false, true)
        || !expectWidth(1366, true, false, true) || !expectWidth(1599, true, false, true)
        || !expectWidth(1600, true, true, true) || !expectWidth(1920, true, true, true)) {
        return fail("width_bands");
    }
    return true;
}

/// @brief 核对一个预置的停靠区、抬起的页签和两个编辑页。
/// @param layout 预置结果。调用方拥有。
/// @param projectTree 项目树是否显示。
/// @param rightDocks 右侧两个停靠区是否显示。
/// @param inspector 检查器是否显示。
/// @param raiseOperationCenter 是否把操作中心抬到任务卡前面。
/// @param editorPage 要切换的编辑页。
/// @param inspectorPage 要切换的检查器标签。
/// @return 每一项都相同才为 true。
bool samePreset(
    const WorkspaceLayout& layout,
    bool projectTree,
    bool rightDocks,
    bool inspector,
    bool raiseOperationCenter,
    ShellEditorPage editorPage,
    ShellInspectorPage inspectorPage) {
    return layout.docks.projectTree == projectTree && layout.docks.taskCards == rightDocks
        && layout.docks.operationCenter == rightDocks && layout.docks.inspector == inspector
        && layout.raiseOperationCenter == raiseOperationCenter && layout.editorPage == editorPage
        && layout.inspectorPage == inspectorPage;
}

/// @brief 三个预置只改停靠区和页签。
/// @return 参数调试、曲线分析和故障诊断都符合约定时为 true。
bool presets() {
    if (!samePreset(presetLayout(WorkspacePreset::ParameterDebug), true, true, true, false,
            ShellEditorPage::ParameterView, ShellInspectorPage::Properties)
        || !samePreset(presetLayout(WorkspacePreset::CurveAnalysis), true, false, false, false,
            ShellEditorPage::CurveAnalysis, ShellInspectorPage::Unchanged)
        || !samePreset(presetLayout(WorkspacePreset::FaultDiagnosis), true, true, true, true,
            ShellEditorPage::Unchanged, ShellInspectorPage::Diagnostics)) {
        return fail("presets");
    }
    return true;
}

/// @brief 在快捷键表里找一个动作。
/// @param map 已解析的表。调用方拥有。
/// @param action 动作名，只借用。
/// @return 找到时指向表内那一项。未找到时为空，调用方不拥有。
const ernc::presentation::KeyBinding* findAction(const ShellKeymap& map, std::string_view action) {
    for (const ernc::presentation::KeyBinding& binding : map.bindings) {
        if (binding.action == action) {
            return &binding;
        }
    }
    return nullptr;
}

/// @brief Esc 只关闭浮层，表里没有取消写入。
/// @param path 快捷键文件路径。调用方拥有。打不开时失败。
/// @return Esc 是未保留的 `close-overlay`，且没有 `cancel-write` 时为 true。
bool keymapEscDoesNotCancel(const char* path) {
    std::ifstream input(path);
    if (!input) {
        return fail("keymap_esc_does_not_cancel");
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const ShellKeymap map = parseShellKeymap(text);
    if (!map.error.empty() || findAction(map, "cancel-write") != nullptr) {
        return fail("keymap_esc_does_not_cancel");
    }
    const ernc::presentation::KeyBinding* escape = findAction(map, "close-overlay");
    const ernc::presentation::KeyBinding* openProject = findAction(map, "open-project");
    const ernc::presentation::KeyBinding* saveProject = findAction(map, "save-project");
    if (escape == nullptr || escape->key != "Esc" || escape->reserved || openProject == nullptr
        || !openProject->reserved || saveProject == nullptr || !saveProject->reserved) {
        return fail("keymap_esc_does_not_cancel");
    }
    return true;
}

} // namespace

/// @brief 按用例名运行布局和快捷键检查。
/// @param argc 参数个数，含程序名。快捷键用例还要一个文件路径。
/// @param argv 程序名、用例名，以及快捷键用例的文件路径。路径由 CTest 传入，本函数只借用。
/// @return 通过时为 0，未知用例或失败时为 1。
int main(int argc, char** argv) {
    if (argc < 2) {
        return 1;
    }
    const std::string_view name = argv[1];
    if (name == "width_bands" && argc == 2) {
        return widthBands() ? 0 : 1;
    }
    if (name == "presets" && argc == 2) {
        return presets() ? 0 : 1;
    }
    if (name == "keymap_esc_does_not_cancel" && argc == 3) {
        return keymapEscDoesNotCancel(argv[2]) ? 0 : 1;
    }
    return 1;
}
