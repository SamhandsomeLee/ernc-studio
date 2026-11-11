#include "scenarios/scenario.hpp"
#include "serial_scenarios/serial_gate.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

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

    /// 故障序列种子。缺省是 0x12345678。smoke 和 bulk 不使用它。
    std::uint32_t seed{0x12345678U};

    /// 场景重复次数。缺省是 1，只接受 1 到 100。
    int iterations{1};

    /// 调用方是否传入了允许写。模拟器场景不因它写真实设备。
    bool allowWrite{false};

    /// 参数无法识别或缺少取值。
    bool invalid{false};
};

/// @brief 打印用法到标准输出。
void printUsage() {
    std::cout << "ernc-comm-lab\n"
              << "  --help                 Show this usage and exit 0.\n"
              << "  --transport <name>     simulator (default) or serial.\n"
              << "  --scenario <name>      simulator: smoke, bulk, or fault-injection.\n"
              << "  --profile <path>       Serial profile. Simulator exits 2. Unusable serial profile exits 3.\n"
              << "  --seed <value>         Fault sequence seed. Default is 0x12345678.\n"
              << "  --iterations <count>   Repeat count from 1 to 100. Default is 1.\n"
              << "  --output <path>        Write the JSON report to this file.\n"
              << "  --allow-write          Record the flag. Serial writes are not submitted.\n";
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
            startsWithOption(argument, "--profile") || startsWithOption(argument, "--output") ||
            startsWithOption(argument, "--seed") || startsWithOption(argument, "--iterations")) {
            const auto equal = argument.find('=');
            name = argument.substr(0, equal);
            value = std::string{argument.substr(equal + 1)};
        } else if (argument == "--transport" || argument == "--scenario" || argument == "--profile" ||
                   argument == "--output" || argument == "--seed" || argument == "--iterations") {
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
        } else if (name == "--seed") {
            unsigned long parsed = 0;
            try {
                std::size_t used = 0;
                parsed = std::stoul(value, &used, 0);
                if (used != value.size() || parsed > 0xFFFFFFFFUL) {
                    options.invalid = true;
                    return options;
                }
            } catch (const std::exception&) {
                options.invalid = true;
                return options;
            }
            options.seed = static_cast<std::uint32_t>(parsed);
        } else if (name == "--iterations") {
            int parsed = 0;
            for (const char digit : value) {
                if (digit < '0' || digit > '9') {
                    options.invalid = true;
                    return options;
                }
                parsed = parsed * 10 + (digit - '0');
                if (parsed > 100) {
                    options.invalid = true;
                    return options;
                }
            }
            if (parsed < 1) {
                options.invalid = true;
                return options;
            }
            options.iterations = parsed;
        }
    }
    return options;
}

/// @brief 写成一行 JSON。
/// @param transport 报告里的传输名。
/// @param scenario 报告里的场景名。
/// @param allowWrite 调用方是否传入允许写。这不表示已经写入。
/// @param status PASS 或 BLOCKED。
/// @param reads 完成的读次数。
/// @param writes 完成的写次数。真实串口写入不计入。
/// @param data 读到的十六进制数据。没有读时为空。
/// @param stats 场景计数。只在本次调用期间借用。
/// @return 调用方拥有的一行 JSON。
std::string makeReport(const std::string& transport, const std::string& scenario, bool allowWrite,
                       const std::string& status, const std::string& data, std::uint32_t seed,
                       const ernc::lab::ScenarioStats& stats) {
    return std::string{"{\"transport\":\""} + transport + "\",\"scenario\":\"" + scenario +
           "\",\"allowWrite\":" + (allowWrite ? "true" : "false") + ",\"status\":\"" + status +
           "\",\"reads\":" + std::to_string(stats.reads) + ",\"writes\":" + std::to_string(stats.writes) +
           ",\"data\":\"" + data + "\",\"seed\":" + std::to_string(seed) + ",\"passed\":" +
           std::to_string(stats.passed) + ",\"failed\":" + std::to_string(stats.failed) +
           ",\"none\":" + std::to_string(stats.none) + ",\"drop\":" + std::to_string(stats.drop) +
           ",\"delay\":" + std::to_string(stats.delay) + ",\"badFrame\":" + std::to_string(stats.badFrame) +
           ",\"badCrc\":" + std::to_string(stats.badCrc) + ",\"disconnect\":" + std::to_string(stats.disconnect) +
           "}\n";
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
    if (options.invalid) {
        std::cerr << "invalid arguments\n";
        return 2;
    }
    const bool knownScenario = options.scenario == "smoke" || options.scenario == "bulk" ||
                               options.scenario == "fault-injection";
    if (options.transport == "serial") {
        const ernc::lab::SerialGate gate = ernc::lab::runSerialGate(options.profile);
        if (gate == ernc::lab::SerialGate::NoByteExchange) {
            std::cerr << "serial port was closed without a byte exchange\n";
            return 2;
        }
        ernc::lab::ScenarioStats empty;
        const std::string report =
            makeReport(options.transport, options.scenario, options.allowWrite, "BLOCKED", "", options.seed, empty);
        if (!writeReport(options.output, report)) {
            std::cerr << "cannot write report\n";
            return 2;
        }
        return 3;
    }
    if (!options.profile.empty()) {
        std::cerr << "external profile is not available\n";
        return 2;
    }
    if (options.transport != "simulator" || !knownScenario) {
        std::cerr << "invalid arguments\n";
        return 2;
    }
    const ernc::lab::ScenarioStats stats = ernc::lab::runScenario(options.scenario, options.seed, options.iterations);
    if (!stats.ok) {
        const std::string report =
            makeReport(options.transport, options.scenario, options.allowWrite, "FAIL", stats.data, options.seed, stats);
        if (!writeReport(options.output, report)) {
            std::cerr << "cannot write report\n";
            return 2;
        }
        return 1;
    }
    const std::string report =
        makeReport(options.transport, options.scenario, options.allowWrite, "PASS", stats.data, options.seed, stats);
    if (!writeReport(options.output, report)) {
        std::cerr << "cannot write report\n";
        return 2;
    }
    return 0;
}
