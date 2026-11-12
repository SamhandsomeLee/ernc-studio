#pragma once

#include "ernc/domain/types.hpp"

#include <optional>
#include <vector>

namespace ernc::domain {

/// 单个参数在快照读数与当前 Store deviceValue 之间的差异。
struct SnapshotParameterDiff {
  /// 目录主键。
  ParameterId parameterId;

  /// 快照里保存的设备读数；快照未收录该参数时为空。
  std::optional<ScalarValue> snapshotValue;

  /// 当前 Store 里的 deviceValue；Store 没有成功读数时为空。
  std::optional<ScalarValue> storeValue;
};

/// 一次快照与运行态 Store 的对照结果。只比较 deviceValue，不含 stagedValue。
struct SnapshotDiff {
  /// 存在差异或仅出现在一侧的参数列表。
  std::vector<SnapshotParameterDiff> entries;
};

}  // namespace ernc::domain
