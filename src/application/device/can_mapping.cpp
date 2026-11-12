#include "can_mapping.hpp"

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace ernc::application {

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayout;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;

const CanSignalMapping* findByName(const CanMappingTable& table, std::string_view name) noexcept {
    for (const CanSignalMapping& mapping : table.signals) {
        if (mapping.nameZh == name) {
            return &mapping;
        }
    }
    return nullptr;
}

bool parseExcelIndex(std::string_view text, std::size_t& position, std::uint32_t& indexOut) {
    constexpr std::string_view key = "\"J\": \"";
    const std::size_t found = text.find(key, position);
    if (found == std::string_view::npos) {
        return false;
    }
    std::size_t at = found + key.size();
    std::uint32_t value = 0;
    bool any = false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        any = true;
        value = value * 10U + static_cast<std::uint32_t>(text[at] - '0');
        ++at;
    }
    if (!any) {
        return false;
    }
    position = at;
    indexOut = value;
    return true;
}

bool parseNameZh(std::string_view text, std::size_t& position, std::string& nameOut) {
    constexpr std::string_view key = "\"K\": \"";
    const std::size_t found = text.find(key, position);
    if (found == std::string_view::npos) {
        return false;
    }
    const std::size_t start = found + key.size();
    const std::size_t end = text.find('"', start);
    if (end == std::string_view::npos || end <= start) {
        return false;
    }
    nameOut.assign(text.substr(start, end - start));
    position = end + 1;
    return !nameOut.empty();
}

bool parseJsonUint32AfterKey(std::string_view text, std::size_t& position, std::string_view key, std::uint32_t& out) {
    const std::size_t found = text.find(key, position);
    if (found == std::string_view::npos) {
        return false;
    }
    std::size_t at = found + key.size();
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
        ++at;
    }
    if (at >= text.size() || text[at] != ':') {
        return false;
    }
    ++at;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
        ++at;
    }
    std::uint32_t value = 0;
    bool any = false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        any = true;
        value = value * 10U + static_cast<std::uint32_t>(text[at] - '0');
        ++at;
    }
    if (!any) {
        return false;
    }
    position = at;
    out = value;
    return true;
}

bool parseMappingTableConfirmed(std::string_view text, bool& confirmedOut) {
    constexpr std::string_view key = "\"mappingTableConfirmed\"";
    const std::size_t found = text.find(key);
    if (found == std::string_view::npos) {
        return false;
    }
    const std::size_t trueToken = text.find("true", found + key.size());
    const std::size_t falseToken = text.find("false", found + key.size());
    if (trueToken == std::string_view::npos && falseToken == std::string_view::npos) {
        return false;
    }
    if (trueToken != std::string_view::npos && (falseToken == std::string_view::npos || trueToken < falseToken)) {
        confirmedOut = true;
        return true;
    }
    confirmedOut = false;
    return true;
}

bool parseSchemaVersion(std::string_view text, std::uint32_t& versionOut) {
    std::size_t position = 0;
    return parseJsonUint32AfterKey(text, position, "\"schema_version\"", versionOut);
}

CanSignalMapping* findByExcelIndex(CanMappingTable& table, std::uint32_t excelIndex) noexcept {
    for (CanSignalMapping& mapping : table.signals) {
        if (mapping.excelIndex == excelIndex) {
            return &mapping;
        }
    }
    return nullptr;
}

}  // namespace

Result<CanMappingTable> loadCanMappingTableFromRawJson(std::string_view jsonText) {
    CanMappingTable table;
    std::size_t position = 0;
    while (position < jsonText.size()) {
        std::uint32_t excelIndex = 0;
        std::string nameZh;
        if (!parseExcelIndex(jsonText, position, excelIndex)) {
            break;
        }
        if (!parseNameZh(jsonText, position, nameZh)) {
            return Result<CanMappingTable>::failure(ErrorCode::InvalidParameter);
        }
        CanSignalMapping mapping;
        mapping.excelIndex = excelIndex;
        mapping.nameZh = std::move(nameZh);
        mapping.mappingStatus = CanMappingStatus::Unmapped;
        mapping.firmwareSlotIndex = std::nullopt;
        table.signals.push_back(std::move(mapping));
    }
    if (table.signals.size() != expectedCanNameCount()) {
        return Result<CanMappingTable>::failure(ErrorCode::InvalidParameter);
    }
    return Result<CanMappingTable>::success(std::move(table));
}

Result<void> loadCanMsgSlotMapIntoTable(CanMappingTable& table, const char* path) {
    if (path == nullptr || table.signals.size() != expectedCanNameCount()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string_view jsonText = buffer.view();

    std::uint32_t schemaVersion = 0;
    if (!parseSchemaVersion(jsonText, schemaVersion) || schemaVersion != 1U) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    bool mappingConfirmed = false;
    if (!parseMappingTableConfirmed(jsonText, mappingConfirmed)) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }

    std::size_t position = 0;
    std::unordered_set<std::uint32_t> seenExcel;
    std::unordered_set<std::uint32_t> seenArray;
    std::size_t mappedCount = 0;
    while (position < jsonText.size()) {
        std::uint32_t excelIndex = 0;
        if (!parseJsonUint32AfterKey(jsonText, position, "\"excelIndex\"", excelIndex)) {
            break;
        }
        std::uint32_t arrayIndex = 0;
        if (!parseJsonUint32AfterKey(jsonText, position, "\"arrayIndex\"", arrayIndex)) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        if (excelIndex < 1U || excelIndex > expectedCanNameCount() || arrayIndex >= expectedCanNameCount()) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        if (!seenExcel.insert(excelIndex).second || !seenArray.insert(arrayIndex).second) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        CanSignalMapping* mapping = findByExcelIndex(table, excelIndex);
        if (mapping == nullptr) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        mapping->arrayIndex = arrayIndex;
        ++mappedCount;
    }
    if (mappedCount != expectedCanNameCount()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    table.mappingTableConfirmed = mappingConfirmed;
    return Result<void>::success();
}

Result<void> applyConfirmedSlotMap(CanMappingTable& table, const FirmwareLayout& layout) {
    if (!table.mappingTableConfirmed) {
        return Result<void>::success();
    }
    const ernc::domain::LayoutResolve located = layout.locate("Can_Msg");
    if (!located.ok()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::uint32_t baseSlot = located.field().slot;
    for (CanSignalMapping& mapping : table.signals) {
        if (!mapping.arrayIndex.has_value()) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        mapping.firmwareSlotIndex = baseSlot + *mapping.arrayIndex;
        mapping.mappingStatus = CanMappingStatus::Mapped;
    }
    return Result<void>::success();
}

Result<CanMappingTable> loadCanMappingTableFromFile(const char* path) {
    if (path == nullptr) {
        return Result<CanMappingTable>::failure(ErrorCode::InvalidParameter);
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Result<CanMappingTable>::failure(ErrorCode::InvalidParameter);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return loadCanMappingTableFromRawJson(buffer.view());
}

const CanSignalMapping* findVehicleSpeedMapping(const CanMappingTable& table) noexcept {
    return findByName(table, vehicleSpeedNameZh());
}

CanMappingSummary summarizeCanMapping(const CanMappingTable& table) noexcept {
    CanMappingSummary summary;
    summary.totalSignals = table.signals.size();
    for (const CanSignalMapping& mapping : table.signals) {
        if (mapping.mappingStatus == CanMappingStatus::Mapped) {
            ++summary.mappedCount;
        } else {
            ++summary.unmappedCount;
        }
    }
    const CanSignalMapping* speed = findVehicleSpeedMapping(table);
    summary.vehicleSpeedMapped =
        speed != nullptr && speed->mappingStatus == CanMappingStatus::Mapped && speed->firmwareSlotIndex.has_value();
    return summary;
}

CanFreshness evaluateCanFreshness(std::optional<RuntimeTime> observedAt, RuntimeTime now,
                                   std::chrono::milliseconds maxAge) noexcept {
    if (!observedAt.has_value()) {
        return CanFreshness::Unknown;
    }
    if (now < *observedAt) {
        return CanFreshness::Stale;
    }
    const auto age = now - *observedAt;
    if (age > maxAge) {
        return CanFreshness::Stale;
    }
    return CanFreshness::Fresh;
}

bool canUnitConfirmed(const CanSignalMapping& mapping) noexcept {
    return !mapping.unit.empty();
}

CanInterlockVerdict evaluateVehicleSpeedInterlock(const CanMappingTable& table, const CanInterlockInput& gates,
                                                   const std::optional<VehicleSpeedSample>& sample, RuntimeTime now,
                                                   std::chrono::milliseconds maxFreshnessAge) {
    if (!gates.deviceConnected) {
        return CanInterlockVerdict::BlockedDeviceDisconnected;
    }
    if (!gates.mappingTableConfirmed) {
        return CanInterlockVerdict::BlockedMappingUnresolved;
    }
    const CanSignalMapping* speedMapping = findVehicleSpeedMapping(table);
    if (speedMapping == nullptr || speedMapping->mappingStatus != CanMappingStatus::Mapped
        || !speedMapping->firmwareSlotIndex.has_value()) {
        return CanInterlockVerdict::BlockedMappingUnresolved;
    }
    if (!canUnitConfirmed(*speedMapping)) {
        return CanInterlockVerdict::BlockedUnitUnconfirmed;
    }
    if (!sample.has_value()) {
        return CanInterlockVerdict::BlockedInvalidValue;
    }
    const CanFreshness freshness =
        sample->observation.freshness != CanFreshness::Unknown
            ? sample->observation.freshness
            : evaluateCanFreshness(sample->observation.observedAt, now, maxFreshnessAge);
    if (freshness != CanFreshness::Fresh) {
        return CanInterlockVerdict::BlockedStale;
    }
    if (!sample->speedKmh.has_value()) {
        return CanInterlockVerdict::BlockedUnitUnconfirmed;
    }
    if (*sample->speedKmh < 0.0F) {
        return CanInterlockVerdict::BlockedInvalidValue;
    }
    return CanInterlockVerdict::AllowedForAutoRelease;
}

}  // namespace ernc::application
