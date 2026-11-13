#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_table_model.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::SessionId;
using ernc::presentation::FakeParameterColumn;
using ernc::presentation::ParameterTableModel;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 找到 ANC_OFF 在目录表里的行。
/// @param model 参数表。只借用。
/// @param name 用例名。
/// @param row 成功时写入行号。
/// @return 找到时为 true。
bool findAnc(const ParameterTableModel& model, const char* name, int& row) {
    const QString anc = QStringLiteral("ANC_OFF");
    for (int candidate = 0; candidate < model.rowCount(); ++candidate) {
        const QString text = model.data(model.index(candidate, static_cast<int>(FakeParameterColumn::Parameter))).toString();
        if (text == anc) {
            row = candidate;
            return true;
        }
    }
    return fail(name);
}

/// @brief 读取一格显示文字。
/// @param model 参数表。
/// @param row 行号。
/// @param column 列。
/// @return DisplayRole 的文字。没有时为空。
QString cell(const ParameterTableModel& model, int row, FakeParameterColumn column) {
    return model.data(model.index(row, static_cast<int>(column))).toString();
}

/// @brief 读数 1 显示为最短十进制，状态是已读取。
/// @param catalog 正式目录。
/// @return Read 为 1、Set 为 —、更新时间是第 10 秒对应的北京时间文本时为 true。
bool observeShowsRead(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    const SessionId session = SessionId::fromRaw(7);
    model.bindSession(session);
    int row = -1;
    if (!findAnc(model, "observe_shows_read", row)) {
        return false;
    }
    const ParameterId id = ParameterId::parse("ANC_OFF").value();
    const RuntimeTime readAt{std::chrono::seconds{10}};
    store.observe(id, ScalarValue::fromFloat(1.0F), readAt);
    model.noteUpdated(session, id);
    const QString expectedTime = QStringLiteral("1970-01-01 08:00:10");
    if (cell(model, row, FakeParameterColumn::Status) != QStringLiteral("已读取")
        || cell(model, row, FakeParameterColumn::Read) != QString()
        || cell(model, row, FakeParameterColumn::Set) != QString()
        || cell(model, row, FakeParameterColumn::UpdatedAt) != expectedTime) {
        return fail("observe_shows_read");
    }
    const Qt::ItemFlags readFlags = model.flags(model.index(row, static_cast<int>(FakeParameterColumn::Read)));
    if (readFlags.testFlag(Qt::ItemIsEditable)) {
        return fail("observe_shows_read");
    }
    return true;
}

/// @brief 暂存 2 之后 Set 和状态更新，Read 仍是刚才的读数。
/// @param catalog 正式目录。
/// @return 状态为待写入，Read 为 1，Set 为 2 时为 true。
bool stageShowsSet(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    const SessionId session = SessionId::fromRaw(7);
    model.bindSession(session);
    int row = -1;
    if (!findAnc(model, "stage_shows_set", row)) {
        return false;
    }
    const ParameterId id = ParameterId::parse("ANC_OFF").value();
    store.observe(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{10}});
    store.stage(id, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{11}});
    model.noteUpdated(session, id);
    if (cell(model, row, FakeParameterColumn::Status) != QStringLiteral("待写入")
        || cell(model, row, FakeParameterColumn::Read) != QString()
        || cell(model, row, FakeParameterColumn::Set) != QString()) {
        return fail("stage_shows_set");
    }
    return true;
}

/// @brief 断连后的过期标记出现在状态列，数值仍保留。
/// @param catalog 正式目录。
/// @return 状态为数据已过期，Read 为 1，Set 为 2 时为 true。
bool offlineShowsStale(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    const SessionId session = SessionId::fromRaw(7);
    model.bindSession(session);
    int row = -1;
    if (!findAnc(model, "offline_shows_stale", row)) {
        return false;
    }
    const ParameterId id = ParameterId::parse("ANC_OFF").value();
    store.observe(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{10}});
    store.stage(id, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{11}});
    store.markOffline();
    model.noteDisconnected();
    if (cell(model, row, FakeParameterColumn::Status) != QStringLiteral("数据已过期")
        || cell(model, row, FakeParameterColumn::Read) != QString()
        || cell(model, row, FakeParameterColumn::Set) != QString()) {
        return fail("offline_shows_stale");
    }
    return true;
}

/// @brief 另一个会话号的通知不改变已经显示的文字。
/// @param catalog 正式目录。
/// @return 错误会话通知后仍是未读取，正确会话通知后才出现读数时为 true。
bool otherSessionIsIgnored(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    const SessionId current = SessionId::fromRaw(7);
    const SessionId other = SessionId::fromRaw(8);
    model.bindSession(current);
    int row = -1;
    if (!findAnc(model, "other_session_is_ignored", row)) {
        return false;
    }
    const ParameterId id = ParameterId::parse("ANC_OFF").value();
    store.observe(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{10}});
    model.noteUpdated(other, id);
    if (cell(model, row, FakeParameterColumn::Status) != QStringLiteral("未读取")
        || cell(model, row, FakeParameterColumn::Read) != QString()) {
        return fail("other_session_is_ignored");
    }
    model.noteUpdated(current, id);
    if (cell(model, row, FakeParameterColumn::Status) != QStringLiteral("已读取")
        || cell(model, row, FakeParameterColumn::Read) != QString()) {
        return fail("other_session_is_ignored");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalog 正式目录。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const ParameterCatalog& catalog, bool& recognized) {
    recognized = true;
    if (name == "observe_shows_read") {
        return observeShowsRead(catalog);
    }
    if (name == "stage_shows_set") {
        return stageShowsSet(catalog);
    }
    if (name == "offline_shows_stale") {
        return offlineShowsStale(catalog);
    }
    if (name == "other_session_is_ignored") {
        return otherSessionIsIgnored(catalog);
    }
    recognized = false;
    return false;
}

} // namespace

/// @brief 运行一个参数表模型用例。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON。两段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_parameter_store_model_tests <case> <parameter_catalog.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    if (!catalog.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    bool recognized = false;
    if (!runCase(argv[1], catalog.catalog(), recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
