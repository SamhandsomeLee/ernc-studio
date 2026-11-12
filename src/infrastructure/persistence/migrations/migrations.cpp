#include "migrations.hpp"

#include <iterator>

namespace ernc::infrastructure {
namespace {

/// v1：project / device_profile / parameter_snapshot / parameter_value /
/// experiment / data_asset / operation_log 七张基础表。
///
/// 时间列统一存 TEXT ISO-8601 UTC，由应用写入；这里不写默认值，避免把本机时区混进数据库。
/// 外键策略：删项目级联删快照/实验；删快照级联删参数值；
/// 被实验引用的快照、被资产引用的实验使用 RESTRICT，必须先显式清理。
constexpr std::string_view kVersion1Statements[] = {
    R"(CREATE TABLE project (
        id              INTEGER PRIMARY KEY,
        name            TEXT NOT NULL,
        created_at      TEXT NOT NULL,
        updated_at      TEXT NOT NULL,
        schema_version  INTEGER NOT NULL,
        catalog_version TEXT NOT NULL
    ))",
    R"(CREATE TABLE device_profile (
        id               INTEGER PRIMARY KEY,
        device_name      TEXT NOT NULL,
        protocol_id      TEXT NOT NULL,
        slave_address    INTEGER NOT NULL,
        baud_rate        INTEGER NOT NULL,
        data_bits        INTEGER NOT NULL,
        parity           TEXT NOT NULL,
        stop_bits        INTEGER NOT NULL,
        firmware_version TEXT NOT NULL
    ))",
    R"(CREATE TABLE parameter_snapshot (
        id               INTEGER PRIMARY KEY,
        project_id       INTEGER NOT NULL REFERENCES project(id) ON DELETE CASCADE,
        created_at       TEXT NOT NULL,
        source           TEXT NOT NULL,
        firmware_version TEXT NOT NULL,
        catalog_version  TEXT NOT NULL,
        note             TEXT
    ))",
    R"(CREATE TABLE parameter_value (
        snapshot_id  INTEGER NOT NULL REFERENCES parameter_snapshot(id) ON DELETE CASCADE,
        parameter_id TEXT NOT NULL,
        data_type    TEXT NOT NULL,
        value_json   TEXT,
        raw_payload  BLOB,
        validity     TEXT NOT NULL,
        PRIMARY KEY (snapshot_id, parameter_id)
    ))",
    R"(CREATE TABLE experiment (
        id                    INTEGER PRIMARY KEY,
        project_id            INTEGER NOT NULL REFERENCES project(id) ON DELETE CASCADE,
        name                  TEXT NOT NULL,
        started_at            TEXT NOT NULL,
        ended_at              TEXT,
        test_condition        TEXT,
        parameter_snapshot_id INTEGER REFERENCES parameter_snapshot(id) ON DELETE RESTRICT,
        note                  TEXT
    ))",
    R"(CREATE TABLE data_asset (
        id              INTEGER PRIMARY KEY,
        experiment_id   INTEGER REFERENCES experiment(id) ON DELETE RESTRICT,
        asset_kind      TEXT NOT NULL,
        relative_path   TEXT NOT NULL,
        format          TEXT NOT NULL,
        byte_size       INTEGER NOT NULL,
        sha256          TEXT NOT NULL,
        dimensions_json TEXT,
        metadata_json   TEXT
    ))",
    R"(CREATE TABLE operation_log (
        id           INTEGER PRIMARY KEY,
        timestamp    TEXT NOT NULL,
        session_id   TEXT,
        command_id   TEXT,
        operation    TEXT NOT NULL,
        parameter_id TEXT,
        slot_address INTEGER,
        result       TEXT NOT NULL,
        duration_ms  INTEGER,
        details_json TEXT
    ))",
    R"(CREATE INDEX idx_parameter_snapshot_project ON parameter_snapshot(project_id))",
    R"(CREATE INDEX idx_experiment_project ON experiment(project_id))",
    R"(CREATE INDEX idx_data_asset_experiment ON data_asset(experiment_id))",
    R"(CREATE INDEX idx_operation_log_timestamp ON operation_log(timestamp))",
};

/// 内置迁移列表。版本必须与 kLatestSchemaVersion 对齐。
constexpr Migration kBuiltinMigrations[] = {
    {1, "initial project schema", kVersion1Statements},
};

static_assert(std::size(kBuiltinMigrations) > 0);
static_assert(kBuiltinMigrations[std::size(kBuiltinMigrations) - 1].version == kLatestSchemaVersion,
    "kLatestSchemaVersion must match the last built-in migration");

}  // namespace

std::span<const Migration> builtinMigrations() noexcept {
    return kBuiltinMigrations;
}

}  // namespace ernc::infrastructure
