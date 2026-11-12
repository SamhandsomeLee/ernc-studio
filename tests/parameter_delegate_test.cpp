#include "controls/parameter_delegates.hpp"
#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_table_model.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QApplication>
#include "controls/parameter_toggle_switch.hpp"

#include <QWidget>

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::presentation::FakeParameterColumn;
using ernc::presentation::ParameterDelegate;
using ernc::presentation::ParameterTableModel;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 找到参数在源模型里的行。
/// @param model 参数表。
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

/// @brief 取目录定义。
/// @param catalog 正式目录。
/// @param parameterId 目录主键。
/// @return 找到时指向目录内部定义。没有时为空。
const ernc::domain::ParameterDefinition* findDefinition(const ParameterCatalog& catalog, const char* parameterId) {
    const ernc::domain::Result<ParameterId> id = ParameterId::parse(parameterId);
    if (!id.ok() || id.tryValue() == nullptr) {
        return nullptr;
    }
    return catalog.find(id.value().text());
}

/// @brief ANC_OFF 的 0、1、2 分别显示开、关和 Unknown。
/// @param catalog 正式目录。
/// @param layout 正式布局。本用例不使用它暂存。
/// @return 三种显示都符合线值，且 2 不是「关」时为 true。
bool ancOffToggleWords(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    (void)layout;
    ParameterRuntimeStore store;
    ParameterDelegate delegate{&catalog, nullptr, &store};
    const ernc::domain::ParameterDefinition* definition = findDefinition(catalog, "ANC_OFF");
    if (definition == nullptr) {
        return fail("anc_off_toggle_words");
    }
    store.observe(definition->id, ScalarValue::fromFloat(0.0F), RuntimeTime{std::chrono::seconds{1}});
    const ernc::domain::ParameterRuntimeState* onState = store.find(definition->id);
    if (delegate.readCaption(*definition, onState) != QStringLiteral("开")
        || delegate.setCaption(*definition, onState) != QStringLiteral("未知")) {
        return fail("anc_off_toggle_words");
    }
    store.observe(definition->id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{2}});
    if (delegate.readCaption(*definition, store.find(definition->id)) != QStringLiteral("关")) {
        return fail("anc_off_toggle_words");
    }
    store.observe(definition->id, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{3}});
    const QString unknown = delegate.readCaption(*definition, store.find(definition->id));
    if (unknown != QStringLiteral("未知")) {
        return fail("anc_off_toggle_words");
    }
    return true;
}

/// @brief 选择「开」后暂存 float 0，并且不创建调度器。
/// @param catalog 正式目录。
/// @param layout 正式布局。暂存需要它做几何检查。
/// @return stagedValue 为 0、dirty 为 true、下拉只有开和关时为 true。
bool ancOffOnStagesZero(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterDelegate delegate{&catalog, &layout, &store};
    const int row = sourceRow(model, QStringLiteral("ANC_OFF"));
    if (row < 0) {
        return fail("anc_off_on_stages_zero");
    }
    QWidget parent;
    const QModelIndex setIndex = model.index(row, static_cast<int>(FakeParameterColumn::Set));
    if (delegate.createEditor(&parent, QStyleOptionViewItem{}, setIndex) != nullptr) {
        return fail("anc_off_on_stages_zero");
    }
    auto* toggle = new ernc::presentation::ParameterToggleSwitch(&parent);
    toggle->applyCaption(QStringLiteral("开"));
    delegate.setModelData(toggle, &model, setIndex);
    const ParameterId id = ParameterId::parse("ANC_OFF").value();
    const ernc::domain::ParameterRuntimeState* state = store.find(id);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("anc_off_on_stages_zero");
    }
    const ernc::domain::Result<float> staged = state->stagedValue->asFloat();
    if (!staged.ok() || staged.value() != 0.0F) {
        return fail("anc_off_on_stages_zero");
    }
    return true;
}

/// @brief Step 的 Set 列不可编辑，也不创建控件。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return flags 没有可编辑标志，createEditor 为空时为 true。
bool stepHasNoEditor(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterDelegate delegate{&catalog, &layout, &store};
    const int row = sourceRow(model, QStringLiteral("Step"));
    if (row < 0) {
        return fail("step_has_no_editor");
    }
    const QModelIndex setIndex = model.index(row, static_cast<int>(FakeParameterColumn::Set));
    if (model.flags(setIndex).testFlag(Qt::ItemIsEditable)) {
        return fail("step_has_no_editor");
    }
    QWidget parent;
    if (delegate.createEditor(&parent, QStyleOptionViewItem{}, setIndex) != nullptr) {
        return fail("step_has_no_editor");
    }
    return true;
}

/// @brief running_time 显示固件原值，不换成秒，也不可编辑。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 1100 仍显示为 1100，文字里没有「秒」时为 true。
bool rncAlpha1SetEditable(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterTableModel model{&catalog, &store};
    ParameterDelegate delegate{&catalog, &layout, &store};
    const int row = sourceRow(model, QStringLiteral("RNC_param.alpha1"));
    if (row < 0) {
        return fail("rnc_alpha1_set_editable");
    }
    const QModelIndex setIndex = model.index(row, static_cast<int>(FakeParameterColumn::Set));
    if (!model.flags(setIndex).testFlag(Qt::ItemIsEditable)) {
        return fail("rnc_alpha1_set_editable");
    }
    QWidget parent;
    if (delegate.createEditor(&parent, QStyleOptionViewItem{}, setIndex) == nullptr) {
        return fail("rnc_alpha1_set_editable");
    }
    return true;
}

bool runningTimeKeepsFirmwareValue(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    (void)layout;
    ParameterRuntimeStore store;
    ParameterDelegate delegate{&catalog, nullptr, &store};
    ParameterTableModel model{&catalog, &store};
    const ernc::domain::ParameterDefinition* definition = findDefinition(catalog, "running_time");
    if (definition == nullptr) {
        return fail("running_time_keeps_firmware_value");
    }
    store.observe(definition->id, ScalarValue::fromFloat(1100.0F), RuntimeTime{std::chrono::seconds{4}});
    const auto* state = store.find(definition->id);
    if (delegate.readCaption(*definition, state) != QStringLiteral("1100")
        || delegate.setCaption(*definition, state) != QStringLiteral("—")) {
        return fail("running_time_keeps_firmware_value");
    }
    const int row = sourceRow(model, QStringLiteral("running_time"));
    if (row < 0 || model.flags(model.index(row, static_cast<int>(FakeParameterColumn::Set))).testFlag(Qt::ItemIsEditable)) {
        return fail("running_time_keeps_firmware_value");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const ParameterCatalog& catalog, const FirmwareLayout& layout, bool& recognized) {
    recognized = true;
    if (name == "anc_off_toggle_words") {
        return ancOffToggleWords(catalog, layout);
    }
    if (name == "anc_off_on_stages_zero") {
        return ancOffOnStagesZero(catalog, layout);
    }
    if (name == "step_has_no_editor") {
        return stepHasNoEditor(catalog, layout);
    }
    if (name == "rnc_alpha1_set_editable") {
        return rncAlpha1SetEditable(catalog, layout);
    }
    if (name == "running_time_keeps_firmware_value") {
        return runningTimeKeepsFirmwareValue(catalog, layout);
    }
    recognized = false;
    return false;
}

} // namespace

/// @brief 运行一个参数委托用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是布局 manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_parameter_delegate_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    bool recognized = false;
    if (!runCase(argv[1], catalog.catalog(), layout.layout(), recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
