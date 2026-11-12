#pragma once

#include "ernc/application/stage_value.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QWidget;

namespace ernc::presentation {

/// 项目树「other」分类页。1 slot 标量可暂存；多槽参数在参数视图读写。
class OtherPage final : public QWidget {
public:
    explicit OtherPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                       const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    void reload();

    void setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer);
    void setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    bool containsParameter(const QString& parameterId) const;
    QString valueText(const QString& parameterId) const;
    bool hasEditor(const QString& parameterId) const;

    void commitValue(const QString& parameterId, const QString& text);
    void activate(const QString& parameterId);
    void emphasizeParameter(const QString& parameterId);

    QString selectedParameterId() const;

private:
    struct Control {
        QString id;
        QLabel* value = nullptr;
        QLineEdit* editor = nullptr;
        QWidget* row = nullptr;
    };

    Control* findControl(const QString& parameterId);
    const Control* findControl(const QString& parameterId) const;
    void notifyEdited(const ernc::domain::ParameterId& id);

    const ernc::domain::ParameterCatalog* catalog_ = nullptr;
    ernc::domain::ParameterRuntimeStore* store_ = nullptr;
    const ernc::domain::FirmwareLayout* layout_ = nullptr;
    std::optional<ernc::application::ParameterStager> stager_;
    std::vector<Control> controls_;

    QScrollArea* scroll_ = nullptr;
    QWidget* emphasizedRow_ = nullptr;
    std::function<void(const ernc::domain::ParameterId&)> editObserver_;
    std::function<void(const ernc::domain::ParameterId&)> selectionObserver_;
    QString selectedId_;
    bool reloading_{false};
};

}  // namespace ernc::presentation
