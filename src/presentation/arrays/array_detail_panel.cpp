#include "arrays/array_detail_panel.hpp"

#include "arrays/array_editor_model.hpp"
#include "ernc/domain/array_parameter.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace ernc::presentation {

ArrayDetailPanel::ArrayDetailPanel(const ernc::domain::ParameterCatalog* catalog, const ernc::domain::FirmwareLayout* layout,
                                   ernc::domain::ArrayRuntimeStore* arrayStore, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, layout_{layout}, arrayStore_{arrayStore} {
    setObjectName(QStringLiteral("erncArrayDetailPanel"));
    auto* root = new QVBoxLayout(this);
    title_ = new QLabel(QStringLiteral("未选择数组参数"), this);
    root->addWidget(title_);
    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("erncArrayDetailTable"));
    root->addWidget(table_, 1);
    auto* actions = new QHBoxLayout();
    auto* readButton = new QPushButton(QStringLiteral("读本参数"), this);
    auto* writeButton = new QPushButton(QStringLiteral("写已改"), this);
    auto* resetButton = new QPushButton(QStringLiteral("收回 Set"), this);
    resetButton->setVisible(false);
    actions->addWidget(readButton);
    actions->addWidget(writeButton);
    actions->addWidget(resetButton);
    actions->addStretch(1);
    root->addLayout(actions);
    connect(readButton, &QPushButton::clicked, this, [this] {
        if (readHandler_ && !parameterId_.isEmpty()) {
            readHandler_(parameterId_);
        }
    });
    connect(writeButton, &QPushButton::clicked, this, [this] {
        if (writeHandler_ && !parameterId_.isEmpty()) {
            writeHandler_(parameterId_);
        }
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        if (model_ != nullptr) {
            model_->resetSets();
        }
    });
}

void ArrayDetailPanel::setIoHandlers(std::function<void(const QString&)> readHandler,
                                     std::function<void(const QString&)> writeHandler) {
    readHandler_ = std::move(readHandler);
    writeHandler_ = std::move(writeHandler);
}

void ArrayDetailPanel::bindParameter(const QString& parameterId) {
    parameterId_ = parameterId;
    if (catalog_ == nullptr || layout_ == nullptr || arrayStore_ == nullptr || parameterId.isEmpty()) {
        title_->setText(QStringLiteral("未选择数组参数"));
        boundState_ = nullptr;
        if (model_ != nullptr) {
            model_->deleteLater();
            model_ = nullptr;
        }
        table_->setModel(nullptr);
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || !ernc::domain::shouldUseCompactArrayUi(definition, *layout_)) {
        title_->setText(QStringLiteral("当前参数不是紧凑数组"));
        boundState_ = nullptr;
        table_->setModel(nullptr);
        return;
    }
    title_->setText(parameterId);
    rebuildModel();
}

void ArrayDetailPanel::rebuildModel() {
    if (catalog_ == nullptr || layout_ == nullptr || arrayStore_ == nullptr || parameterId_.isEmpty()) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId_.toStdString());
    if (definition == nullptr || !definition->firmwarePath) {
        return;
    }
    const ernc::domain::LayoutResolve resolved = layout_->locate(*definition->firmwarePath);
    if (!resolved.ok()) {
        return;
    }
    const ernc::domain::FieldLayout& field = resolved.field();
    const ernc::domain::ScalarValue::Kind kind = ernc::domain::scalarKindForField(field);
    const ernc::domain::Result<ernc::domain::ParameterId> parsed =
        ernc::domain::ParameterId::parse(definition->id.text());
    if (!parsed.ok()) {
        return;
    }
    boundState_ = &arrayStore_->ensure(parsed.value(), field.slotCount, kind);
    if (model_ != nullptr) {
        model_->deleteLater();
    }
    model_ = new ArrayEditorModel(boundState_, this);
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    if (!writable()) {
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    } else {
        table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    }
}

bool ArrayDetailPanel::writable() const {
    if (catalog_ == nullptr || parameterId_.isEmpty()) {
        return false;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId_.toStdString());
    if (definition == nullptr || !definition->access) {
        return true;
    }
    return *definition->access == ernc::domain::AccessMode::ReadWrite;
}

void ArrayDetailPanel::refreshTable() {
    if (model_ != nullptr) {
        model_->layoutAboutToBeChanged();
        model_->layoutChanged();
    }
}

void ArrayDetailPanel::onArrayStoreChanged() {
    if (!parameterId_.isEmpty()) {
        bindParameter(parameterId_);
    }
}

}  // namespace ernc::presentation
