#include "ernc/application/simulator_can_msg_pulse.hpp"
#include "ernc/application/simulator_mirror_seed.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"
#include "ernc/protocol/wire_parser.hpp"

#include "slave_image.hpp"

#include <QCoreApplication>
#include <QSerialPort>
#include <QTimer>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::application::makeDemoSimulatorImage;
using ernc::application::patchSimulatorMirrorArrayZeros;
using ernc::application::seedSimulatorMirrorFromLegacy;
using ernc::application::writeCanMsgMirrorLevel;
using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalogLoader;
using ernc::simulator::SlaveImage;

struct Options {
    bool help{false};
    bool invalid{false};
    std::string port;
    int baudRate{115200};
    std::filesystem::path catalog{"catalog/parameter_catalog.json"};
    std::filesystem::path layout{"firmware/layout_manifest.json"};
    std::filesystem::path seedJson;
};

void printUsage() {
    std::cout << "ernc-serial-slave\n"
              << "  Modbus 从站（长度前缀 + FC03/FC16），镜像与桌面「虚拟从站」相同默认值。\n"
              << "  --help\n"
              << "  --port <COMx>          虚拟串口对端，例如 com0com 的 CNCA0。\n"
              << "  --baud <rate>          默认 115200。\n"
              << "  --catalog <path>       默认 catalog/parameter_catalog.json\n"
              << "  --layout <path>        默认 firmware/layout_manifest.json\n"
              << "  --seed-json <path>     可选，旧版 params JSON 覆盖标量（如 data/参数.json）。\n";
}

bool startsWithOption(std::string_view argument, std::string_view name) {
    return argument.size() > name.size() + 1 && argument.substr(0, name.size()) == name
        && argument[name.size()] == '=';
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index] == nullptr ? std::string_view{} : std::string_view{argv[index]};
        if (argument == "--help") {
            options.help = true;
            return options;
        }
        std::string_view name;
        std::string value;
        if (startsWithOption(argument, "--port") || startsWithOption(argument, "--baud")
            || startsWithOption(argument, "--catalog") || startsWithOption(argument, "--layout")
            || startsWithOption(argument, "--seed-json")) {
            const auto equal = argument.find('=');
            name = argument.substr(0, equal);
            value = std::string{argument.substr(equal + 1)};
        } else if (argument == "--port" || argument == "--baud" || argument == "--catalog" || argument == "--layout"
                   || argument == "--seed-json") {
            if (index + 1 >= argc || argv[index + 1] == nullptr) {
                options.invalid = true;
                return options;
            }
            name = argument;
            value = argv[++index];
        } else {
            options.invalid = true;
            return options;
        }
        if (value.empty()) {
            options.invalid = true;
            return options;
        }
        if (name == "--port") {
            options.port = value;
        } else if (name == "--baud") {
            options.baudRate = std::stoi(value);
        } else if (name == "--catalog") {
            options.catalog = value;
        } else if (name == "--layout") {
            options.layout = value;
        } else if (name == "--seed-json") {
            options.seedJson = value;
        }
    }
    if (!options.help && options.port.empty()) {
        options.invalid = true;
    }
    return options;
}

class SerialSlave final : public QObject {
    Q_OBJECT
public:
    SerialSlave(SlaveImage image, const ernc::domain::FirmwareLayout& layout, QSerialPort* port, QObject* parent = nullptr)
        : QObject(parent), image_(std::move(image)), layout_{&layout}, port_(port) {
        QObject::connect(port_, &QSerialPort::readyRead, this, &SerialSlave::onReadyRead);
        pulseTimer_.setInterval(1000);
        pulseTimer_.setSingleShot(false);
        QObject::connect(&pulseTimer_, &QTimer::timeout, this, &SerialSlave::onCanMsgPulse);
        canMsgHigh_ = true;
        (void)writeCanMsgMirrorLevel(image_, layout, canMsgHigh_);
        pulseTimer_.start();
    }

private:
    void onCanMsgPulse() {
        if (layout_ == nullptr) {
            return;
        }
        canMsgHigh_ = !canMsgHigh_;
        (void)writeCanMsgMirrorLevel(image_, *layout_, canMsgHigh_);
    }
    void onReadyRead() {
        if (port_ == nullptr) {
            return;
        }
        const QByteArray chunk = port_->readAll();
        const std::span<const std::uint8_t> incoming{
            reinterpret_cast<const std::uint8_t*>(chunk.constData()),
            static_cast<std::size_t>(chunk.size())};
        parser_.append(incoming);
        for (;;) {
            const ernc::protocol::ParseStep step = parser_.pull();
            if (step.kind == ernc::protocol::ParseStep::Kind::NeedMore) {
                break;
            }
            if (step.kind != ernc::protocol::ParseStep::Kind::Frame) {
                continue;
            }
            const ernc::simulator::SlaveReply reply =
                ernc::simulator::exchange(image_, step.frame);
            if (reply.responded && port_->isOpen()) {
                port_->write(reinterpret_cast<const char*>(reply.frame.data()),
                             static_cast<qint64>(reply.frame.size()));
            }
        }
    }

    SlaveImage image_;
    const ernc::domain::FirmwareLayout* layout_{nullptr};
    QSerialPort* port_{nullptr};
    ernc::protocol::WireParser parser_;
    QTimer pulseTimer_;
    bool canMsgHigh_{false};
};

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.help) {
        printUsage();
        return 0;
    }
    if (options.invalid) {
        printUsage();
        return 2;
    }

    QCoreApplication application(argc, argv);

    const CatalogLoad catalogLoad = ParameterCatalogLoader::loadFile(options.catalog);
    if (!catalogLoad.ok()) {
        std::cerr << "catalog load failed\n";
        return 2;
    }
    const LayoutLoad layoutLoad = FirmwareLayoutLoader::loadFile(options.layout);
    if (!layoutLoad.ok()) {
        std::cerr << "layout load failed\n";
        return 2;
    }

    std::vector<std::uint8_t> bytes = makeDemoSimulatorImage(catalogLoad.catalog(), layoutLoad.layout());
    if (bytes.empty()) {
        std::cerr << "empty simulator image\n";
        return 2;
    }
    if (!options.seedJson.empty()) {
        const ernc::infrastructure::LegacyParamJsonLoad loaded =
            ernc::infrastructure::loadLegacyParamJsonFile(options.seedJson);
        if (!loaded.ok()) {
            std::cerr << "seed-json load failed\n";
            return 2;
        }
        seedSimulatorMirrorFromLegacy(catalogLoad.catalog(), layoutLoad.layout(), bytes, loaded.document);
        patchSimulatorMirrorArrayZeros(catalogLoad.catalog(), layoutLoad.layout(), bytes);
    }

    SlaveImage image{std::move(bytes)};
    QSerialPort port;
    port.setPortName(QString::fromStdString(options.port));
    port.setBaudRate(options.baudRate);
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    port.setFlowControl(QSerialPort::NoFlowControl);
    if (!port.open(QIODevice::ReadWrite)) {
        std::cerr << "open port failed: " << options.port << '\n';
        return 3;
    }

    SerialSlave server{std::move(image), layoutLoad.layout(), &port};
    std::cout << "ernc-serial-slave listening on " << options.port << " @" << options.baudRate << '\n';
    return application.exec();
}

#include "main.moc"
