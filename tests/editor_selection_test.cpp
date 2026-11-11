#include "workspace/editor_selection.hpp"

#include <iostream>
#include <string_view>

namespace {

using ernc::presentation::EditorSelection;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 初始没有页面，也没有参数。
/// @return 两个 id 都为空时为 true。
bool initialEmpty() {
    const EditorSelection selection;
    if (!selection.pageId().empty() || !selection.parameterId().empty()) {
        return fail("initial_empty");
    }
    return true;
}

/// @brief 选中页面不清除已经选中的参数。
/// @return 参数 id 保持 `check_data` 且页面变为 `other` 时为 true。
bool pageKeepsParameter() {
    EditorSelection selection;
    selection.selectParameter("check_data");
    selection.selectPage("other");
    if (selection.parameterId() != "check_data" || selection.pageId() != "other") {
        return fail("page_keeps_parameter");
    }
    selection.selectPage("ERNC_Flag");
    if (selection.parameterId() != "check_data" || selection.pageId() != "ERNC_Flag") {
        return fail("page_keeps_parameter");
    }
    return true;
}

/// @brief 新的参数 id 替换旧的，空文本不清除。
/// @return 空选择被忽略时为 true。
bool emptyDoesNotClear() {
    EditorSelection selection;
    selection.selectParameter("check_data");
    selection.selectPage("other");
    selection.selectParameter("");
    selection.selectPage("");
    if (selection.parameterId() != "check_data" || selection.pageId() != "other") {
        return fail("empty_does_not_clear");
    }
    selection.selectParameter("ANC_OFF");
    if (selection.parameterId() != "ANC_OFF" || selection.pageId() != "other") {
        return fail("empty_does_not_clear");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name 用例名，只在本次调用期间借用。
/// @param recognized 识别到用例名时设为 true。调用方拥有。
/// @return 用例通过时为 true。未识别的名字也返回 false，并由 `recognized` 区分。
bool runCase(const char* name, bool& recognized) {
    recognized = true;
    const std::string_view caseName(name);
    if (caseName == "initial_empty") {
        return initialEmpty();
    }
    if (caseName == "page_keeps_parameter") {
        return pageKeepsParameter();
    }
    if (caseName == "empty_does_not_clear") {
        return emptyDoesNotClear();
    }
    recognized = false;
    return false;
}

} // namespace

/// @brief 运行一个编辑选择用例。
/// @param argc 参数个数。必须是 2。
/// @param argv `argv[1]` 是用例名。只在本次调用期间借用。
/// @return 通过返回 0，断言失败返回 1，用法错误返回 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_editor_selection_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
