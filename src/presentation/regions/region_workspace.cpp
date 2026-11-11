#include "regions/region_workspace.hpp"

#include "ernc/infrastructure/legacy_region_codec.hpp"
#include "regions/spec_radius_page.hpp"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <bit>
#include <cstdint>

namespace ernc::presentation {
namespace {

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
    layout->addWidget(regionBox_);
    layout->addWidget(fieldBox_);
    layout->addWidget(statusLabel_);
    layout->addWidget(progressLabel_);
    auto* readButton = new QPushButton(QStringLiteral("读取"), this);
    auto* importButton = new QPushButton(QStringLiteral("导入"), this);
    auto* previewButton = new QPushButton(QStringLiteral("预览"), this);
    auto* exportButton = new QPushButton(QStringLiteral("导出"), this);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    writeButton_->setEnabled(false);
    layout->addWidget(readButton);
    layout->addWidget(importButton);
    layout->addWidget(previewButton);
    layout->addWidget(writeButton_);
    layout->addWidget(exportButton);
    layout->addWidget(cancelButton);
    layout->addWidget(stack_, 1);
    QObject::connect(regionBox_, &QComboBox::currentTextChanged, this, [this](const QString& path) { selectRegion(path); });
    QObject::connect(fieldBox_, &QComboBox::currentTextChanged, this, [this](const QString& path) {
        selectField(path == QStringLiteral("全部") ? QString() : path);
    });
    QObject::connect(readButton, &QPushButton::clicked, this, [this]() { readSelected(); });
    QObject::connect(previewButton, &QPushButton::clicked, this, [this]() { previewSelected(); });
    QObject::connect(writeButton_, &QPushButton::clicked, this, [this]() { writeSelected(); });
    QObject::connect(cancelButton, &QPushButton::clicked, this, [this]() { cancelTransfer(); });
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
    if (region == nullptr || reader_ == nullptr) {
        status_ = QStringLiteral("读取被拒绝");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<void> started = reader_->start(*region);
    if (!started.ok()) {
        status_ = QStringLiteral("读取被拒绝");
    } else {
        status_ = QStringLiteral("读取已入队");
    }
    statusLabel_->setText(status_);
    refreshProgress();
    return started;
}

ernc::domain::Result<void> RegionWorkspace::importBin(const std::filesystem::path& source) {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    if (region == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<std::vector<std::uint8_t>> loaded = ernc::infrastructure::readRegionBin(*region, source);
    if (!loaded.ok() || loaded.tryValue() == nullptr) {
        status_ = QStringLiteral("导入失败");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(loaded.ok() ? ernc::domain::ErrorCode::Unknown : loaded.error());
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
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> RegionWorkspace::importTxt(const std::filesystem::path& source) {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    if (region == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<std::vector<float>> loaded = ernc::infrastructure::readRegionTxt(*region, source);
    if (!loaded.ok() || loaded.tryValue() == nullptr) {
        status_ = QStringLiteral("导入失败");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(loaded.ok() ? ernc::domain::ErrorCode::Unknown : loaded.error());
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
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<ernc::application::RegionWritePreview> RegionWorkspace::previewSelected() {
    const ernc::domain::RegionDefinition* region = selectedRegion();
    const std::optional<std::vector<std::uint8_t>> bytes = currentBytes();
    if (region == nullptr || writer_ == nullptr || !bytes) {
        status_ = QStringLiteral("预览失败");
        statusLabel_->setText(status_);
        writeButton_->setEnabled(false);
        return ernc::domain::Result<ernc::application::RegionWritePreview>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    ernc::domain::Result<ernc::application::RegionWritePreview> preview = writer_->preview(*region, *bytes);
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
    if (region == nullptr || writer_ == nullptr || !preview_ || !previewBytes_ || !bytes || *bytes != *previewBytes_) {
        status_ = QStringLiteral("写入被拒绝");
        statusLabel_->setText(status_);
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<void> started = writer_->start(*region, *previewBytes_);
    status_ = started.ok() ? QStringLiteral("写入已入队") : QStringLiteral("写入被拒绝");
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

ernc::domain::Result<void> RegionWorkspace::exportCancelled(const ernc::application::RegionTransferRecord& record,
                                                           const std::filesystem::path&) {
    if (!record.canSaveAsCompleteBin()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
}

void RegionWorkspace::cancelTransfer() {
    if (reader_ != nullptr) {
        reader_->cancel();
    }
    if (writer_ != nullptr) {
        writer_->cancel();
    }
    status_ = QStringLiteral("已取消");
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
        progress_ = QStringLiteral("已接收 %1/%2 slots").arg(progress.filledBytes / 4U).arg(progress.plannedBytes / 4U);
    } else if (writer_ != nullptr && writer_->busy()) {
        const ernc::application::RegionWriteProgress progress = writer_->progress();
        const std::uint32_t total = selected_ ? selected_->slotCount : 0U;
        progress_ = QStringLiteral("已验证 %1/%2 slots").arg(progress.verifiedSlots).arg(total);
    } else {
        progress_ = QStringLiteral("空闲");
    }
    progressLabel_->setText(progress_);
}

}  // namespace ernc::presentation
