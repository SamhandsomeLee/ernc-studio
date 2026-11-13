#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_filter_proxy.hpp"
#include "parameters/parameter_table_model.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QCoreApplication>

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
using ernc::presentation::FakeParameterColumn;
using ernc::presentation::ParameterFilterProxy;
using ernc::presentation::ParameterTableModel;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 在源模型里找参数 id 的行号。
/// @param model 源模型。
/// @param parameterId 目录主键。
/// @return 找到时为行号。没有时为 -1。
int sourceRow(const ParameterTableModel& model, const QString& parameterId) {
    for (int row = 0; row < model.rowCount(); ++row) {
        const QString text = model.data(model.index(row, static_cast<int>(FakeParameterColumn::Parameter))).toString();
        if (text == parameterId) {
            return row;
        }
    }
    return -1;
}

/// @brief 判断代理当前是否显示这个参数。
/// @param proxy 筛选代理。
/// @param parameterId 目录主键。
/// @return 某一代理行的参数列等于该 id 时为 true。
bool proxyShows(const ParameterFilterProxy& proxy, const QString& parameterId) {
    for (int row = 0; row < proxy.rowCount(); ++row) {
        const QString text =
            proxy.data(proxy.index(row, static_cast<int>(FakeParameterColumn::Parameter))).toString();
        if (text == parameterId) {
            return true;
        }
    }
    return false;
}

/// @brief ALL 搜索 alpha1 能找到目标，并且源模型行号不变。
/// @param catalog 正式目录。
/// @return 代理含 RNC_param.alpha1、不含 ANC_OFF，源行号仍是搜索前的值时为 true。
bool allSearchKeepsSourceOrder(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterFilterProxy proxy{&catalog, &store};
    proxy.setSourceModel(&model);
    const int alphaRow = sourceRow(model, QStringLiteral("RNC_param.alpha1"));
    const int ancRow = sourceRow(model, QStringLiteral("ANC_OFF"));
    if (alphaRow < 0 || ancRow < 0 || proxy.rowCount() != model.rowCount()) {
        return fail("all_search_keeps_source_order");
    }
    proxy.setPage(QStringLiteral("ALL"));
    proxy.setQuery(QStringLiteral("alpha1"));
    if (!proxyShows(proxy, QStringLiteral("RNC_param.alpha1")) || proxyShows(proxy, QStringLiteral("ANC_OFF"))) {
        return fail("all_search_keeps_source_order");
    }
    const QString sourceText =
        model.data(model.index(alphaRow, static_cast<int>(FakeParameterColumn::Parameter))).toString();
    if (sourceText != QStringLiteral("RNC_param.alpha1") || sourceRow(model, QStringLiteral("RNC_param.alpha1")) != alphaRow) {
        return fail("all_search_keeps_source_order");
    }
    proxy.sort(0);
    if (sourceRow(model, QStringLiteral("ANC_OFF")) != ancRow) {
        return fail("all_search_keeps_source_order");
    }
    proxy.sort(-1);
    if (sourceRow(model, QStringLiteral("ANC_OFF")) != ancRow) {
        return fail("all_search_keeps_source_order");
    }
    return true;
}

/// @brief 当前组是 ERNC_Flag 时，搜索不到另一个分类里的参数。
/// @param catalog 正式目录。
/// @return 能看到 ANC_OFF，看不到 RNC_param.alpha1 时为 true。大小写不同的 id 仍能匹配。
bool flagPageHidesOtherGroup(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterFilterProxy proxy{&catalog, &store};
    proxy.setSourceModel(&model);
    proxy.setPage(QStringLiteral("ERNC_Flag"));
    proxy.setQuery(QStringLiteral("anc_off"));
    if (!proxyShows(proxy, QStringLiteral("ANC_OFF")) || proxyShows(proxy, QStringLiteral("RNC_param.alpha1"))) {
        return fail("flag_page_hides_other_group");
    }
    proxy.setQuery(QStringLiteral("alpha1"));
    if (proxyShows(proxy, QStringLiteral("RNC_param.alpha1")) || proxyShows(proxy, QStringLiteral("ANC_OFF"))) {
        return fail("flag_page_hides_other_group");
    }
    return true;
}

/// @brief Dirty 开关只看 Store 的 dirty，过期但仍然 dirty 的行留下。
/// @param catalog 正式目录。
/// @return ANC_OFF 留下，只读过的 ENC_OFF 被隐藏时为 true。
/// @brief 读回筛选只保留未过期的 deviceValue。
/// @param catalog 正式目录。
/// @return 读过且在线的 ANC_OFF 留下，未读或仅过期的 ENC_OFF 隐藏时为 true。
bool readObservedShowsFreshDeviceValue(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterFilterProxy proxy{&catalog, &store};
    proxy.setSourceModel(&model);
    const ParameterId anc = ParameterId::parse("ANC_OFF").value();
    const ParameterId enc = ParameterId::parse("ENC_OFF").value();
    store.observe(anc, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{10}});
    store.observe(enc, ScalarValue::fromFloat(3.0F), RuntimeTime{std::chrono::seconds{12}});
    store.markOffline();
    model.noteUpdated(ernc::domain::SessionId::fromRaw(1), anc);
    model.noteUpdated(ernc::domain::SessionId::fromRaw(1), enc);
    proxy.setPage(QStringLiteral("ALL"));
    proxy.setQuery(QString());
    proxy.setReadObservedOnly(true);
    if (!proxyShows(proxy, QStringLiteral("ANC_OFF")) || proxyShows(proxy, QStringLiteral("ENC_OFF"))) {
        return fail("read_observed_shows_fresh_device_value");
    }
    return true;
}

bool dirtyKeepsStaleDirtyRow(const ParameterCatalog& catalog) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterFilterProxy proxy{&catalog, &store};
    proxy.setSourceModel(&model);
    const ParameterId anc = ParameterId::parse("ANC_OFF").value();
    const ParameterId enc = ParameterId::parse("ENC_OFF").value();
    store.observe(anc, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{10}});
    store.stage(anc, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{11}});
    store.markOffline();
    store.observe(enc, ScalarValue::fromFloat(3.0F), RuntimeTime{std::chrono::seconds{12}});
    model.noteUpdated(ernc::domain::SessionId::fromRaw(1), anc);
    model.noteUpdated(ernc::domain::SessionId::fromRaw(1), enc);
    proxy.setPage(QStringLiteral("ALL"));
    proxy.setQuery(QString());
    proxy.setDirtyOnly(true);
    if (!proxyShows(proxy, QStringLiteral("ANC_OFF")) || proxyShows(proxy, QStringLiteral("ENC_OFF"))) {
        return fail("dirty_keeps_stale_dirty_row");
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
    if (name == "all_search_keeps_source_order") {
        return allSearchKeepsSourceOrder(catalog);
    }
    if (name == "flag_page_hides_other_group") {
        return flagPageHidesOtherGroup(catalog);
    }
    if (name == "dirty_keeps_stale_dirty_row") {
        return dirtyKeepsStaleDirtyRow(catalog);
    }
    if (name == "read_observed_shows_fresh_device_value") {
        return readObservedShowsFreshDeviceValue(catalog);
    }
    recognized = false;
    return false;
}

} // namespace

/// @brief 运行一个参数筛选用例。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON。两段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_parameter_filter_tests <case> <parameter_catalog.json>\n";
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
