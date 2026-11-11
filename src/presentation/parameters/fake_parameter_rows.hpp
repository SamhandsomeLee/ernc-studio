#pragma once

#include <cstddef>
#include <iterator>
#include <string_view>

namespace ernc::presentation {

/// 假数据表的列。顺序固定，Read 列只用于显示。
enum class FakeParameterColumn {
    /// 状态词。不是设备会话状态。
    Status = 0,

    /// 目录参数 id。
    Parameter,

    /// 固件路径原文。模型不把它解析成地址。
    Path,

    /// 假的 Read 文本。这一列不可编辑。
    Read,

    /// 假的 Set 文本。本切片也不提供编辑。
    Set,

    /// 固定的更新时间文本。不是系统钟读数。
    UpdatedAt,

    /// 列数，不是一条数据。
    Count
};

/// 一行固定假数据。所有文字都是字面量，不是设备当前值。
struct FakeParameterRow {
    /// 状态词，例如「未读取」。
    std::string_view status;

    /// 目录里的参数 id。
    std::string_view parameterId;

    /// 与参数 id 对应的固件路径原文。
    std::string_view path;

    /// Read 列文本。没有读数时为「—」。
    std::string_view read;

    /// Set 列文本。没有暂存值时为「—」。
    std::string_view set;

    /// 更新时间文本。没有时间时为「—」。
    std::string_view updatedAt;
};

/// 三行固定假数据。数值故意不用设计示意里的 0.05 和 0.002。
inline constexpr FakeParameterRow kFakeParameterRows[] = {
    {"未读取", "ANC_OFF", "ANC_OFF", "—", "—", "—"},
    {"待写入", "RNC_param.alpha1", "RNC_param.alpha1", "1", "2", "2026-11-11T10:00:00Z"},
    {"数据已过期", "Step", "Step", "3", "—", "2026-11-11T09:00:00Z"},
};

/// @brief 按参数 id 找到固定假数据行。
/// @param parameterId 目录主键，只在本次调用期间借用。空文本和不在表里的 id 都表示没有假数据。
/// @return 指向 `kFakeParameterRows` 里那一行的指针。调用方不拥有，静态表一直有效。未找到时为空指针。
inline const FakeParameterRow* findFakeParameterRow(std::string_view parameterId) {
    if (parameterId.empty()) {
        return nullptr;
    }
    for (const FakeParameterRow& row : kFakeParameterRows) {
        if (row.parameterId == parameterId) {
            return &row;
        }
    }
    return nullptr;
}

/// @brief 取出一格假数据。
/// @param row 行号，从 0 开始。越界时返回空视图。
/// @param column `FakeParameterColumn` 的整数值。`Count` 和越界列返回空视图。
/// @return 指向静态字面量的文本，调用方不拥有。
inline std::string_view fakeParameterCell(int row, int column) {
    if (row < 0 || static_cast<std::size_t>(row) >= std::size(kFakeParameterRows)) {
        return {};
    }
    const FakeParameterRow& record = kFakeParameterRows[static_cast<std::size_t>(row)];
    switch (static_cast<FakeParameterColumn>(column)) {
    case FakeParameterColumn::Status:
        return record.status;
    case FakeParameterColumn::Parameter:
        return record.parameterId;
    case FakeParameterColumn::Path:
        return record.path;
    case FakeParameterColumn::Read:
        return record.read;
    case FakeParameterColumn::Set:
        return record.set;
    case FakeParameterColumn::UpdatedAt:
        return record.updatedAt;
    case FakeParameterColumn::Count:
        break;
    }
    return {};
}

/// @brief 这一列是否允许编辑。
/// @param column 列号。Read 列以及其余列在本切片都返回 false。
/// @return 恒为 false。模型用它决定是否加上可编辑标志。
inline bool fakeParameterColumnIsEditable(int column) {
    (void)column;
    return false;
}

/// @brief 列标题。
/// @param column 列号。越界时返回空视图。
/// @return 静态列名，调用方不拥有。
inline std::string_view fakeParameterColumnName(int column) {
    switch (static_cast<FakeParameterColumn>(column)) {
    case FakeParameterColumn::Status:
        return "状态";
    case FakeParameterColumn::Parameter:
        return "参数";
    case FakeParameterColumn::Path:
        return "路径";
    case FakeParameterColumn::Read:
        return "Read";
    case FakeParameterColumn::Set:
        return "Set";
    case FakeParameterColumn::UpdatedAt:
        return "更新时间";
    case FakeParameterColumn::Count:
        break;
    }
    return {};
}

} // namespace ernc::presentation
