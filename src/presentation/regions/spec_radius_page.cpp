#include "regions/spec_radius_page.hpp"

#include "ernc/domain/ernc_tf_codec.hpp"

#include <QLabel>
#include <QString>
#include <QTableView>
#include <QVBoxLayout>

#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ernc::presentation {
namespace {

constexpr QStringView kBlank = u"—";
constexpr std::size_t kPointCount = 120;
constexpr std::size_t kByteCount = kPointCount * 4U;

/// @brief 这块区域是不是本页接受的谱半径。
/// @param region 区域定义，只在本次调用期间借用。
/// @return 路径、120 点、480 字节和 float 都符合时为 true。
bool accepts(const ernc::domain::RegionDefinition& region) {
    return region.path == "ERNC_TF.SpecRadius" && region.shape == std::vector<std::uint32_t>{kPointCount}
        && region.slotCount == kPointCount && region.size == kByteCount && region.scalarType
        && *region.scalarType == ernc::domain::DataType::Float32;
}

/// @brief 把区域定义收成解码器要的子字段。
/// @param region 已接受的谱半径，只在本次调用期间借用。
/// @return 调用方拥有的子字段。路径仍是 ERNC_TF.SpecRadius。
ernc::domain::SubRegion asField(const ernc::domain::RegionDefinition& region) {
    ernc::domain::SubRegion field;
    field.path = region.path;
    field.slot = region.slot;
    field.slotCount = region.slotCount;
    field.size = region.size;
    field.offset = region.offset;
    field.typeName = region.typeName;
    field.scalarType = region.scalarType;
    field.shape = region.shape;
    field.evidence = region.evidence;
    field.writable = region.writable;
    return field;
}

/// @brief 显示一个有限 float。
/// @param value 标量，只在本次调用期间借用。
/// @return 十进制文本。不是有限 float 时为「—」。
QString formatFloat(const ernc::domain::ScalarValue& value) {
    const ernc::domain::Result<float> number = value.asFloat();
    if (!number.ok() || !std::isfinite(number.value())) {
        return kBlank.toString();
    }
    return QString::number(static_cast<double>(number.value()), 'g', 9);
}

/// @brief 把文本解析成有限 float。
/// @param text 用户输入。
/// @return 有限值。空文本、非数字或非有限值时为空。
std::optional<ernc::domain::ScalarValue> parseFiniteFloat(const QString& text) {
    bool ok = false;
    const double number = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return std::nullopt;
    }
    return ernc::domain::ScalarValue::fromFloat(static_cast<float>(number));
}

/// @brief 追加一个小端 float。
/// @param bytes 输出缓冲。函数在尾部追加 4 字节。
/// @param value 有限 float。下标 0 是最低字节。
void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xFFU));
}

}  // namespace

SpecRadiusModel::SpecRadiusModel(ernc::domain::ArrayState* state, QObject* parent)
    : QAbstractTableModel(parent), state_{state} {}

int SpecRadiusModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || state_ == nullptr || state_->size() != kPointCount || state_->kind() != ernc::domain::ScalarValue::Kind::Float32) {
        return 0;
    }
    return static_cast<int>(kPointCount);
}

int SpecRadiusModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant SpecRadiusModel::data(const QModelIndex& index, int role) const {
    if (rowCount() == 0 || !index.isValid()) {
        return {};
    }
    const bool show = role == Qt::DisplayRole || (role == Qt::EditRole && index.column() == static_cast<int>(SpecRadiusColumn::Set));
    if (!show) {
        return {};
    }
    if (index.column() == static_cast<int>(SpecRadiusColumn::Frequency)) {
        return QString::number(index.row());
    }
    const ernc::domain::ArrayElement* element = state_->element(static_cast<std::size_t>(index.row()));
    if (element == nullptr) {
        return {};
    }
    if (index.column() == static_cast<int>(SpecRadiusColumn::Read)) {
        return element->read ? formatFloat(*element->read) : kBlank.toString();
    }
    if (index.column() == static_cast<int>(SpecRadiusColumn::Set)) {
        return element->set ? formatFloat(*element->set) : kBlank.toString();
    }
    if (index.column() == static_cast<int>(SpecRadiusColumn::Dirty)) {
        return element->dirty ? QStringLiteral("是") : QStringLiteral("否");
    }
    return {};
}

QVariant SpecRadiusModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case static_cast<int>(SpecRadiusColumn::Frequency):
        return QStringLiteral("频率");
    case static_cast<int>(SpecRadiusColumn::Read):
        return QStringLiteral("Read");
    case static_cast<int>(SpecRadiusColumn::Set):
        return QStringLiteral("Set");
    case static_cast<int>(SpecRadiusColumn::Dirty):
        return QStringLiteral("Dirty");
    default:
        return {};
    }
}

Qt::ItemFlags SpecRadiusModel::flags(const QModelIndex& index) const {
    if (!index.isValid() || rowCount() == 0) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == static_cast<int>(SpecRadiusColumn::Set)) {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}

bool SpecRadiusModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (state_ == nullptr || role != Qt::EditRole || !index.isValid() || index.column() != static_cast<int>(SpecRadiusColumn::Set)) {
        return false;
    }
    const std::optional<ernc::domain::ScalarValue> parsed = parseFiniteFloat(value.toString());
    if (!parsed || !state_->setValue(static_cast<std::size_t>(index.row()), *parsed).ok()) {
        return false;
    }
    const QModelIndex dirty = this->index(index.row(), static_cast<int>(SpecRadiusColumn::Dirty));
    emit dataChanged(index, dirty, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

void SpecRadiusModel::resetSets() {
    if (state_ == nullptr) {
        return;
    }
    state_->reset();
    if (rowCount() == 0) {
        return;
    }
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::DisplayRole, Qt::EditRole});
}

void SpecRadiusModel::notifyReads() {
    if (rowCount() == 0) {
        return;
    }
    const QModelIndex first = index(0, static_cast<int>(SpecRadiusColumn::Read));
    const QModelIndex last = index(rowCount() - 1, static_cast<int>(SpecRadiusColumn::Read));
    emit dataChanged(first, last, {Qt::DisplayRole});
}

SpecRadiusPage::SpecRadiusPage(const ernc::domain::RegionDefinition& region, QWidget* parent)
    : QWidget(parent)
    , region_(region)
    , accepted_(accepts(region))
    , state_(accepted_ ? kPointCount : 0U, ernc::domain::ScalarValue::Kind::Float32)
    , model_(new SpecRadiusModel(&state_, this)) {
    auto* layout = new QVBoxLayout(this);
    auto* note = new QLabel(accepted_ ? QStringLiteral("频率列是下标 0–119，不是赫兹。")
                                      : QStringLiteral("这块区域不是 120 点的 ERNC_TF.SpecRadius。"),
                            this);
    auto* table = new QTableView(this);
    table->setModel(model_);
    layout->addWidget(note);
    layout->addWidget(table, 1);
}

bool SpecRadiusPage::acceptsRegion() const noexcept {
    return accepted_;
}

SpecRadiusModel* SpecRadiusPage::model() const noexcept {
    return model_;
}

bool SpecRadiusPage::loadReadBytes(std::span<const std::uint8_t> bytes) {
    if (!accepted_ || bytes.size() != kByteCount) {
        return false;
    }
    const ernc::domain::Result<ernc::domain::DecodedField> decoded = ernc::domain::decodeErncTfField(asField(region_), bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr || decoded.value().values.size() != kPointCount) {
        return false;
    }
    for (std::size_t index = 0; index < kPointCount; ++index) {
        if (!state_.setRead(index, ernc::domain::ScalarValue::fromFloat(decoded.value().values[index])).ok()) {
            return false;
        }
    }
    model_->notifyReads();
    return true;
}

void SpecRadiusPage::resetSets() {
    model_->resetSets();
}

ernc::domain::Result<std::vector<std::uint8_t>> SpecRadiusPage::setBytes() const {
    if (!accepted_) {
        return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kByteCount);
    for (std::size_t index = 0; index < kPointCount; ++index) {
        const ernc::domain::ArrayElement* element = state_.element(index);
        if (element == nullptr || !element->set) {
            return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        const ernc::domain::Result<float> number = element->set->asFloat();
        if (!number.ok() || !std::isfinite(number.value())) {
            return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        appendFloat(bytes, number.value());
    }
    return ernc::domain::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

ernc::domain::Result<ernc::application::RegionWritePreview> SpecRadiusPage::previewWrite(ernc::application::RegionWriter& writer) const {
    const ernc::domain::Result<std::vector<std::uint8_t>> bytes = setBytes();
    if (!bytes.ok() || bytes.tryValue() == nullptr) {
        return ernc::domain::Result<ernc::application::RegionWritePreview>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return writer.preview(region_, *bytes.tryValue());
}

}  // namespace ernc::presentation
