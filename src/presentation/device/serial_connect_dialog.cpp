#include "device/serial_connect_dialog.hpp"

#include "serial_transport.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ernc::presentation {

SerialConnectDialog::SerialConnectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("连接串口设备"));
    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        QStringLiteral("从本机枚举端口。波特率须与现场设备一致；程序不假设已冻结的默认波特率。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* form = new QFormLayout();
    portBox_ = new QComboBox(this);
    portBox_->setMinimumWidth(200);
    auto* refresh = new QPushButton(QStringLiteral("刷新端口"), this);
    auto* portRow = new QWidget(this);
    auto* portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->addWidget(portBox_, 1);
    portLayout->addWidget(refresh);
    form->addRow(QStringLiteral("端口"), portRow);

    baudBox_ = new QSpinBox(this);
    baudBox_->setRange(300, 921600);
    baudBox_->setSingleStep(300);
    baudBox_->setValue(115200);
    form->addRow(QStringLiteral("波特率"), baudBox_);
    layout->addLayout(form);

    QObject::connect(refresh, &QPushButton::clicked, this, [this] { refreshPorts(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshPorts();
}

QString SerialConnectDialog::portName() const {
    return portBox_ != nullptr ? portBox_->currentText() : QString{};
}

int SerialConnectDialog::baudRate() const {
    return baudBox_ != nullptr ? baudBox_->value() : 0;
}

void SerialConnectDialog::refreshPorts() {
    if (portBox_ == nullptr) {
        return;
    }
    const QString previous = portBox_->currentText();
    portBox_->clear();
    for (const std::string& name : ernc::serial::listSerialPortNames()) {
        portBox_->addItem(QString::fromStdString(name));
    }
    const int index = portBox_->findText(previous);
    if (index >= 0) {
        portBox_->setCurrentIndex(index);
    }
}

}  // namespace ernc::presentation
