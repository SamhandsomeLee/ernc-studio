#include "regions/region_workspace.hpp"

#include "ernc/application/region_writer.hpp"

#include "ernc/infrastructure/legacy_region_codec.hpp"
#include "regions/spec_radius_page.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <bit>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <sstream>
#include <system_error>

namespace ernc::presentation {
namespace {

using ernc::domain::ErrorCode;

/// @brief 为 JSON 字符串转义引号和反斜杠。
std::string escapeJson(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char character : text) {
        if (character == '"' || character == '\\') {
            out.push_back('\\');
        }
        out.push_back(character);
    }
    return out;
}

/// @brief 构造区域导入的 detail JSON。
std::string importDetailJson(const ernc::domain::RegionDefinition& region, const std::filesystem::path& source,
                             std::optional<std::uintmax_t> fileBytes) {
    std::ostringstream detail;
    detail << "{\"region\":\"" << escapeJson(region.path) << "\",\"expected_bytes\":" << region.size
           << ",\"expected_slots\":" << region.slotCount << ",\"source\":\"" << escapeJson(source.string()) << '"';
    if (fileBytes) {
        detail << ",\"file_bytes\":" << *fileBytes;
    }
    detail << '}';
    return detail.str();
}


/// @brief 用子字段做成一块可读写的区域定义。
/// @param child 目录中的子字段，只在本次调用期间借用。
/// @return 调用方拥有的定义。路径、长度和证据与子字段相同。
ernc::domain::RegionDefinition definitionFrom(const ernc::domain::SubRegion& child) {
    ernc::domain::RegionDefinition region;
    region.path = child.path;
    region.slot = child.slot;
    region.slotCount = child.slotCount;
    region.size = child.size;
    region.offset = child.offset;
    region.typeName = child.typeName;
    region.scalarType = child.scalarType;
    region.shape = child.shape;
    region.evidence = child.evidence;
    region.writable = child.writable;
    return region;
}

/// @brief 把 float 序列收成小端字节。
/// @param values 存储顺序的 float。
/// @return 每个值 4 字节。调用方拥有。
std::vector<std::uint8_t> bytesFromFloats(const std::vector<float>& values) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(values.size() * 4U);
    for (const float value : values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xFFU));
    }
    return bytes;
}

/// @brief 把小端的 4-byte slot 还原成 float，供旧版 TXT 导出。
std::optional<std::vector<float>> floatsFromBytes(const std::span<const std::uint8_t> bytes) {
    if ((bytes.size() % 4U) != 0U) {
        return std::nullopt;
    }
    std::vector<float> values;
    values.reserve(bytes.size() / 4U);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4U) {
        const std::uint32_t bits = static_cast<std::uint32_t>(bytes[offset])
            | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
            | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
            | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        values.push_back(std::bit_cast<float>(bits));
    }
    return values;
}

std::optional<ernc::domain::RegionDefinition> definitionForPath(
    const ernc::domain::RegionCatalog& catalog, const std::string_view path) {
    if (const ernc::domain::RegionDefinition* region = catalog.find(path)) {
        return *region;
    }
    for (const std::string_view root : {std::string_view{"ERNC_TF"}, std::string_view{"check_data"}}) {
        const ernc::domain::RegionDefinition* parent = catalog.find(root);
        if (parent == nullptr) {
            continue;
        }
        for (const ernc::domain::SubRegion& child : parent->subregions) {
            if (child.path == path) {
                return definitionFrom(child);
            }
        }
    }
    return std::nullopt;
}

std::vector<ernc::domain::RegionDefinition> importDefinitions(const ernc::domain::RegionCatalog& catalog) {
    std::vector<ernc::domain::RegionDefinition> definitions;
    for (const std::string_view root : {std::string_view{"ERNC_TF"}, std::string_view{"check_data"}}) {
        const ernc::domain::RegionDefinition* parent = catalog.find(root);
        if (parent == nullptr) {
            continue;
        }
        definitions.push_back(*parent);
        for (const ernc::domain::SubRegion& child : parent->subregions) {
            definitions.push_back(definitionFrom(child));
        }
    }
    return definitions;
}

QString leafName(const std::string& path) {
    const std::size_t separator = path.rfind('.');
    return QString::fromStdString(separator == std::string::npos ? path : path.substr(separator + 1U));
}

}  // namespace

RegionWorkspace::RegionWorkspace(const ernc::domain::RegionCatalog& catalog, ernc::application::RegionReader* reader,
                                 ernc::application::RegionWriter* writer, QWidget* parent)
    : QWidget(parent)
    , catalog_(catalog)
    , reader_(reader)
    , writer_(writer)
    , status_(QStringLiteral("空闲"))
    , progress_(QStringLiteral("空闲"))
    , regionBox_(new QComboBox(this))
    , fieldBox_(new QComboBox(this))
    , statusLabel_(new QLabel(status_, this))
    , progressLabel_(new QLabel(progress_, this))
    , progressBar_(new QProgressBar(this))
    , readButton_(new QPushButton(QStringLiteral("从设备读取"), this))
    , importButton_(new QPushButton(QStringLiteral("导入 BIN/TXT"), this))
    , exportButton_(new QPushButton(QStringLiteral("导出 BIN/TXT"), this))
    , cancelButton_(new QPushButton(QStringLiteral("取消"), this))
    , writeButton_(new QPushButton(QStringLiteral("写入"), this))
    , stack_(new QStackedWidget(this))
    , specPage_(nullptr) {
    const QStringList paths{QStringLiteral("ERNC_TF"), QStringLiteral("check_data"), QStringLiteral("ERNC_TF.SpecRadius")};
    for (const QString& path : paths) {
        if (catalog_.find(path.toStdString()) != nullptr) {
            regionBox_->addItem(path);
        }
    }
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto* scopeGroup = new QGroupBox(QStringLiteral("数据范围"), this);
    auto* scopeLayout = new QFormLayout(scopeGroup);
    scopeLayout->addRow(QStringLiteral("区域"), regionBox_);
    scopeLayout->addRow(QStringLiteral("字段"), fieldBox_);
    layout->addWidget(scopeGroup);
    layout->addWidget(statusLabel_);
    layout->addWidget(progressLabel_);
    progressBar_->setObjectName(QStringLiteral("erncRegionReadProgress"));
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setFormat(QStringLiteral("%v / %m slots  ·  %p%"));
    progressBar_->setVisible(false);
    layout->addWidget(progressBar_);
    auto* previewButton = new QPushButton(QStringLiteral("预览"), this);
    auto* primaryActions = new QHBoxLayout();
    primaryActions->addWidget(readButton_);
    primaryActions->addWidget(importButton_);
    layout->addLayout(primaryActions);
    auto* fileActions = new QHBoxLayout();
    fileActions->addWidget(exportButton_);
    fileActions->addWidget(cancelButton_);
    layout->addLayout(fileActions);
    auto* advancedActions = new QHBoxLayout();
    advancedActions->addWidget(previewButton);
    advancedActions->addWidget(writeButton_);
    layout->addLayout(advancedActions);
    writeButton_->setEnabled(false);
    cancelButton_->setEnabled(false);
    layout->addWidget(stack_, 1);
    QObject::connect(regionBox_, &QComboBox::currentTextChanged, this, [this](const QString& path) { selectRegion(path); });
    QObject::connect(fieldBox_, &QComboBox::currentTextChanged, this, [this](const QString& path) {
        selectField(path == QStringLiteral("全部") ? QString() : path);
    });
    QObject::connect(readButton_, &QPushButton::clicked, this, [this]() { readSelected(); });
    QObject::connect(importButton_, &QPushButton::clicked, this, [this]() {
        const QStringList fileNames = QFileDialog::getOpenFileNames(
            this, QStringLiteral("导入区域文件"), QString(),
            QStringLiteral("区域数据 (*.bin *.txt);;二进制 (*.bin);;文本 (*.txt);;所有文件 (*)"));
        if (fileNames.isEmpty()) {
            return;
        }
        std::vector<std::filesystem::path> paths;
        paths.reserve(static_cast<std::size_t>(fileNames.size()));
        for (const QString& fileName : fileNames) {
            paths.emplace_back(fileName.toStdString());
        }
        (void)importFiles(paths);
    });
    QObject::connect(exportButton_, &QPushButton::clicked, this, [this]() {
        const QString fileName = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出区域数据"), QString(),
            QStringLiteral("二进制 (*.bin);;文本 (*.txt)"));
        if (fileName.isEmpty()) {
            return;
        }
        if (fileName.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
            (void)exportTxt(fileName.toStdString());
        } else {
            (void)exportBin(fileName.toStdString());
        }
    });
    QObject::connect(previewButton, &QPushButton::clicked, this, [this]() { previewSelected(); });
    QObject::connect(writeButton_, &QPushButton::clicked, this, [this]() { writeSelected(); });
    QObject::connect(cancelButton_, &QPushButton::clicked, this, [this]() { cancelTransfer(); });
    if (regionBox_->count() > 0) {
        selectRegion(regionBox_->currentText());
    }
}

bool RegionWorkspace::selectRegion(const QString& path) {
    if (catalog_.find(path.toStdString()) == nullptr) {
        return false;
    }
    regionPath_ = path;
    fieldPath_.clear();
    if (regionBox_->currentText() != path) {
        regionBox_->setCurrentText(path);
    }
    refreshSelection();
    clearImported();
    return true;
}

bool RegionWorkspace::selectField(const QString& path) {
    if (!path.isEmpty()) {
        const ernc::domain::RegionDefinition* parent = catalog_.find(regionPath_.toStdString());
        bool found = false;
        if (parent != nullptr) {
            for (const ernc::domain::SubRegion& child : parent->subregions) {
                if (QString::fromStdString(child.path) == path) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }
    }
    fieldPath_ = path;
    refreshSelection();
    clearImported();
    return true;
}

ernc::domain::Result<void> RegionWorkspace::readSelected() {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    if (region == nullptr) {
        status_ = QStringLiteral("未选择读取范围");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (!ernc::domain::realReadPermitted(region->evidence)) {
        status_ = QStringLiteral("区域读取未授权（证据等级不支持 FC03）");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    ernc::domain::Result<void> started = ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    if (startRead_) {
        started = startRead_(QString::fromStdString(region->path));
    } else if (reader_ != nullptr) {
        started = reader_->start(*region);
    } else {
        status_ = QStringLiteral("未绑定设备：请先在「设备」菜单连接虚拟从站或串口");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::DeviceDisconnected);
    }
    if (!started.ok()) {
        const ErrorCode error = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
        if (error == ErrorCode::DeviceDisconnected) {
            status_ = QStringLiteral("未连接设备");
        } else if (error == ErrorCode::InvalidParameter) {
            status_ = QStringLiteral("读取被拒绝：设备忙、已有区域任务或区域目录未加载");
        } else {
            status_ = QStringLiteral("读取入队失败");
        }
    } else {
        activeReadPath_ = QString::fromStdString(region->path);
        status_ = QStringLiteral("正在读取 %1").arg(activeReadPath_);
        setReadRunning(true);
        noteReadProgress(activeReadPath_, 0, region->size);
    }
    statusLabel_->setText(status_);
    refreshProgress();
    return started;
}

ernc::domain::Result<void> RegionWorkspace::importFiles(const std::vector<std::filesystem::path>& sources) {
    if (sources.empty()) {
        return ernc::domain::Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::vector<ernc::domain::RegionDefinition> definitions = importDefinitions(catalog_);
    std::size_t importedCount = 0;
    for (const std::filesystem::path& source : sources) {
        struct Candidate {
            ernc::domain::RegionDefinition region;
            std::vector<std::uint8_t> bytes;
        };
        std::vector<Candidate> candidates;
        const QString suffix = QFileInfo(QString::fromStdString(source.string())).suffix().toLower();
        for (const ernc::domain::RegionDefinition& definition : definitions) {
            if (suffix == QStringLiteral("bin")) {
                const ernc::domain::Result<std::vector<std::uint8_t>> loaded =
                    ernc::infrastructure::readRegionBin(definition, source);
                if (loaded.ok() && loaded.tryValue() != nullptr) {
                    candidates.push_back(Candidate{definition, loaded.value()});
                }
            } else if (suffix == QStringLiteral("txt")) {
                const ernc::domain::Result<std::vector<float>> loaded =
                    ernc::infrastructure::readRegionTxt(definition, source);
                if (loaded.ok() && loaded.tryValue() != nullptr) {
                    candidates.push_back(Candidate{definition, bytesFromFloats(loaded.value())});
                }
            }
        }
        if (candidates.empty()) {
            status_ = QStringLiteral("无法识别文件：%1").arg(QString::fromStdString(source.filename().string()));
            statusLabel_->setText(status_);
            return ernc::domain::Result<void>::failure(ErrorCode::InvalidParameter);
        }

        std::size_t chosen = candidates.size();
        const QString fileStem = QString::fromStdString(source.stem().string()).toLower();
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (fileStem.contains(leafName(candidates[index].region.path).toLower())) {
                if (chosen != candidates.size()) {
                    chosen = candidates.size();
                    break;
                }
                chosen = index;
            }
        }
        if (chosen == candidates.size() && selected_) {
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                if (candidates[index].region.path == selected_->path) {
                    chosen = index;
                    break;
                }
            }
        }
        if (chosen == candidates.size() && candidates.size() == 1U) {
            chosen = 0;
        }
        if (chosen == candidates.size()) {
            QStringList choices;
            for (const Candidate& candidate : candidates) {
                choices.push_back(QString::fromStdString(candidate.region.path));
            }
            bool accepted = false;
            const QString choice = QInputDialog::getItem(
                this, QStringLiteral("选择数据字段"),
                QStringLiteral("%1 与多个字段长度相同，请选择其含义：")
                    .arg(QString::fromStdString(source.filename().string())),
                choices, 0, false, &accepted);
            if (!accepted) {
                return ernc::domain::Result<void>::failure(ErrorCode::OperationCancelled);
            }
            chosen = static_cast<std::size_t>(choices.indexOf(choice));
        }

        Candidate& loaded = candidates[chosen];
        if (selected_ && selected_->path == loaded.region.path) {
            imported_ = loaded.bytes;
            preview_.reset();
            previewBytes_.reset();
            if (specPage_ != nullptr && specPage_->isVisible()) {
                specPage_->loadReadBytes(*imported_);
            }
        }
        if (bytesHandler_) {
            bytesHandler_(loaded.region, loaded.bytes);
        }
        ++importedCount;
        notifyUiAction(suffix == QStringLiteral("txt") ? "region.import.txt" : "region.import.bin",
                       importDetailJson(loaded.region, source, loaded.bytes.size()), true, ErrorCode::Unknown);
    }
    status_ = QStringLiteral("已导入 %1 个文件并更新曲线").arg(importedCount);
    statusLabel_->setText(status_);
    progress_ = QStringLiteral("文件导入完成");
    progressLabel_->setText(progress_);
    writeButton_->setEnabled(false);
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> RegionWorkspace::importBin(const std::filesystem::path& source) {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    if (region == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    std::error_code sizeError;
    const std::uintmax_t fileBytes = std::filesystem::file_size(source, sizeError);
    const std::optional<std::uintmax_t> bytesOnDisk =
        sizeError ? std::optional<std::uintmax_t>{} : std::optional<std::uintmax_t>{fileBytes};
    const ernc::domain::Result<std::vector<std::uint8_t>> loaded = ernc::infrastructure::readRegionBin(*region, source);
    if (!loaded.ok() || loaded.tryValue() == nullptr) {
        status_ = QStringLiteral("导入失败");
        statusLabel_->setText(status_);
        const ErrorCode error = loaded.ok() ? ErrorCode::Unknown : loaded.error();
        notifyUiAction("region.import.bin", importDetailJson(*region, source, bytesOnDisk), false, error);
        return ernc::domain::Result<void>::failure(error);
    }
    imported_ = loaded.value();
    preview_.reset();
    previewBytes_.reset();
    if (specPage_ != nullptr && specPage_->isVisible()) {
        specPage_->loadReadBytes(*imported_);
    }
    status_ = QStringLiteral("已导入，尚未写入");
    statusLabel_->setText(status_);
    writeButton_->setEnabled(false);
    refreshProgress();
    notifyUiAction("region.import.bin", importDetailJson(*region, source, bytesOnDisk), true, ErrorCode::Unknown);
    emitBytesAvailable();
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> RegionWorkspace::importTxt(const std::filesystem::path& source) {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    if (region == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    std::error_code sizeError;
    const std::uintmax_t fileBytes = std::filesystem::file_size(source, sizeError);
    const std::optional<std::uintmax_t> bytesOnDisk =
        sizeError ? std::optional<std::uintmax_t>{} : std::optional<std::uintmax_t>{fileBytes};
    const ernc::domain::Result<std::vector<float>> loaded = ernc::infrastructure::readRegionTxt(*region, source);
    if (!loaded.ok() || loaded.tryValue() == nullptr) {
        status_ = QStringLiteral("导入失败");
        statusLabel_->setText(status_);
        const ErrorCode error = loaded.ok() ? ErrorCode::Unknown : loaded.error();
        notifyUiAction("region.import.txt", importDetailJson(*region, source, bytesOnDisk), false, error);
        return ernc::domain::Result<void>::failure(error);
    }
    imported_ = bytesFromFloats(loaded.value());
    preview_.reset();
    previewBytes_.reset();
    if (specPage_ != nullptr && specPage_->isVisible()) {
        specPage_->loadReadBytes(*imported_);
    }
    status_ = QStringLiteral("已导入，尚未写入");
    statusLabel_->setText(status_);
    writeButton_->setEnabled(false);
    refreshProgress();
    notifyUiAction("region.import.txt", importDetailJson(*region, source, bytesOnDisk), true, ErrorCode::Unknown);
    emitBytesAvailable();
    return ernc::domain::Result<void>::success();
}

void RegionWorkspace::setUiActivityRecorder(
    std::function<void(std::string_view, std::string_view, bool, ernc::domain::ErrorCode)> recorder) {
    uiRecorder_ = std::move(recorder);
}

void RegionWorkspace::setReadHandlers(
    std::function<ernc::domain::Result<void>(const QString&)> start, std::function<void()> cancel) {
    startRead_ = std::move(start);
    cancelRead_ = std::move(cancel);
}

void RegionWorkspace::setWriteHandlers(std::function<ernc::domain::Result<void>(const QString&, const QByteArray&)> start,
                                     std::function<void()> cancel) {
    startWrite_ = std::move(start);
    cancelWrite_ = std::move(cancel);
}

void RegionWorkspace::noteReadProgress(const QString& path, const quint64 filledBytes, const quint64 plannedBytes) {
    if (!activeReadPath_.isEmpty() && path != activeReadPath_) {
        return;
    }
    const quint64 filledSlots = filledBytes / 4U;
    const quint64 plannedSlots = plannedBytes / 4U;
    progressBar_->setRange(0, static_cast<int>(std::min<quint64>(plannedSlots, static_cast<quint64>(INT_MAX))));
    progressBar_->setValue(static_cast<int>(std::min<quint64>(filledSlots, static_cast<quint64>(INT_MAX))));
    progressBar_->setVisible(true);
    progress_ = QStringLiteral("正在读取 · %1 / %2 slots").arg(filledSlots).arg(plannedSlots);
    progressLabel_->setText(progress_);
}

void RegionWorkspace::noteReadFinished(const bool success, const QString& path, const QByteArray& bytes,
                                       const int errorCode) {
    if (!activeReadPath_.isEmpty() && path != activeReadPath_) {
        return;
    }
    const std::optional<ernc::domain::RegionDefinition> definition = definitionForPath(catalog_, path.toStdString());
    if (success && definition && bytes.size() == static_cast<qsizetype>(definition->size)) {
        imported_ = std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.constData()) + bytes.size());
        selected_ = *definition;
        regionPath_ = path.section('.', 0, 0) == QStringLiteral("check_data")
            ? QStringLiteral("check_data")
            : (path == QStringLiteral("ERNC_TF.SpecRadius") ? QStringLiteral("ERNC_TF.SpecRadius")
                                                             : QStringLiteral("ERNC_TF"));
        fieldPath_ = catalog_.find(path.toStdString()) != nullptr ? QString() : path;
        preview_.reset();
        previewBytes_.reset();
        status_ = QStringLiteral("读取完成，可导出 BIN/TXT");
        progress_ = QStringLiteral("读取完成 · %1 slots").arg(definition->slotCount);
        progressBar_->setRange(0, static_cast<int>(definition->slotCount));
        progressBar_->setValue(static_cast<int>(definition->slotCount));
        if (specPage_ != nullptr && specPage_->isVisible() && selected_->path == "ERNC_TF.SpecRadius") {
            specPage_->loadReadBytes(*imported_);
        }
        emitBytesAvailable();
        notifyUiAction("region.read.finish", importDetailJson(*definition, {}, bytes.size()), true, ErrorCode::Unknown);
    } else {
        const ErrorCode error = static_cast<ErrorCode>(errorCode);
        status_ = error == ErrorCode::OperationCancelled ? QStringLiteral("读取已取消，未发布不完整数据")
                                                         : QStringLiteral("读取失败，未发布不完整数据");
        progress_ = QStringLiteral("传输未完成");
        notifyUiAction("region.read.finish", std::string{"{\"region\":\""} + escapeJson(path.toStdString()) + "\"}",
                       false, error);
    }
    statusLabel_->setText(status_);
    progressLabel_->setText(progress_);
    setReadRunning(false);
    activeReadPath_.clear();
}

void RegionWorkspace::noteWriteProgress(const QString& path, const quint64 verifiedBytes, const quint64 plannedBytes) {
    if (!activeWritePath_.isEmpty() && path != activeWritePath_) {
        return;
    }
    const quint64 verifiedSlots = verifiedBytes / 4U;
    const quint64 plannedSlots = plannedBytes / 4U;
    progressBar_->setRange(0, static_cast<int>(std::min<quint64>(plannedSlots, static_cast<quint64>(INT_MAX))));
    progressBar_->setValue(static_cast<int>(std::min<quint64>(verifiedSlots, static_cast<quint64>(INT_MAX))));
    progressBar_->setVisible(true);
    progress_ = QStringLiteral("正在写入 · %1 / %2 slots").arg(verifiedSlots).arg(plannedSlots);
    progressLabel_->setText(progress_);
}

void RegionWorkspace::noteWriteFinished(const bool success, const QString& path, const int errorCode) {
    if (!activeWritePath_.isEmpty() && path != activeWritePath_) {
        return;
    }
    if (success) {
        status_ = QStringLiteral("写入完成（读回已验证）");
        progress_ = QStringLiteral("写入完成");
        notifyUiAction("region.write.finish", std::string{"{\"region\":\""} + escapeJson(path.toStdString()) + "\"}", true,
                       ErrorCode::Unknown);
    } else {
        const ErrorCode error = static_cast<ErrorCode>(errorCode);
        status_ = error == ErrorCode::OperationCancelled ? QStringLiteral("写入已取消")
                                                         : QStringLiteral("写入失败或未完全验证");
        progress_ = QStringLiteral("传输未完成");
        notifyUiAction("region.write.finish", std::string{"{\"region\":\""} + escapeJson(path.toStdString()) + "\"}", false,
                       error);
    }
    statusLabel_->setText(status_);
    progressLabel_->setText(progress_);
    setWriteRunning(false);
    activeWritePath_.clear();
}

void RegionWorkspace::notifyUiAction(std::string_view action, std::string_view detail, bool success,
                                     ernc::domain::ErrorCode error) {
    if (uiRecorder_) {
        uiRecorder_(action, detail, success, error);
    }
}

void RegionWorkspace::setBytesAvailableHandler(
    std::function<void(const ernc::domain::RegionDefinition&, std::span<const std::uint8_t>)> handler) {
    bytesHandler_ = std::move(handler);
}

void RegionWorkspace::emitBytesAvailable() {
    if (!bytesHandler_ || !selected_ || !imported_ || imported_->size() != selected_->size) {
        return;
    }
    bytesHandler_(*selected_, *imported_);
}

ernc::domain::Result<ernc::application::RegionWritePreview> RegionWorkspace::previewSelected() {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    const std::optional<std::vector<std::uint8_t>> bytes = currentBytes();
    if (region == nullptr || !bytes) {
        status_ = QStringLiteral("预览失败");
        statusLabel_->setText(status_);
        writeButton_->setEnabled(false);
        return ernc::domain::Result<ernc::application::RegionWritePreview>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    ernc::domain::Result<ernc::application::RegionWritePreview> preview =
        writer_ != nullptr ? writer_->preview(*region, *bytes) : ernc::application::previewRegionTransfer(*region, *bytes);
    if (!preview.ok() || preview.tryValue() == nullptr) {
        preview_.reset();
        previewBytes_.reset();
        status_ = QStringLiteral("预览失败");
        writeButton_->setEnabled(false);
    } else {
        preview_ = preview.value();
        previewBytes_ = *bytes;
        status_ = preview_->commitPermitted ? QStringLiteral("预览可写入") : QStringLiteral("预览完成，当前区域不能写入");
        writeButton_->setEnabled(true);
    }
    statusLabel_->setText(status_);
    refreshProgress();
    return preview;
}

ernc::domain::Result<void> RegionWorkspace::writeSelected() {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    const std::optional<std::vector<std::uint8_t>> bytes = currentBytes();
    if (region == nullptr || !preview_ || !previewBytes_ || !bytes || *bytes != *previewBytes_ || !preview_->commitPermitted) {
        status_ = QStringLiteral("写入被拒绝");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    ernc::domain::Result<void> started = ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    const QByteArray payload(reinterpret_cast<const char*>(previewBytes_->data()),
                             static_cast<qsizetype>(previewBytes_->size()));
    if (startWrite_) {
        started = startWrite_(QString::fromStdString(region->path), payload);
    } else if (writer_ != nullptr) {
        started = writer_->start(*region, *previewBytes_);
    }
    if (started.ok()) {
        activeWritePath_ = QString::fromStdString(region->path);
        setWriteRunning(true);
        status_ = QStringLiteral("正在写入 %1").arg(activeWritePath_);
    } else {
        status_ = QStringLiteral("写入被拒绝");
    }
    statusLabel_->setText(status_);
    refreshProgress();
    return started;
}

ernc::domain::Result<void> RegionWorkspace::exportBin(const std::filesystem::path& destination) {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    const std::optional<std::vector<std::uint8_t>> bytes = currentBytes();
    if (region == nullptr || !bytes || bytes->size() != region->size) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return ernc::infrastructure::writeRegionBin(*region, *bytes, destination);
}

ernc::domain::Result<void> RegionWorkspace::exportTxt(const std::filesystem::path& destination) {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    const std::optional<std::vector<std::uint8_t>> bytes = currentBytes();
    if (region == nullptr || !bytes || bytes->size() != region->size) {
        return ernc::domain::Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::optional<std::vector<float>> values = floatsFromBytes(*bytes);
    if (!values) {
        return ernc::domain::Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const bool wholeRegion = region->path == "ERNC_TF" || region->path == "check_data";
    return ernc::infrastructure::writeRegionTxt(
        *region, *values, destination,
        wholeRegion ? ernc::infrastructure::RegionTxtLayout::WholeRegion
                    : ernc::infrastructure::RegionTxtLayout::Field);
}

ernc::domain::Result<void> RegionWorkspace::exportCancelled(const ernc::application::RegionTransferRecord& record,
                                                           const std::filesystem::path&) {
    if (!record.canSaveAsCompleteBin()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
}

void RegionWorkspace::cancelTransfer() {
    if (cancelRead_) {
        cancelRead_();
    } else if (reader_ != nullptr) {
        reader_->cancel();
    }
    if (cancelWrite_) {
        cancelWrite_();
    } else if (writer_ != nullptr) {
        writer_->cancel();
    }
    status_ = (readRunning_ || writeRunning_) ? QStringLiteral("正在取消，等待当前分包结束") : QStringLiteral("已取消");
    statusLabel_->setText(status_);
    refreshProgress();
}

QString RegionWorkspace::statusText() const {
    return status_;
}

QString RegionWorkspace::progressText() const {
    return progress_;
}

bool RegionWorkspace::writeActionEnabled() const noexcept {
    return writeButton_->isEnabled();
}

bool RegionWorkspace::specRadiusVisible() const noexcept {
    return specPage_ != nullptr && stack_->currentWidget() == specPage_;
}

int RegionWorkspace::specRadiusRows() const noexcept {
    if (!specRadiusVisible() || specPage_->model() == nullptr) {
        return 0;
    }
    return specPage_->model()->rowCount();
}

const ernc::domain::RegionDefinition* RegionWorkspace::selectedRegion() const noexcept {
    return selected_ ? &*selected_ : nullptr;
}

void RegionWorkspace::clearImported() {
    imported_.reset();
    preview_.reset();
    previewBytes_.reset();
    writeButton_->setEnabled(false);
    status_ = QStringLiteral("空闲");
    progress_ = QStringLiteral("空闲");
    statusLabel_->setText(status_);
    progressLabel_->setText(progress_);
    progressBar_->setVisible(false);
}

void RegionWorkspace::refreshSelection() {
    const ernc::domain::RegionDefinition* parent = catalog_.find(regionPath_.toStdString());
    fieldBox_->blockSignals(true);
    fieldBox_->clear();
    fieldBox_->addItem(QStringLiteral("全部"));
    if (parent != nullptr) {
        for (const ernc::domain::SubRegion& child : parent->subregions) {
            fieldBox_->addItem(QString::fromStdString(child.path));
        }
    }
    if (fieldPath_.isEmpty()) {
        fieldBox_->setCurrentIndex(0);
    } else {
        fieldBox_->setCurrentText(fieldPath_);
    }
    fieldBox_->blockSignals(false);

    selected_.reset();
    if (parent != nullptr && fieldPath_.isEmpty()) {
        selected_ = *parent;
    } else if (parent != nullptr) {
        for (const ernc::domain::SubRegion& child : parent->subregions) {
            if (QString::fromStdString(child.path) == fieldPath_) {
                selected_ = definitionFrom(child);
                break;
            }
        }
    }
    if (regionPath_ == QStringLiteral("ERNC_TF.SpecRadius") && parent != nullptr) {
        selected_ = *parent;
    }

    if (specPage_ != nullptr) {
        stack_->removeWidget(specPage_);
        specPage_->deleteLater();
        specPage_ = nullptr;
    }
    while (stack_->count() > 0) {
        QWidget* widget = stack_->widget(0);
        stack_->removeWidget(widget);
        widget->deleteLater();
    }
    const bool showSpec = selected_ && selected_->path == "ERNC_TF.SpecRadius";
    if (showSpec) {
        specPage_ = new SpecRadiusPage(*selected_, stack_);
        stack_->addWidget(specPage_);
        stack_->setCurrentWidget(specPage_);
    } else {
        auto* summary = new QLabel(selected_ ? QString::fromStdString(selected_->path) : QStringLiteral("未选择"), stack_);
        stack_->addWidget(summary);
        stack_->setCurrentWidget(summary);
    }
}

std::optional<std::vector<std::uint8_t>> RegionWorkspace::currentBytes() const {
    if (specPage_ != nullptr && specRadiusVisible()) {
        const ernc::domain::Result<std::vector<std::uint8_t>> sets = specPage_->setBytes();
        if (sets.ok() && sets.tryValue() != nullptr) {
            return sets.value();
        }
    }
    if (imported_ && selected_ && imported_->size() == selected_->size) {
        return imported_;
    }
    return std::nullopt;
}

void RegionWorkspace::refreshProgress() {
    if (reader_ != nullptr && reader_->busy()) {
        const ernc::application::RegionReadProgress progress = reader_->progress();
        progress_ = QStringLiteral("已接收 %1/%2 槽").arg(progress.filledBytes / 4U).arg(progress.plannedBytes / 4U);
    } else if (writer_ != nullptr && writer_->busy()) {
        const ernc::application::RegionWriteProgress progress = writer_->progress();
        const std::uint32_t total = selected_ ? selected_->slotCount : 0U;
        progress_ = QStringLiteral("已验证 %1/%2 槽").arg(progress.verifiedSlots).arg(total);
    } else if (!readRunning_ && !writeRunning_) {
        progress_ = QStringLiteral("空闲");
    }
    progressLabel_->setText(progress_);
}

void RegionWorkspace::setReadRunning(const bool running) {
    readRunning_ = running;
    setTransferRunning(running || writeRunning_);
}

void RegionWorkspace::setWriteRunning(const bool running) {
    writeRunning_ = running;
    setTransferRunning(running || readRunning_);
}

void RegionWorkspace::setTransferRunning(const bool running) {
    regionBox_->setEnabled(!running);
    fieldBox_->setEnabled(!running);
    readButton_->setEnabled(!running);
    importButton_->setEnabled(!running);
    exportButton_->setEnabled(!running);
    cancelButton_->setEnabled(running);
    writeButton_->setEnabled(!running && preview_.has_value() && preview_->commitPermitted);
    if (running) {
        progressBar_->setVisible(true);
    }
}

}  // namespace ernc::presentation
