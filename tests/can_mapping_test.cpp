#include "can_mapping.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

using ernc::application::CanFreshness;
using ernc::application::CanInterlockInput;
using ernc::application::CanInterlockVerdict;
using ernc::application::CanMappingStatus;
using ernc::application::CanMappingTable;
using ernc::application::CanSignalMapping;
using ernc::application::VehicleSpeedSample;
using ernc::application::canMsgSlotCount;
using ernc::application::canUnitConfirmed;
using ernc::application::evaluateVehicleSpeedInterlock;
using ernc::application::expectedCanNameCount;
using ernc::application::findVehicleSpeedMapping;
using ernc::application::loadCanMappingTableFromFile;
using ernc::application::loadCanMappingTableFromRawJson;
using ernc::application::summarizeCanMapping;
using ernc::application::vehicleSpeedNameZh;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

RuntimeTime at(int seconds) {
    return RuntimeTime{std::chrono::seconds{seconds}};
}

bool catalog_loads_36_names(const char* canPath) {
    constexpr const char* name = "catalog_loads_36_names";
    const Result<CanMappingTable> loaded = loadCanMappingTableFromFile(canPath);
    if (!loaded.ok()) {
        return fail(name);
    }
    const CanMappingTable& table = loaded.value();
    if (table.signals.size() != expectedCanNameCount() || table.mappingTableConfirmed) {
        return fail(name);
    }
    const CanSignalMapping* speed = findVehicleSpeedMapping(table);
    if (speed == nullptr || speed->excelIndex != 1U || speed->nameZh != vehicleSpeedNameZh()
        || speed->mappingStatus != CanMappingStatus::Unmapped || speed->firmwareSlotIndex.has_value()) {
        return fail(name);
    }
    if (canMsgSlotCount() != 72U) {
        return fail(name);
    }
    const auto summary = summarizeCanMapping(table);
    if (summary.mappedCount != 0U || summary.unmappedCount != 36U || summary.vehicleSpeedMapped) {
        return fail(name);
    }
    return true;
}

bool speed_blocked_when_mapping_unconfirmed(const char* canPath) {
    constexpr const char* name = "speed_blocked_when_mapping_unconfirmed";
    const Result<CanMappingTable> loaded = loadCanMappingTableFromFile(canPath);
    if (!loaded.ok()) {
        return false;
    }
    CanMappingTable table = loaded.value();
    table.mappingTableConfirmed = false;
    VehicleSpeedSample sample;
    sample.observation.rawValue = 10.0F;
    sample.observation.observedAt = at(100);
    sample.observation.freshness = CanFreshness::Fresh;
    sample.speedKmh = 10.0F;
    CanInterlockInput gates;
    gates.deviceConnected = true;
    gates.mappingTableConfirmed = false;
    const CanInterlockVerdict verdict =
        evaluateVehicleSpeedInterlock(table, gates, sample, at(100), std::chrono::seconds{2});
    return verdict == CanInterlockVerdict::BlockedMappingUnresolved;
}

bool speed_blocked_when_entry_unmapped() {
    CanMappingTable table;
    table.mappingTableConfirmed = true;
    CanSignalMapping speed;
    speed.excelIndex = 1U;
    speed.nameZh = vehicleSpeedNameZh();
    speed.mappingStatus = CanMappingStatus::Unmapped;
    table.signals.push_back(speed);
    VehicleSpeedSample sample;
    sample.observation.freshness = CanFreshness::Fresh;
    sample.speedKmh = 6.0F;
    CanInterlockInput gates;
    gates.deviceConnected = true;
    gates.mappingTableConfirmed = true;
    const CanInterlockVerdict verdict =
        evaluateVehicleSpeedInterlock(table, gates, sample, at(1), std::chrono::seconds{2});
    return verdict == CanInterlockVerdict::BlockedMappingUnresolved;
}

CanMappingTable mappedSpeedTable() {
    CanMappingTable table;
    table.mappingTableConfirmed = true;
    CanSignalMapping speed;
    speed.excelIndex = 1U;
    speed.nameZh = vehicleSpeedNameZh();
    speed.mappingStatus = CanMappingStatus::Mapped;
    speed.firmwareSlotIndex = 10U;
    speed.unit = "km/h";
    table.signals.push_back(speed);
    return table;
}

bool stale_observation_blocks() {
    VehicleSpeedSample sample;
    sample.observation.observedAt = at(0);
    sample.observation.freshness = CanFreshness::Unknown;
    sample.speedKmh = 6.0F;
    CanInterlockInput gates;
    gates.deviceConnected = true;
    gates.mappingTableConfirmed = true;
    const CanInterlockVerdict verdict = evaluateVehicleSpeedInterlock(
        mappedSpeedTable(), gates, sample, at(10), std::chrono::seconds{2});
    return verdict == CanInterlockVerdict::BlockedStale;
}

bool unit_unconfirmed_blocks_threshold() {
    CanMappingTable table = mappedSpeedTable();
    table.signals.front().unit.clear();
    VehicleSpeedSample sample;
    sample.observation.freshness = CanFreshness::Fresh;
    sample.speedKmh = 6.0F;
    CanInterlockInput gates;
    gates.deviceConnected = true;
    gates.mappingTableConfirmed = true;
    if (canUnitConfirmed(table.signals.front())) {
        return fail("unit_unconfirmed_blocks_threshold");
    }
    const CanInterlockVerdict verdict =
        evaluateVehicleSpeedInterlock(table, gates, sample, at(1), std::chrono::seconds{2});
    return verdict == CanInterlockVerdict::BlockedUnitUnconfirmed;
}

bool allowed_when_fully_confirmed() {
    VehicleSpeedSample sample;
    sample.observation.observedAt = at(100);
    sample.observation.freshness = CanFreshness::Fresh;
    sample.speedKmh = 6.0F;
    CanInterlockInput gates;
    gates.deviceConnected = true;
    gates.mappingTableConfirmed = true;
    const CanInterlockVerdict verdict = evaluateVehicleSpeedInterlock(
        mappedSpeedTable(), gates, sample, at(101), std::chrono::seconds{2});
    return verdict == CanInterlockVerdict::AllowedForAutoRelease;
}

bool raw_value_alone_does_not_bypass_mapping() {
    const Result<CanMappingTable> loaded = loadCanMappingTableFromRawJson(R"({
      "rows": [
        {"cells": {"J": "1", "K": "车速"}}
      ]
    })");
    return !loaded.ok() || loaded.tryError() == nullptr || *loaded.tryError() != ErrorCode::InvalidParameter;
}

bool runCase(std::string_view name, const char* canPath, bool& recognized) {
    recognized = true;
    if (name == "catalog_loads_36_names") {
        return catalog_loads_36_names(canPath);
    }
    if (name == "speed_blocked_when_mapping_unconfirmed") {
        return speed_blocked_when_mapping_unconfirmed(canPath);
    }
    if (name == "speed_blocked_when_entry_unmapped") {
        return speed_blocked_when_entry_unmapped();
    }
    if (name == "stale_observation_blocks") {
        return stale_observation_blocks();
    }
    if (name == "unit_unconfirmed_blocks_threshold") {
        return unit_unconfirmed_blocks_threshold();
    }
    if (name == "allowed_when_fully_confirmed") {
        return allowed_when_fully_confirmed();
    }
    if (name == "raw_value_alone_does_not_bypass_mapping") {
        return raw_value_alone_does_not_bypass_mapping();
    }
    recognized = false;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_can_mapping_tests <case> <can_names.raw.json>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], argv[2], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
