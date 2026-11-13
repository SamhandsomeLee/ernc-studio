#pragma once

#include "ernc/domain/array_runtime_store.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include <QWidget>

#include <functional>

class QTableView;
class QLabel;

namespace ernc::presentation {

class ArrayEditorModel;

/// 底部「数组详情」：Read/Set 表与读本参数 / 写已改（收回 Set 入口暂隐藏）。
class ArrayDetailPanel final : public QWidget {
    Q_OBJECT
public:
    ArrayDetailPanel(const ernc::domain::ParameterCatalog* catalog, const ernc::domain::FirmwareLayout* layout,
                     ernc::domain::ArrayRuntimeStore* arrayStore, QWidget* parent = nullptr);

    /// @brief 绑定当前数组并刷新表格。空 id 显示占位。
    void bindParameter(const QString& parameterId);

    /// @brief 登记设备读/写请求。
    void setIoHandlers(std::function<void(const QString&)> readHandler,
                       std::function<void(const QString&)> writeHandler);

    void refreshTable();

public slots:
    void onArrayStoreChanged();

private:
    void rebuildModel();
    bool writable() const;

    const ernc::domain::ParameterCatalog* catalog_{nullptr};
    const ernc::domain::FirmwareLayout* layout_{nullptr};
    ernc::domain::ArrayRuntimeStore* arrayStore_{nullptr};
    QString parameterId_;
    QLabel* title_{nullptr};
    QTableView* table_{nullptr};
    ArrayEditorModel* model_{nullptr};
    ernc::domain::ArrayState* boundState_{nullptr};
    std::function<void(const QString&)> readHandler_;
    std::function<void(const QString&)> writeHandler_;
};

}  // namespace ernc::presentation
