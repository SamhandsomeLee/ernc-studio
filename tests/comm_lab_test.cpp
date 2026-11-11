#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// 等待实验台退出的上限，单位是毫秒。一次内存读应远早于此结束。
constexpr DWORD kLabWaitMs = 20000;

/// @brief 子进程运行结果。退出码只有在 finished 为 true 时才有意义。
struct ChildResult {
    /// CreateProcessA 已创建进程时为 true。创建失败时为 false。
    bool started{false};

    /// 在 kLabWaitMs 内等到进程结束，并且读到了退出码时为 true。
    bool finished{false};

    /// 等待超时后已终止子进程时为 true。此时退出码不作断言依据。
    bool timedOut{false};

    /// GetExitCodeProcess 的退出码。实验台用 0、1、2、3 表示不同结果。
    unsigned long exitCode{0};

    /// 创建或等待失败时的 Win32 错误码。0 表示没有记录错误。
    unsigned long winError{0};

    /// 子进程标准输出和标准错误合并后的全部字节。调用方拥有。
    std::string output;
};

/// @brief 关闭一个 Win32 句柄，并把句柄置空，避免重复关闭。
/// @param handle 要关闭的句柄。nullptr 和 INVALID_HANDLE_VALUE 会被忽略。函数返回后该引用变为 nullptr。
void closeHandle(HANDLE& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

/// @brief 按 Windows 命令行规则给一个参数加引号。
/// @param token 原始参数。路径里的空格和反斜杠都按原字符保留，只在本次调用期间借用。
/// @return 调用方拥有的带引号文本。空 token 得到一对引号。
std::string quoteCommandToken(const std::string& token) {
    std::string quoted;
    quoted.reserve(token.size() + 2);
    quoted.push_back('"');
    for (std::size_t index = 0;; ++index) {
        unsigned backslashes = 0;
        while (index < token.size() && token[index] == '\\') {
            ++index;
            ++backslashes;
        }
        if (index >= token.size()) {
            quoted.append(static_cast<std::size_t>(backslashes) * 2U, '\\');
            break;
        }
        if (token[index] == '"') {
            quoted.append(static_cast<std::size_t>(backslashes) * 2U + 1U, '\\');
            quoted.push_back('"');
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(token[index]);
        }
    }
    quoted.push_back('"');
    return quoted;
}

/// @brief 拼出传给 CreateProcessA 的可写命令行。可执行文件路径始终加引号。
/// @param labPath 实验台可执行文件路径。可以含空格，只在本次调用期间借用。
/// @param arguments 实验台参数，不含程序名。每个元素只在本次调用期间借用。
/// @return 调用方拥有的命令行。第一段是加引号的 labPath。
std::string buildLabCommandLine(const std::string& labPath, const std::vector<std::string>& arguments) {
    std::string command = quoteCommandToken(labPath);
    for (const std::string& argument : arguments) {
        command.push_back(' ');
        command += quoteCommandToken(argument);
    }
    return command;
}

/// @brief 删掉当前工作目录里可能残留的报告，避免读到上一次的文件。
/// @param path 相对当前工作目录的文件名。空字符串表示本用例没有报告文件。
/// @return 文件本来就不存在，或已经删除时为 true。
bool deleteOutputFile(std::string_view path) {
    if (path.empty()) {
        return true;
    }
    const std::string name{path};
    if (DeleteFileA(name.c_str()) != 0) {
        return true;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

/// @brief 读出实验台写出的报告文件。
/// @param path 相对当前工作目录的文件名，只在本次调用期间借用。
/// @param content 成功时替换为文件的全部字节。调用方拥有。打开失败时不修改。
/// @return 文件打开且没有 badbit 时为 true。文件不存在时为 false。
bool readOutputFile(std::string_view path, std::string& content) {
    const std::string name{path};
    std::ifstream input{name, std::ios::binary};
    if (!input) {
        return false;
    }
    const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        return false;
    }
    content = bytes;
    return true;
}

/// @brief 判断字符是不是 JSON 里可忽略的空白。
/// @param ch 报告中的一个字节。只接受空格、制表符、换行和回车。
/// @return 属于这四种空白时为 true。
bool isJsonSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/// @brief 判断文本去掉两端空白后是不是恰好一个 JSON 对象。
/// @param text 报告全文，只在本次调用期间借用。字符串内的花括号不算新对象。
/// @return 只有一个顶层对象、花括号配平且没有对象外的多余字符时为 true。
bool isSingleJsonObject(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && isJsonSpace(text[begin])) {
        ++begin;
    }
    if (begin >= text.size()) {
        return false;
    }
    std::size_t end = text.size();
    while (end > begin && isJsonSpace(text[end - 1])) {
        --end;
    }
    if (text[begin] != '{' || text[end - 1] != '}') {
        return false;
    }

    int depth = 0;
    int roots = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t index = begin; index < end; ++index) {
        const char ch = text[index];
        if (inString) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                ++roots;
            }
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth < 0) {
                return false;
            }
            continue;
        }
        if (depth == 0) {
            return false;
        }
    }
    return depth == 0 && roots == 1 && !inString;
}

/// @brief 把失败原因写到标准错误。
/// @param caseName 用例短名，只在本次调用期间借用。
/// @param detail 失败原因，只在本次调用期间借用。
/// @return 恒为 false，方便调用方直接 return。
bool failCase(std::string_view caseName, std::string_view detail) {
    std::cerr << "failed: " << caseName << ": " << detail << '\n';
    return false;
}

/// @brief 把子进程捕获到的输出补到标准错误，便于 CTest 显示。
/// @param output 合并后的标准输出和标准错误。只在本次调用期间借用。空字符串不写任何内容。
void writeCapturedOutput(const std::string& output) {
    if (output.empty()) {
        return;
    }
    std::cerr << output;
    if (output.back() != '\n') {
        std::cerr << '\n';
    }
}

/// @brief 用 CreateProcessA 运行实验台，等待结束并取回退出码和输出。
/// @param labPath 实验台可执行文件的完整路径。可以含空格，只在本次调用期间借用。
/// @param arguments 传给实验台的参数，不含程序名。调用方拥有，本次调用只读。
/// @return 调用方拥有的结果。启动失败时 started 为 false，并带上 Win32 错误码。
ChildResult runChild(const std::string& labPath, const std::vector<std::string>& arguments) {
    ChildResult result;
    std::string command = buildLabCommandLine(labPath, arguments);

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (CreatePipe(&readPipe, &writePipe, &inherit, 1U << 20U) == 0) {
        result.winError = GetLastError();
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nul == INVALID_HANDLE_VALUE) {
        result.winError = GetLastError();
        closeHandle(readPipe);
        closeHandle(writePipe);
        return result;
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;

    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(labPath.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                                        &startup, &process);
    closeHandle(writePipe);
    closeHandle(nul);
    if (created == 0) {
        result.winError = GetLastError();
        closeHandle(readPipe);
        return result;
    }

    result.started = true;
    const DWORD waited = WaitForSingleObject(process.hProcess, kLabWaitMs);
    if (waited == WAIT_OBJECT_0) {
        DWORD code = 0;
        if (GetExitCodeProcess(process.hProcess, &code) != 0) {
            result.finished = true;
            result.exitCode = code;
        } else {
            result.winError = GetLastError();
        }
    } else {
        result.timedOut = waited == WAIT_TIMEOUT;
        result.winError = GetLastError();
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
    }

    char buffer[4096];
    for (;;) {
        DWORD got = 0;
        const BOOL read = ReadFile(readPipe, buffer, sizeof(buffer), &got, nullptr);
        if (read == 0 || got == 0) {
            break;
        }
        result.output.append(buffer, buffer + got);
    }

    closeHandle(readPipe);
    closeHandle(process.hThread);
    closeHandle(process.hProcess);
    return result;
}

/// @brief 运行实验台并核对退出码；需要报告时再核对那一个 JSON 对象。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @param caseName 用例短名，只用于失败日志。
/// @param arguments 传给实验台的参数，不含程序名。字面量只在本次调用期间借用。
/// @param expectedExit 期望的进程退出码。2 和 3 在对应用例里是通过，不是检查器失败。
/// @param outputFile 相对当前工作目录的报告路径。空表示本用例不读文件。
/// @param needles 报告里必须出现的子串。outputFile 为空时忽略。
/// @param stdoutNeedle 捕获输出里必须出现的子串。空表示不检查输出文本。
/// @return 退出码和文本都符合期望时为 true。启动失败、超时或文本不符时为 false。
bool expectLab(const std::string& labPath, std::string_view caseName, std::initializer_list<std::string_view> arguments,
               unsigned long expectedExit, std::string_view outputFile, std::initializer_list<std::string_view> needles,
               std::string_view stdoutNeedle) {
    if (!deleteOutputFile(outputFile)) {
        return failCase(caseName, "cannot remove stale report");
    }

    std::vector<std::string> args;
    args.reserve(arguments.size());
    for (const std::string_view argument : arguments) {
        args.emplace_back(argument);
    }

    const ChildResult child = runChild(labPath, args);
    if (!child.started) {
        const std::string detail = "cannot start lab, win32 error " + std::to_string(child.winError);
        return failCase(caseName, detail);
    }
    if (!child.finished) {
        const std::string detail = child.timedOut ? "lab timed out" : "lab wait failed";
        failCase(caseName, detail);
        writeCapturedOutput(child.output);
        return false;
    }
    if (child.exitCode != expectedExit) {
        const std::string detail =
            "exit " + std::to_string(child.exitCode) + " expected " + std::to_string(expectedExit);
        failCase(caseName, detail);
        writeCapturedOutput(child.output);
        return false;
    }
    if (!stdoutNeedle.empty() && child.output.find(stdoutNeedle) == std::string::npos) {
        const std::string detail = std::string{"stdout missing "} + std::string{stdoutNeedle};
        failCase(caseName, detail);
        writeCapturedOutput(child.output);
        return false;
    }
    if (outputFile.empty()) {
        return true;
    }

    std::string report;
    if (!readOutputFile(outputFile, report)) {
        return failCase(caseName, "cannot read report");
    }
    if (!isSingleJsonObject(report)) {
        failCase(caseName, "report is not one JSON object");
        writeCapturedOutput(report);
        return false;
    }
    for (const std::string_view needle : needles) {
        if (report.find(needle) == std::string::npos) {
            const std::string detail = std::string{"report missing "} + std::string{needle};
            failCase(caseName, detail);
            writeCapturedOutput(report);
            return false;
        }
    }
    return true;
}

/// @brief 核对 --help 退出 0，并且标准输出里有 --transport。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。本用例不读 JSON 文件。
/// @return 退出码为 0 且输出包含 --transport 时为 true。
bool expectHelp(const std::string& labPath) {
    return expectLab(labPath, "help", {"--help"}, 0, {}, {}, "--transport");
}

/// @brief 核对模拟器只读运行：退出 0，报告是一个不允许写的 PASS 对象。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @return 报告含 allowWrite false、PASS、reads 1、writes 0 和数据 78563412 时为 true。
bool expectSimulatorReadonly(const std::string& labPath) {
    constexpr std::string_view kFile = "comm-lab-read.json";
    return expectLab(labPath, "simulator_readonly", {"--transport", "simulator", "--output", kFile}, 0, kFile,
                     {"\"allowWrite\":false", "\"status\":\"PASS\"", "\"reads\":1", "\"writes\":0",
                      "\"data\":\"78563412\""},
                     {});
}

/// @brief 核对传入 --allow-write 后仍然只读：允许写标志为 true，写次数仍是 0。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @return 退出码为 0，且报告含 allowWrite true、writes 0、reads 1 和数据 78563412 时为 true。
bool expectAllowWriteStillReads(const std::string& labPath) {
    constexpr std::string_view kFile = "comm-lab-allow-write.json";
    return expectLab(labPath, "allow_write_still_reads",
                     {"--transport", "simulator", "--allow-write", "--output", kFile}, 0, kFile,
                     {"\"allowWrite\":true", "\"writes\":0", "\"reads\":1", "\"data\":\"78563412\""}, {});
}

/// @brief 核对串口传输被阻断：实验台退出 3，报告是 BLOCKED 且没有读写。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @return 退出码为 3，且报告含 BLOCKED、reads 0、writes 0 时为 true。
bool expectSerialBlocked(const std::string& labPath) {
    constexpr std::string_view kFile = "comm-lab-serial.json";
    return expectLab(labPath, "serial_blocked", {"--transport", "serial", "--output", kFile}, 3, kFile,
                     {"\"status\":\"BLOCKED\"", "\"reads\":0", "\"writes\":0"}, {});
}

/// @brief 核对未知场景 soak 被拒绝，退出码为 2。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。本用例不读 JSON 文件。
/// @return 实验台退出 2 时为 true。soak 仍未实现，不能把已实现的 bulk 当成未知场景。
bool expectUnknownScenario(const std::string& labPath) {
    return expectLab(labPath, "unknown_scenario", {"--scenario", "soak"}, 2, {}, {}, {});
}

/// @brief 从一行 JSON 里读取一个整数键。
/// @param report 报告全文，只在本次调用期间借用。键按 "name": 匹配，冒号后允许空白。
/// @param key JSON 键名，不含引号。只在本次调用期间借用。
/// @param value 找到时写入该十进制整数。调用方拥有。找不到或数字溢出时不修改。
/// @return 键存在且后面是连续数字时为 true。
bool readJsonInt(std::string_view report, std::string_view key, int& value) {
    const std::string pattern = std::string{"\""} + std::string{key} + "\":";
    const std::size_t at = report.find(pattern);
    if (at == std::string_view::npos) {
        return false;
    }
    std::size_t index = at + pattern.size();
    while (index < report.size() && isJsonSpace(report[index])) {
        ++index;
    }
    if (index >= report.size() || report[index] < '0' || report[index] > '9') {
        return false;
    }
    int parsed = 0;
    while (index < report.size() && report[index] >= '0' && report[index] <= '9') {
        const int digit = report[index] - '0';
        if (parsed > (2147483647 - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
        ++index;
    }
    value = parsed;
    return true;
}

/// @brief 核对 smoke 场景：退出 0，报告是一次只读通过。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @return 报告含 reads 1、writes 0、数据 78563412、passed 1、failed 0 和 PASS 时为 true。
bool expectSmoke(const std::string& labPath) {
    constexpr std::string_view kFile = "comm-lab-smoke.json";
    return expectLab(labPath, "smoke", {"--scenario", "smoke", "--output", kFile}, 0, kFile,
                     {"\"reads\":1", "\"writes\":0", "\"data\":\"78563412\"", "\"passed\":1", "\"failed\":0",
                      "\"status\":\"PASS\""},
                     {});
}

/// @brief 核对 bulk 一轮写入：退出 0，写次数至少为 1，且没有失败。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @return 报告含 failed 0、PASS，并且 writes 大于等于 1 时为 true。
bool expectBulk(const std::string& labPath) {
    constexpr std::string_view kFile = "comm-lab-bulk.json";
    if (!expectLab(labPath, "bulk", {"--scenario", "bulk", "--iterations", "1", "--output", kFile}, 0, kFile,
                   {"\"failed\":0", "\"status\":\"PASS\""}, {})) {
        return false;
    }
    std::string report;
    if (!readOutputFile(kFile, report)) {
        return failCase("bulk", "cannot read report");
    }
    int writes = 0;
    if (!readJsonInt(report, "writes", writes) || writes < 1) {
        return failCase("bulk", "writes is not at least 1");
    }
    return true;
}

/// @brief 比较两份报告里的同一个故障计数字段。
/// @param left 第一次运行的报告全文，只在本次调用期间借用。
/// @param right 第二次运行的报告全文，只在本次调用期间借用。
/// @param key 计数字段名，不含引号。只在本次调用期间借用。
/// @param detail 缺失或不一致时写入原因。调用方拥有。一致时不修改。
/// @return 两个整数相等时为 true。不要求该计数大于 0。
bool sameFaultCount(std::string_view left, std::string_view right, std::string_view key, std::string& detail) {
    int leftValue = 0;
    int rightValue = 0;
    if (!readJsonInt(left, key, leftValue) || !readJsonInt(right, key, rightValue)) {
        detail = std::string{"missing fault count "} + std::string{key};
        return false;
    }
    if (leftValue != rightValue) {
        detail = std::string{key} + " " + std::to_string(leftValue) + " then " + std::to_string(rightValue);
        return false;
    }
    return true;
}

/// @brief 用同一种子把 fault-injection 跑两次，并核对六种故障计数一致。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @return 两次都退出 0，且 none、drop、delay、badFrame、badCrc、disconnect 各自相等时为 true。
bool expectFaultInjection(const std::string& labPath) {
    constexpr std::string_view kFirst = "comm-lab-fault-a.json";
    constexpr std::string_view kSecond = "comm-lab-fault-b.json";
    constexpr std::string_view kCase = "fault_injection";
    const std::initializer_list<std::string_view> kFirstArgs = {
        "--scenario", "fault-injection", "--seed", "305419896", "--iterations", "8", "--output", kFirst};
    const std::initializer_list<std::string_view> kSecondArgs = {
        "--scenario", "fault-injection", "--seed", "305419896", "--iterations", "8", "--output", kSecond};
    if (!expectLab(labPath, kCase, kFirstArgs, 0, kFirst, {}, {})) {
        return false;
    }
    if (!expectLab(labPath, kCase, kSecondArgs, 0, kSecond, {}, {})) {
        return false;
    }
    std::string firstReport;
    std::string secondReport;
    if (!readOutputFile(kFirst, firstReport) || !readOutputFile(kSecond, secondReport)) {
        return failCase(kCase, "cannot read fault reports");
    }
    constexpr std::string_view kKeys[] = {"none", "drop", "delay", "badFrame", "badCrc", "disconnect"};
    for (const std::string_view key : kKeys) {
        std::string detail;
        if (!sameFaultCount(firstReport, secondReport, key, detail)) {
            return failCase(kCase, detail);
        }
    }
    return true;
}

/// @brief 核对串口上的 bulk 仍被阻断：退出 3，报告状态是 BLOCKED。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。本用例不写真实串口。
/// @return 退出码为 3 且报告含 BLOCKED 时为 true。
bool expectSerialBulkBlocked(const std::string& labPath) {
    constexpr std::string_view kFile = "comm-lab-serial-bulk.json";
    return expectLab(labPath, "serial_bulk_blocked",
                     {"--transport", "serial", "--scenario", "bulk", "--output", kFile}, 3, kFile,
                     {"\"status\":\"BLOCKED\""}, {});
}

/// @brief 核对迭代次数 0 被拒绝，退出码为 2。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。本用例不读 JSON 文件。
/// @return 实验台退出 2 时为 true。允许范围从 1 开始。
bool expectIterationsZero(const std::string& labPath) {
    return expectLab(labPath, "iterations_zero", {"--iterations", "0"}, 2, {}, {}, {});
}

/// @brief 核对迭代次数 101 被拒绝，退出码为 2。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。本用例不读 JSON 文件。
/// @return 实验台退出 2 时为 true。允许范围到 100 为止。
bool expectIterationsTooLarge(const std::string& labPath) {
    return expectLab(labPath, "iterations_101", {"--iterations", "101"}, 2, {}, {}, {});
}

/// @brief 核对外部 Profile 不可用，退出码为 2。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。missing-profile.json 不必存在。
/// @return 实验台退出 2 时为 true。
bool expectMissingProfile(const std::string& labPath) {
    return expectLab(labPath, "missing_profile", {"--profile", "missing-profile.json"}, 2, {}, {}, {});
}

/// @brief 按用例短名运行一个实验台检查。
/// @param labPath 实验台可执行文件路径，只在本次调用期间借用。
/// @param name CTest 传入的用例短名，不含 comm_lab. 前缀。
/// @param recognized 用例名存在时设为 true。未知用例设为 false。
/// @return 该用例通过时为 true。未知用例为 false，同时 recognized 为 false。
bool runCase(const std::string& labPath, std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "help") {
        return expectHelp(labPath);
    }
    if (name == "simulator_readonly") {
        return expectSimulatorReadonly(labPath);
    }
    if (name == "allow_write_still_reads") {
        return expectAllowWriteStillReads(labPath);
    }
    if (name == "serial_blocked") {
        return expectSerialBlocked(labPath);
    }
    if (name == "unknown_scenario") {
        return expectUnknownScenario(labPath);
    }
    if (name == "missing_profile") {
        return expectMissingProfile(labPath);
    }
    if (name == "smoke") {
        return expectSmoke(labPath);
    }
    if (name == "bulk") {
        return expectBulk(labPath);
    }
    if (name == "fault_injection") {
        return expectFaultInjection(labPath);
    }
    if (name == "serial_bulk_blocked") {
        return expectSerialBulkBlocked(labPath);
    }
    if (name == "iterations_zero") {
        return expectIterationsZero(labPath);
    }
    if (name == "iterations_101") {
        return expectIterationsTooLarge(labPath);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个通信实验台进程检查。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是实验台可执行文件路径，argv[2] 是用例短名。两者只在本次进程内借用。
/// @return 期望成立时为 0。结果不符时为 1。参数缺失、路径为空或用例名未知时为 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr || argv[1][0] == '\0') {
        std::cerr << "usage: ernc_comm_lab_tests <lab-exe> <case>\n";
        return 2;
    }
    const std::string labPath{argv[1]};
    bool recognized = false;
    if (!runCase(labPath, argv[2], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[2] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
