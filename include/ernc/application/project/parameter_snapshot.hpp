#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/snapshot_diff.hpp"
#include "ernc/domain/types.hpp"

#include <QSqlDatabase>

#include <cstdint>
#include <optional>
#include <string_view>

namespace ernc::application {

/// 从运行态读数捕获快照时的元数据。
struct CaptureSnapshotRequest {
  /// `project.id`，须对应已打开的工程库。
  std::int64_t projectId{0};

  /// 捕获时记录的固件版本。
  std::string_view firmwareVersion;

  /// 捕获时记录的目录版本。
  std::string_view catalogVersion;

  /// 可选备注；空表示不写 note 列。
  std::optional<std::string_view> note;
};

/// 参数快照：捕获 deviceValue、按快照灌入 Store 观察结果、与 Store 做 deviceValue 对照。
class ParameterSnapshotService {
 public:
  /// @param database 已迁移的 `project.sqlite` 连接。
  explicit ParameterSnapshotService(QSqlDatabase& database);

  /// @brief 把目录中形状为 scalar 且 Store 含 deviceValue 的参数写入快照表。
  /// @return 成功时返回 `parameter_snapshot.id`。无任何可持久化值时仍创建快照头，行数为 0。
  ernc::domain::Result<std::int64_t> capture(const CaptureSnapshotRequest& request,
                                             const ernc::domain::ParameterCatalog& catalog,
                                             const ernc::domain::ParameterRuntimeStore& store);

  /// @brief 用快照里的 device 读数调用 `observe()`，不修改 stagedValue 与 dirty。
  /// @param observedAt 写入 Store 的读回时刻，由调用方固定以便测试断言。
  ernc::domain::Result<void> hydrateStoreForView(std::int64_t snapshotId,
                                                 const ernc::domain::ParameterCatalog& catalog,
                                                 ernc::domain::ParameterRuntimeStore& store,
                                                 ernc::domain::RuntimeTime observedAt);

  /// @brief 按 ParameterId 比较快照值与 Store 的 deviceValue。
  ernc::domain::Result<ernc::domain::SnapshotDiff> diffAgainstStore(
      std::int64_t snapshotId, const ernc::domain::ParameterRuntimeStore& store);

  /// @brief 把旧版 JSON 标量写入快照表。
  ernc::domain::Result<std::int64_t> importLegacyDocument(
      const CaptureSnapshotRequest& request, const ernc::domain::ParameterCatalog& catalog,
      const ernc::infrastructure::LegacyParamJsonDocument& document,
      std::string_view source = std::string_view{"legacy_json_import"});

 private:
  QSqlDatabase& database_;
};

/// @brief 运行态设备读数捕获时写入 `parameter_snapshot.source` 的固定文本。
inline constexpr std::string_view kRuntimeDeviceReadSource = "runtime_device_read";

}  // namespace ernc::application
