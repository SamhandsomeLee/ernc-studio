#include "shell/layout_settings.hpp"

#include <cctype>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace ernc::presentation {
namespace {

/// @brief 去掉两端空白。
/// @param text 原文本，只借用。
/// @return 调用方拥有的去空白文本。全空白时为空字符串。
std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

/// @brief 按空白把一行拆成字段。
/// @param line 已经去掉两端空白的一行。只借用。
/// @return 调用方拥有的字段。连续空白算一个分隔。
std::vector<std::string> splitFields(std::string_view line) {
    std::istringstream input{std::string(line)};
    std::vector<std::string> fields;
    std::string field;
    while (input >> field) {
        fields.push_back(std::move(field));
    }
    return fields;
}

/// @brief 解析一行快捷键。
/// @param line 已经去掉两端空白的一行。只借用。注释和空行不会传到这里。
/// @param binding 成功时写入这一行。调用方拥有。
/// @param error 失败时写入原因。调用方拥有。
/// @return 这一行可以放进快捷键表时为 true。
bool parseBindingLine(std::string_view line, KeyBinding& binding, std::string& error) {
    const std::vector<std::string> fields = splitFields(line);
    if (fields.size() == 2) {
        binding.key = fields[0];
        binding.action = fields[1];
        binding.reserved = false;
        return true;
    }
    if (fields.size() == 3 && fields[2] == "reserved") {
        binding.key = fields[0];
        binding.action = fields[1];
        binding.reserved = true;
        return true;
    }
    error = "快捷键行无法解析。";
    return false;
}

} // namespace

DockVisibility docksForWidth(int logicalWidth) {
    DockVisibility docks;
    docks.inspector = true;
    docks.projectTree = logicalWidth >= kShellCompactWidth;
    docks.taskCards = logicalWidth >= kShellWideWidth;
    docks.operationCenter = logicalWidth >= kShellWideWidth;
    return docks;
}

WorkspaceLayout layoutForWidth(int logicalWidth) {
    WorkspaceLayout layout;
    layout.docks = docksForWidth(logicalWidth);
    layout.raiseOperationCenter = false;
    layout.editorPage = ShellEditorPage::Unchanged;
    layout.inspectorPage = ShellInspectorPage::Unchanged;
    return layout;
}

WorkspaceLayout presetLayout(WorkspacePreset preset) {
    WorkspaceLayout layout;
    layout.docks.projectTree = true;
    layout.docks.inspector = preset != WorkspacePreset::CurveAnalysis;
    layout.docks.taskCards = preset != WorkspacePreset::CurveAnalysis;
    layout.docks.operationCenter = preset != WorkspacePreset::CurveAnalysis;
    layout.raiseOperationCenter = preset == WorkspacePreset::FaultDiagnosis;
    if (preset == WorkspacePreset::ParameterDebug) {
        layout.editorPage = ShellEditorPage::ParameterView;
        layout.inspectorPage = ShellInspectorPage::Properties;
    } else if (preset == WorkspacePreset::CurveAnalysis) {
        layout.editorPage = ShellEditorPage::FunctionView;
        layout.inspectorPage = ShellInspectorPage::Unchanged;
    } else {
        layout.editorPage = ShellEditorPage::Unchanged;
        layout.inspectorPage = ShellInspectorPage::Diagnostics;
    }
    return layout;
}

WindowSize defaultWindowSize(int availableWidth, int availableHeight) {
    WindowSize size;
    size.width = availableWidth > 0 ? std::min(availableWidth, kShellMaxWidth) : kShellCompactWidth;
    size.height = availableHeight > 0 ? std::min(availableHeight, kShellMaxHeight) : 800;
    return size;
}

ShellKeymap parseShellKeymap(std::string_view text) {
    ShellKeymap map;
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t newline = text.find('\n', cursor);
        const std::string_view raw = text.substr(cursor, newline == std::string_view::npos ? std::string_view::npos : newline - cursor);
        cursor = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        const std::string line = trim(raw);
        if (line.empty() || line.front() == '#') {
            if (newline == std::string_view::npos) {
                break;
            }
            continue;
        }
        KeyBinding binding;
        if (!parseBindingLine(line, binding, map.error)) {
            map.bindings.clear();
            return map;
        }
        map.bindings.push_back(std::move(binding));
        if (newline == std::string_view::npos) {
            break;
        }
    }
    return map;
}

ShellKeymap loadShellKeymapFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        ShellKeymap map;
        map.error = "无法读取快捷键表。";
        return map;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return parseShellKeymap(text);
}

} // namespace ernc::presentation
