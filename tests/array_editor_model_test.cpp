#include "arrays/array_editor_model.hpp"

#include "ernc/domain/array_state.hpp"

#include <QCoreApplication>
#include <QModelIndex>

#include <iostream>
#include <string_view>

namespace {

using ernc::domain::ArrayState;
using ernc::domain::ScalarValue;
using ernc::presentation::ArrayEditorColumn;
using ernc::presentation::ArrayEditorModel;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool setColumnEditsOnlyThatIndex() {
    ArrayState state{10, ScalarValue::Kind::Float32};
    if (!state.setRead(0, ScalarValue::fromFloat(0.25F)).ok() || !state.setRead(2, ScalarValue::fromFloat(1.5F)).ok()) {
        return fail("set_column_edits_only_that_index");
    }
    ArrayEditorModel model{&state};
    const QModelIndex read = model.index(2, static_cast<int>(ArrayEditorColumn::Read));
    const QModelIndex set = model.index(2, static_cast<int>(ArrayEditorColumn::Set));
    if (model.flags(read).testFlag(Qt::ItemIsEditable) || !model.flags(set).testFlag(Qt::ItemIsEditable)) {
        return fail("set_column_edits_only_that_index");
    }
    if (model.setData(read, QStringLiteral("9")) || !model.setData(set, QStringLiteral("4"))) {
        return fail("set_column_edits_only_that_index");
    }
    const auto* first = state.element(0);
    const auto* edited = state.element(2);
    if (first == nullptr || edited == nullptr || !edited->dirty || first->dirty || !edited->set || model.data(read).toString() != QStringLiteral("1.5")
        || model.data(set).toString() != QStringLiteral("4") || model.data(model.index(0, 1)).toString() != QStringLiteral("—")) {
        return fail("set_column_edits_only_that_index");
    }
    model.resetSets();
    if (edited->dirty || !edited->set || model.data(set).toString() != QStringLiteral("1.5") || model.data(read).toString() != QStringLiteral("1.5")) {
        return fail("set_column_edits_only_that_index");
    }
    return true;
}

bool runCase(std::string_view name) {
    if (name == "set_column_edits_only_that_index") {
        return setColumnEditsOnlyThatIndex();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_array_editor_model_tests <case>\n";
        return 2;
    }
    return runCase(argv[1]) ? 0 : 1;
}
