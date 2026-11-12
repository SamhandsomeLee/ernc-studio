#pragma once

#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QTimer;
class QTableWidget;

namespace ernc::domain {
class FirmwareLayout;
}

namespace ernc::presentation {

class DeviceController;

/// 「CAN信息」页：36 路名称 + 指示灯。数值来自 ParameterReader 读回的 Can_Msg。
class CanInfoPanel final : public QWidget {
    Q_OBJECT

public:
    explicit CanInfoPanel(const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);
    ~CanInfoPanel() override;

    void setNamesPath(const QString& path);
    void setSlotMapPath(const QString& path);

    void setDeviceController(DeviceController* controller);

    void reloadMapping();

public slots:
    void onPollTick();
    void onCanMsgUpdated(bool success);
    void onConnectionChanged(bool connected);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct MappingState;

    void refreshRows();
    void updatePollTimer();

    const ernc::domain::FirmwareLayout* layout_{nullptr};
    DeviceController* deviceController_{nullptr};
    std::unique_ptr<MappingState> mapping_;

    QTableWidget* tableWidget_{nullptr};
    QLabel* statusLabel_{nullptr};
    QTimer* pollTimer_{nullptr};
    std::vector<QLabel*> lampDots_;
    QString namesPath_;
    QString slotMapPath_;
};

}  // namespace ernc::presentation
