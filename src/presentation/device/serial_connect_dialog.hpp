#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;

namespace ernc::presentation {

/// 选择本机串口和波特率。波特率由操作员填写，不代表设备合同默认值。
class SerialConnectDialog final : public QDialog {
public:
    explicit SerialConnectDialog(QWidget* parent = nullptr);

    QString portName() const;
    int baudRate() const;

private:
    void refreshPorts();

    QComboBox* portBox_{nullptr};
    QSpinBox* baudBox_{nullptr};
};

}  // namespace ernc::presentation
