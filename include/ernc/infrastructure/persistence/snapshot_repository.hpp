#pragma once

#include "ernc/domain/types.hpp"

#include <QSqlDatabase>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {

/// 持久化后的单个标量快照值。valueJson 与 dataType 成对出现。
struct PersistedParameterValue {
  ernc::domain::ParameterId parameterId;
  std::string dataType;
  std::string valueJson;
  std::string validity;

  friend bool operator==(const PersistedParameterValue& left, const PersistedParameterValue& right) {
    return left.parameterId == right.parameterId && left.dataType == right.dataType
        && left.valueJson == right.valueJson && left.validity == right.validity;
  }
};

/// `parameter_snapshot` 与 `parameter_value` 表的 SQLite 访问。不打开连接。
class SnapshotRepository {
 public:
  /// @param database 已打开的 `project.sqlite` 连接，生存期须覆盖本对象使用期。
  explicit SnapshotRepository(QSqlDatabase& database);

  /// @brief 插入一条快照头记录。
  /// @param projectId `project.id`。
  /// @param createdAt ISO-8601 UTC 文本。
  /// @param source 来源标记，例如 `runtime_device_read`。
  /// @param firmwareVersion 固件版本文本。
  /// @param catalogVersion 目录版本文本。
  /// @param note 可选备注。
  /// @return 成功时返回新 snapshot 行的 id。
  ernc::domain::Result<std::int64_t> insertSnapshot(std::int64_t projectId, std::string_view createdAt,
                                                    std::string_view source, std::string_view firmwareVersion,
                                                    std::string_view catalogVersion,
                                                    std::optional<std::string_view> note);

  /// @brief 插入一条快照标量值。
  ernc::domain::Result<void> insertParameterValue(std::int64_t snapshotId, const ernc::domain::ParameterId& parameterId,
                                                  std::string_view dataType, std::string_view valueJson,
                                                  std::string_view validity);

  /// @brief 读取某次快照的全部标量值。
  ernc::domain::Result<std::vector<PersistedParameterValue>> loadParameterValues(std::int64_t snapshotId);

  /// @brief 统计某次快照的 `parameter_value` 行数。
  ernc::domain::Result<std::int64_t> countParameterValues(std::int64_t snapshotId);

 private:
  QSqlDatabase& database_;
};

}  // namespace ernc::infrastructure
