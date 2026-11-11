#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using ernc::application::CommandId;
using ernc::application::CommandOp;
using ernc::application::CommandRequest;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ReadCommandResult;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// 内置一槽镜像。数据是字面量，不用编码器生成。
constexpr std::uint8_t kPlantedSlot[] = {0x78, 0x56, 0x34, 0x12};

/// @brief 命令行上已经认出的选项。
struct Options {
    /// 为 true 时只打印用法。
    bool help{false};

    /// simulator 或 serial。缺省是 simulator。
    std::string transport{"simulator"};

    /// 场景名。缺省是 smoke。本程序不执行 smoke 套件。
    std::string scenario{"smoke"};

    /// 外部 Profile 路径。空表示使用内置从站配置。
    std::string profile;

    /// JSON 报告路径。空表示写到标准输出。
    std::string output;

    /// 调用方是否传入了允许写。本程序仍然不执行写。
    bool allowWrite{false};

    /// 参数无法识别或缺少取值。
    bool invalid{false};
};

/// @brief 打印用法到标准输出。
void printUsage() {
    std::cout << "ernc-comm-lab\n"
              << "  --help                 Show this usage and exit 0.\n"
              << "  --transport <name>     simulator (default) or serial.\n"
              << "  --scenario <name>      smoke is accepted; other names exit 2.\n"
              << "  --profile <path>       External profile is not available and exits 2.\n"
              << "  --output <path>        Write the JSON report to this file.\n"
              << "  --allow-write          Record the flag. This build still writes 0 times.\n";
}

/// @brief 判断参数是否等于带等号的长选项前缀。
/// @param argument 当前参数，只在本次调用期间借用。
/// @param name 不带等号的选项名，例如 --transport。只在本次调用期间借用。
/// @return 参数以 name= 开头时为 true。
bool startsWithOption(std::string_view argument, std::string_view name) {
    return argument.size() > name.size() + 1 && argument.substr(0, name.size()) == name &&
           argument[name.size()] == '=';
}

/// @brief 从 argv 取出选项。
/// @param argc 参数个数，包含程序名。
/// @param argv 参数数组，只在本次调用期间借用。
/// @return 解析结果。无法识别的参数把 invalid 设为 true。
Options parseArguments(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index] == nullptr ? std::string_view{} : std::string_view{argv[index]};
        if (argument == "--help") {
            options.help = true;
            continue;
        }
        if (argument == "--allow-write") {
            options.allowWrite = true;
            continue;
        }
        std::string_view name;
        std::string value;
        if (startsWithOption(argument, "--transport") || startsWithOption(argument, "--scenario") ||
            startsWithOption(argument, "--profile") || startsWithOption(argument, "--output")) {
            const auto equal = argument.find('=');
            name = argument.substr(0, equal);
            value = std::string{argument.substr(equal + 1)};
        } else if (argument == "--transport" || argument == "--scenario" || argument == "--profile" ||
                   argument == "--output") {
            if (index + 1 >= argc || argv[index + 1] == nullptr) {
                options.invalid = true;
                return options;
            }
            name = argument;
            value = argv[++index];
        } else {
            options.invalid = true;
            return options;
        }
        if (value.empty()) {
            options.invalid = true;
            return options;
        }
        if (name == "--transport") {
            options.transport = value;
        } else if (name == "--scenario") {
            options.scenario = value;
        } else if (name == "--profile") {
            options.profile = value;
        } else if (name == "--output") {
            options.output = value;
        }
    }
    return options;
}

/// @brief 把字节写成连续的小写十六进制。
/// @param bytes 要输出的字节，只在本次调用期间借用。
/// @return 调用方拥有的十六进制文本。空输入得到空字符串。
std::string toHex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

/// @brief 写成一行 JSON。
/// @param transport 报告里的传输名。
/// @param scenario 报告里的场景名。
/// @param allowWrite 调用方是否传入允许写。这不表示已经写入。
/// @param status PASS 或 BLOCKED。
/// @param reads 完成的读次数。
/// @param writes 完成的写次数。本程序固定写 0。
/// @param data 读到的十六进制数据。没有读时为空。
/// @return 调用方拥有的一行 JSON。
std::string makeReport(const std::string& transport, const std::string& scenario, bool allowWrite,
                       const std::string& status, int reads, int writes, const std::string& data) {
    return std::string{"{\"transport\":\""} + transport + "\",\"scenario\":\"" + scenario +
           "\",\"allowWrite\":" + (allowWrite ? "true" : "false") + ",\"status\":\"" + status +
           "\",\"reads\":" + std::to_string(reads) + ",\"writes\":" + std::to_string(writes) +
           ",\"data\":\"" + data + "\"}\n";
}

/// @brief 把报告写到文件或标准输出。
/// @param output 文件路径。空字符串表示标准输出。
/// @param report 一行 JSON，只在本次调用期间借用。
/// @return 文件打不开时为 false。标准输出始终为 true。
bool writeReport(const std::string& output, const std::string& report) {
    if (output.empty()) {
        std::cout << report;
        return true;
    }
    const std::filesystem::path path{output};
    if (path.has_parent_path() && !path.parent_path().empty() && !std::filesystem::exists(path.parent_path())) {
        return false;
    }
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }
    file << report;
    return static_cast<bool>(file);
}

/// @brief 对内置镜像做一次只读 FC03。
/// @param data 成功时写入读到的字节。调用方拥有这份副本。
/// @return 读到的 4 字节与内置字面量相同时为 true。
bool readPlantedSlot(std::vector<std::uint8_t>& data) {
    SlaveImage image{std::vector<std::uint8_t>(kPlantedSlot, kPlantedSlot + 4)};
    MemorySlaveTransport port{image};
    DeviceSession session{port};
    const auto connected = session.connect();
    if (!connected.ok() || connected.tryError() != nullptr) {
        return false;
    }
    CommandScheduler scheduler{session, port};
    CommandRequest request;
    request.op = CommandOp::Read;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.slaveAddress = 0x01;
    request.readBack = false;
    const auto enqueued = scheduler.enqueue(request);
    const CommandId* id = enqueued.tryValue();
    if (!enqueued.ok() || enqueued.tryError() != nullptr || id == nullptr) {
        return false;
    }
    scheduler.pump();
    const auto outcome = scheduler.takeResult(*id);
    if (!outcome.has_value()) {
        return false;
    }
    const ReadCommandResult* read = std::get_if<ReadCommandResult>(&outcome->body);
    if (read == nullptr || read->payload.size() != 4) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (read->payload[index] != kPlantedSlot[index]) {
            return false;
        }
    }
    data = read->payload;
    return true;
}

}  // namespace

/// @brief 运行通信实验骨架。
/// @param argc 参数个数，包含程序名。
/// @param argv 参数数组，只在本次进程内借用。
/// @return 0 通过，1 数据不符，2 参数或配置错误，3 为 BLOCKED。
int main(int argc, char** argv) {
    const Options options = parseArguments(argc, argv);
    if (options.help) {
        printUsage();
        return 0;
    }
    if (options.invalid || options.scenario != "smoke") {
        std::cerr << "invalid arguments\n";
        return 2;
    }
    if (!options.profile.empty()) {
        std::cerr << "external profile is not available\n";
        return 2;
    }
    if (options.transport == "serial") {
        const std::string report =
            makeReport(options.transport, options.scenario, options.allowWrite, "BLOCKED", 0, 0, "");
        if (!writeReport(options.output, report)) {
            std::cerr << "cannot write report\n";
            return 2;
        }
        return 3;
    }
    if (options.transport != "simulator") {
        std::cerr << "invalid arguments\n";
        return 2;
    }
    std::vector<std::uint8_t> data;
    if (!readPlantedSlot(data) || data.size() != 4) {
        return 1;
    }
    const std::string report =
        makeReport(options.transport, options.scenario, options.allowWrite, "PASS", 1, 0, toHex(data));
    if (!writeReport(options.output, report)) {
        std::cerr << "cannot write report\n";
        return 2;
    }
    return 0;
}
