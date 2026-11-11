#include "operation_center/operation_log.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::presentation::OperationLog;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 连续追加 1000 行后只保留最新的 200 行。
/// @return 行数是 200、最旧一行是第 801 行，且全文没有 `0.05` 时为 true。
bool keepsNewestLines() {
    OperationLog log;
    for (int number = 1; number <= 1000; ++number) {
        log.append("行 " + std::to_string(number));
    }
    if (log.size() != OperationLog::kCapacity || log.lineAt(0) != "行 801" || log.lineAt(199) != "行 1000") {
        return fail("keeps_newest_lines");
    }
    if (log.text().find("0.05") != std::string::npos) {
        return fail("keeps_newest_lines");
    }
    return true;
}

} // namespace

/// @brief 按用例名运行操作日志检查。
/// @param argc 参数个数，含程序名。
/// @param argv 程序名和用例名。用例名由 CTest 传入，本函数只借用。
/// @return 通过时为 0，未知用例或失败时为 1。
int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const std::string_view name = argv[1];
    if (name == "keeps_newest_lines") {
        return keepsNewestLines() ? 0 : 1;
    }
    return 1;
}
