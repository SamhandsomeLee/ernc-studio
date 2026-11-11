#pragma once

#include <cstdint>
#include <string>

namespace ernc::lab {

/// @brief 一次场景跑完后的计数。
///
/// 全部由场景函数填写。故障计数只在 fault-injection 里增加。
struct ScenarioStats {
    /// 成功读到数据区的次数。失败的读不计入。
    int reads{0};

    /// 从站接受了写入的次数。真实串口写入不在这里。
    int writes{0};

    /// 与字面量期望一致，或正常故障响应被正确分类的次数。
    int passed{0};

    /// 数据不符，或故障结果不能当成成功的次数。
    int failed{0};

    /// 最近一次成功读到的十六进制数据。没有成功读时为空。
    std::string data;

    /// FaultKind::None 的次数。
    int none{0};

    /// 丢包次数。
    int drop{0};

    /// 延迟次数。
    int delay{0};

    /// 坏帧次数。
    int badFrame{0};

    /// 坏 CRC 次数。
    int badCrc{0};

    /// 断连次数。
    int disconnect{0};

    /// 场景要求的检查都满足时为 true。故障注入跑完即为 true，即使 failed 大于 0。
    bool ok{false};
};

/// @brief 运行已注册的模拟器场景。
///
/// 只接受 smoke、bulk、fault-injection。不打开串口，也不睡眠。
/// @param scenario 场景名，只在本次调用期间借用。
/// @param seed 故障序列种子。smoke 和 bulk 不使用它，但仍原样返回到报告。
/// @param iterations 重复次数。调用方已经限制在 1 到 100。bulk 每一轮都从同一份初始镜像开始。
/// @return 调用方拥有的计数。场景名未注册时 ok 为 false，其余计数为 0。
ScenarioStats runScenario(const std::string& scenario, std::uint32_t seed, int iterations);

}  // namespace ernc::lab
