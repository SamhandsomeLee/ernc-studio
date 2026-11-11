#include "parameters/fake_parameter_rows.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::presentation::FakeParameterColumn;
using ernc::presentation::fakeParameterCell;
using ernc::presentation::fakeParameterColumnIsEditable;
using ernc::presentation::fakeParameterColumnName;
using ernc::presentation::kFakeParameterRows;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 判断两段文本是否相同。
/// @param left 期望或实际文本，只借用。
/// @param right 另一段文本，只借用。
/// @return 内容一致时为 true。
bool same(std::string_view left, std::string_view right) {
    return left == right;
}

/// @brief 三行假数据必须是约定字面量，并且不含设计示意里的小数。
/// @return 状态、参数 id、路径、Read、Set 和更新时间都匹配时为 true。
bool fixedRows() {
    if (std::size(kFakeParameterRows) != 3) {
        return fail("fixed_rows");
    }
    if (!same(fakeParameterCell(0, static_cast<int>(FakeParameterColumn::Status)), "未读取")
        || !same(fakeParameterCell(0, static_cast<int>(FakeParameterColumn::Parameter)), "ANC_OFF")
        || !same(fakeParameterCell(0, static_cast<int>(FakeParameterColumn::Path)), "ANC_OFF")
        || !same(fakeParameterCell(0, static_cast<int>(FakeParameterColumn::Read)), "—")
        || !same(fakeParameterCell(0, static_cast<int>(FakeParameterColumn::Set)), "—")
        || !same(fakeParameterCell(0, static_cast<int>(FakeParameterColumn::UpdatedAt)), "—")) {
        return fail("fixed_rows");
    }
    if (!same(fakeParameterCell(1, static_cast<int>(FakeParameterColumn::Status)), "待写入")
        || !same(fakeParameterCell(1, static_cast<int>(FakeParameterColumn::Parameter)), "RNC_param.alpha1")
        || !same(fakeParameterCell(1, static_cast<int>(FakeParameterColumn::Read)), "1")
        || !same(fakeParameterCell(1, static_cast<int>(FakeParameterColumn::Set)), "2")
        || !same(fakeParameterCell(1, static_cast<int>(FakeParameterColumn::UpdatedAt)), "2026-11-11T10:00:00Z")) {
        return fail("fixed_rows");
    }
    if (!same(fakeParameterCell(2, static_cast<int>(FakeParameterColumn::Status)), "数据已过期")
        || !same(fakeParameterCell(2, static_cast<int>(FakeParameterColumn::Parameter)), "Step")
        || !same(fakeParameterCell(2, static_cast<int>(FakeParameterColumn::Read)), "3")
        || !same(fakeParameterCell(2, static_cast<int>(FakeParameterColumn::Set)), "—")
        || !same(fakeParameterCell(2, static_cast<int>(FakeParameterColumn::UpdatedAt)), "2026-11-11T09:00:00Z")) {
        return fail("fixed_rows");
    }

    std::string joined;
    const int columns = static_cast<int>(FakeParameterColumn::Count);
    for (int row = 0; row < static_cast<int>(std::size(kFakeParameterRows)); ++row) {
        for (int column = 0; column < columns; ++column) {
            joined.append(fakeParameterCell(row, column));
        }
    }
    if (joined.find("0.05") != std::string::npos || joined.find("0.002") != std::string::npos
        || joined.find("0.06") != std::string::npos) {
        return fail("fixed_rows");
    }
    return true;
}

/// @brief Read 列以及其余列在本切片都不可编辑。
/// @return Read 列名正确且每一列都返回不可编辑时为 true。
bool readIsNotEditable() {
    const int readColumn = static_cast<int>(FakeParameterColumn::Read);
    if (!same(fakeParameterColumnName(readColumn), "Read")) {
        return fail("read_is_not_editable");
    }
    for (int column = 0; column < static_cast<int>(FakeParameterColumn::Count); ++column) {
        if (fakeParameterColumnIsEditable(column)) {
            return fail("read_is_not_editable");
        }
    }
    return true;
}

} // namespace

/// @brief 按用例名运行假数据表检查。
/// @param argc 参数个数，含程序名。
/// @param argv 程序名和用例名。用例名由 CTest 传入，本函数只借用。
/// @return 通过时为 0，未知用例或失败时为 1。
int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const std::string_view name = argv[1];
    if (name == "fixed_rows") {
        return fixedRows() ? 0 : 1;
    }
    if (name == "read_is_not_editable") {
        return readIsNotEditable() ? 0 : 1;
    }
    return 1;
}
