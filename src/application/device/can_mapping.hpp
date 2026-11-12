#pragma once

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/types.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::application {

/// Excel 名称在 manifest 中的映射状态。
enum class CanMappingStatus {
    /// 没有 slot 或布局证据。
    Unmapped,

    /// 负责人确认的显式映射。
    Mapped,
};

/// 观测新鲜度。
enum class CanFreshness {
    Unknown,
    Fresh,
    Stale,
};

/// 安全联锁判定。车速未满足条件时不得自动放行。
enum class CanInterlockVerdict {
    AllowedForAutoRelease,
    BlockedMappingUnresolved,
    BlockedUnitUnconfirmed,
    BlockedStale,
    BlockedInvalidValue,
    BlockedDeviceDisconnected,
};

/// 一条 CAN 信号目录行。
struct CanSignalMapping {
    /// Excel 序号列 J，从 1 开始。
    std::uint32_t excelIndex{0};

    /// 中文名称列 K。
    std::string nameZh;

    CanMappingStatus mappingStatus{CanMappingStatus::Unmapped};

    /// 对应 Can_Msg 槽位；未映射时为空。加载器不得用 excelIndex 猜测。
    std::optional<std::uint32_t> firmwareSlotIndex;

    /// Can_Msg[72] 中本名称的下标；slot map 未合并时为空。
    std::optional<std::uint32_t> arrayIndex;

    /// 物理单位。空表示未确认。
    std::string unit;

    /// 缩放系数。未确认时为空。
    std::optional<float> scale;
};

/// 36 项名称表；不推断 72 float 布局。
struct CanMappingTable {
    /// 负责人是否确认 36 名称与 72 slot 映射表。未确认前恒为 false。
    bool mappingTableConfirmed{false};

    std::vector<CanSignalMapping> signals;
};

struct CanSignalObservation {
    float rawValue{0.0F};
    std::optional<ernc::domain::RuntimeTime> observedAt;
    CanFreshness freshness{CanFreshness::Unknown};
};

struct VehicleSpeedSample {
    CanSignalObservation observation;
    /// 仅单位与映射确认后由上层填入；本模块不换算。
    std::optional<float> speedKmh;
};

struct CanInterlockInput {
    bool deviceConnected{false};
    bool mappingTableConfirmed{false};
};

struct CanMappingSummary {
    std::size_t totalSignals{0};
    std::size_t mappedCount{0};
    std::size_t unmappedCount{0};
    bool vehicleSpeedMapped{false};
};

constexpr std::size_t expectedCanNameCount() noexcept {
    return 36U;
}

constexpr std::size_t canMsgSlotCount() noexcept {
    return 72U;
}

constexpr const char* vehicleSpeedNameZh() noexcept {
    return "车速";
}

/// @brief 从 Excel 原始 JSON 加载 36 个名称。全部 mapping 为 Unmapped，无 slot。
ernc::domain::Result<CanMappingTable> loadCanMappingTableFromRawJson(std::string_view jsonText);

/// @brief 从文件路径加载。
ernc::domain::Result<CanMappingTable> loadCanMappingTableFromFile(const char* path);

/// @brief 将 catalog/can_msg_slot_map.json 合并进已有 36 项名称表。
/// @param table 须先由 loadCanMappingTableFromFile 填满名称；就地写入 arrayIndex 与 mappingTableConfirmed。
ernc::domain::Result<void> loadCanMsgSlotMapIntoTable(CanMappingTable& table, const char* path);

/// @brief mappingTableConfirmed 为 true 时，用固件布局解析 Can_Msg 基址并写入 firmwareSlotIndex。
ernc::domain::Result<void> applyConfirmedSlotMap(CanMappingTable& table, const ernc::domain::FirmwareLayout& layout);

/// @param table 映射表，只借用。
/// @return 首条「车速」项指针；未找到时为空。
const CanSignalMapping* findVehicleSpeedMapping(const CanMappingTable& table) noexcept;

[[nodiscard]] CanMappingSummary summarizeCanMapping(const CanMappingTable& table) noexcept;

[[nodiscard]] CanFreshness evaluateCanFreshness(std::optional<ernc::domain::RuntimeTime> observedAt,
                                                 ernc::domain::RuntimeTime now,
                                                 std::chrono::milliseconds maxAge) noexcept;

[[nodiscard]] bool canUnitConfirmed(const CanSignalMapping& mapping) noexcept;

[[nodiscard]] CanInterlockVerdict evaluateVehicleSpeedInterlock(const CanMappingTable& table,
                                                                 const CanInterlockInput& gates,
                                                                 const std::optional<VehicleSpeedSample>& sample,
                                                                 ernc::domain::RuntimeTime now,
                                                                 std::chrono::milliseconds maxFreshnessAge);

}  // namespace ernc::application
