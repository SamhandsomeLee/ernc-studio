#include "regions/spec_radius_page.hpp"

#include "ernc/application/region_writer.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <QApplication>
#include <QModelIndex>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::RegionWritePreview;
using ernc::application::RegionWriter;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::RegionCatalog;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;
using ernc::presentation::SpecRadiusColumn;
using ernc::presentation::SpecRadiusModel;
using ernc::presentation::SpecRadiusPage;
using ernc::ports::IDeviceTransport;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 从正式布局取出谱半径目录。
/// @param manifestPath 布局文件。
/// @param name 失败时打印的用例名。
/// @return 目录成功时为成功结果。失败时已经打印用例名。
Result<RegionCatalog> openCatalog(const char* manifestPath, const char* name) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        fail(name);
        return Result<RegionCatalog>::failure(ErrorCode::InvalidParameter);
    }
    Result<RegionCatalog> built = RegionCatalog::load(loaded.layout());
    if (!built.ok() || built.tryValue() == nullptr || built.value().find("ERNC_TF.SpecRadius") == nullptr) {
        fail(name);
        if (built.ok()) {
            return Result<RegionCatalog>::failure(ErrorCode::InvalidParameter);
        }
    }
    return built;
}

/// @brief 120 行里只有 Set 列能编辑，频率列是下标。
/// @param manifestPath 正式 manifest。
/// @return 第 0 行频率是 0、第 119 行是 119，Read 和 Dirty 不可编辑时为 true。
bool rowsOnlySetEditable(const char* manifestPath) {
    constexpr const char* name = "rows_only_set_editable";
    const Result<RegionCatalog> opened = openCatalog(manifestPath, name);
    if (!opened.ok()) {
        return false;
    }
    const RegionDefinition* radius = opened.value().find("ERNC_TF.SpecRadius");
    if (radius == nullptr) {
        return false;
    }
    SpecRadiusPage page(*radius);
    SpecRadiusModel* model = page.model();
    if (!page.acceptsRegion() || model == nullptr || model->rowCount() != 120 || model->columnCount() != 4) {
        return fail(name);
    }
    const QModelIndex frequency = model->index(0, static_cast<int>(SpecRadiusColumn::Frequency));
    const QModelIndex lastFrequency = model->index(119, static_cast<int>(SpecRadiusColumn::Frequency));
    const QModelIndex read = model->index(0, static_cast<int>(SpecRadiusColumn::Read));
    const QModelIndex set = model->index(2, static_cast<int>(SpecRadiusColumn::Set));
    const QModelIndex dirty = model->index(2, static_cast<int>(SpecRadiusColumn::Dirty));
    if (model->headerData(0, Qt::Horizontal).toString() != QStringLiteral("频率") || frequency.data().toString() != QStringLiteral("0")
        || lastFrequency.data().toString() != QStringLiteral("119") || model->flags(frequency).testFlag(Qt::ItemIsEditable)
        || model->flags(read).testFlag(Qt::ItemIsEditable) || !model->flags(set).testFlag(Qt::ItemIsEditable)
        || model->flags(dirty).testFlag(Qt::ItemIsEditable) || model->setData(read, QStringLiteral("9"))
        || !model->setData(set, QStringLiteral("1.5")) || set.data().toString() != QStringLiteral("1.5")
        || read.data().toString() != QStringLiteral("—") || dirty.data().toString() != QStringLiteral("是")
        || model->index(0, static_cast<int>(SpecRadiusColumn::Dirty)).data().toString() != QStringLiteral("否")) {
        return fail(name);
    }
    return true;
}

/// @brief 读入 480 字节只改 Read。
/// @param manifestPath 正式 manifest。
/// @return 先前的 Set 仍是 4，Read 变成 1，Reset 后 Set 回到 1 时为 true。
bool loadReadKeepsSet(const char* manifestPath) {
    constexpr const char* name = "load_read_keeps_set";
    const Result<RegionCatalog> opened = openCatalog(manifestPath, name);
    if (!opened.ok()) {
        return false;
    }
    const RegionDefinition* radius = opened.value().find("ERNC_TF.SpecRadius");
    if (radius == nullptr) {
        return false;
    }
    SpecRadiusPage page(*radius);
    SpecRadiusModel* model = page.model();
    const QModelIndex set = model->index(0, static_cast<int>(SpecRadiusColumn::Set));
    if (!model->setData(set, QStringLiteral("4"))) {
        return fail(name);
    }
    std::vector<std::uint8_t> bytes(480, 0);
    bytes[0] = 0x00;
    bytes[1] = 0x00;
    bytes[2] = 0x80;
    bytes[3] = 0x3F;
    if (!page.loadReadBytes(bytes)) {
        return fail(name);
    }
    const QModelIndex read = model->index(0, static_cast<int>(SpecRadiusColumn::Read));
    const QModelIndex dirty = model->index(0, static_cast<int>(SpecRadiusColumn::Dirty));
    if (read.data().toString() != QStringLiteral("1") || set.data().toString() != QStringLiteral("4") || dirty.data().toString() != QStringLiteral("是")) {
        return fail(name);
    }
    page.resetSets();
    if (set.data().toString() != QStringLiteral("1") || dirty.data().toString() != QStringLiteral("否")) {
        return fail(name);
    }
    return true;
}

/// @brief 少一个字节时不改已经显示的 Read。
/// @param manifestPath 正式 manifest。
/// @return loadReadBytes 为 false，且 Read 仍是 1 时为 true。
bool shortBytesRejected(const char* manifestPath) {
    constexpr const char* name = "short_bytes_rejected";
    const Result<RegionCatalog> opened = openCatalog(manifestPath, name);
    if (!opened.ok()) {
        return false;
    }
    const RegionDefinition* radius = opened.value().find("ERNC_TF.SpecRadius");
    if (radius == nullptr) {
        return false;
    }
    SpecRadiusPage page(*radius);
    std::vector<std::uint8_t> bytes(480, 0);
    bytes[2] = 0x80;
    bytes[3] = 0x3F;
    if (!page.loadReadBytes(bytes)) {
        return fail(name);
    }
    std::vector<std::uint8_t> shortBytes(479, 0);
    if (page.loadReadBytes(shortBytes) || page.model()->index(0, static_cast<int>(SpecRadiusColumn::Read)).data().toString() != QStringLiteral("1")) {
        return fail(name);
    }
    return true;
}

/// @brief 非有限 Set 被拒绝。
/// @param manifestPath 正式 manifest。
/// @return setData 为 false，Dirty 仍是否时为 true。
bool nanSetRejected(const char* manifestPath) {
    constexpr const char* name = "nan_set_rejected";
    const Result<RegionCatalog> opened = openCatalog(manifestPath, name);
    if (!opened.ok()) {
        return false;
    }
    const RegionDefinition* radius = opened.value().find("ERNC_TF.SpecRadius");
    if (radius == nullptr) {
        return false;
    }
    SpecRadiusPage page(*radius);
    const QModelIndex set = page.model()->index(1, static_cast<int>(SpecRadiusColumn::Set));
    if (page.model()->setData(set, QStringLiteral("nan"))
        || page.model()->index(1, static_cast<int>(SpecRadiusColumn::Dirty)).data().toString() != QStringLiteral("否")) {
        return fail(name);
    }
    return true;
}

/// @brief 正式谱半径可以预览并允许基线写入。
/// @param manifestPath 正式 manifest。
/// @return 两包各 60 个 slot，commitPermitted 为 true，且没有请求时为 true。
bool previewRealRegionCommits(const char* manifestPath) {
    constexpr const char* name = "preview_real_region_commits";
    const Result<RegionCatalog> opened = openCatalog(manifestPath, name);
    if (!opened.ok()) {
        return false;
    }
    const RegionDefinition* radius = opened.value().find("ERNC_TF.SpecRadius");
    if (radius == nullptr) {
        return false;
    }
    SpecRadiusPage page(*radius);
    for (int row = 0; row < 120; ++row) {
        if (!page.model()->setData(page.model()->index(row, static_cast<int>(SpecRadiusColumn::Set)), QStringLiteral("1"))) {
            return fail(name);
        }
    }
    const Result<std::vector<std::uint8_t>> bytes = page.setBytes();
    if (!bytes.ok() || bytes.tryValue() == nullptr || bytes.value().size() != 480U || bytes.value()[0] != 0x00 || bytes.value()[3] != 0x3F) {
        return fail(name);
    }
    SlaveImage image{std::vector<std::uint8_t>(4U, 0)};
    MemorySlaveTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{session, scheduler};
    const Result<RegionWritePreview> preview = page.previewWrite(writer);
    if (!preview.ok() || preview.tryValue() == nullptr || !preview.value().commitPermitted || preview.value().chunks.size() != 2U
        || preview.value().chunks[0].slotCount != 60U || preview.value().chunks[1].slotCount != 60U || writer.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief shape 不是 120 时没有可编辑行。
/// @return 行数为 0，且预览失败时为 true。
bool wrongShapeHasNoRows() {
    constexpr const char* name = "wrong_shape_has_no_rows";
    RegionDefinition region;
    region.path = "ERNC_TF.SpecRadius";
    region.slotCount = 10;
    region.size = 40;
    region.shape = {10};
    region.scalarType = ernc::domain::DataType::Float32;
    SpecRadiusPage page(region);
    SlaveImage image{std::vector<std::uint8_t>(4U, 0)};
    MemorySlaveTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    RegionWriter writer{session, scheduler};
    const Result<RegionWritePreview> preview = page.previewWrite(writer);
    if (page.acceptsRegion() || page.model()->rowCount() != 0 || preview.ok() || preview.tryError() == nullptr
        || *preview.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。wrong_shape 不读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "rows_only_set_editable") {
        return rowsOnlySetEditable(manifestPath);
    }
    if (name == "load_read_keeps_set") {
        return loadReadKeepsSet(manifestPath);
    }
    if (name == "short_bytes_rejected") {
        return shortBytesRejected(manifestPath);
    }
    if (name == "nan_set_rejected") {
        return nanSetRejected(manifestPath);
    }
    if (name == "preview_real_region_commits") {
        return previewRealRegionDoesNotCommit(manifestPath);
    }
    if (name == "wrong_shape_has_no_rows") {
        return wrongShapeHasNoRows();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_spec_radius_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    const std::string manifest = argv[3];
    QApplication app(argc, argv);
    return runCase(caseName, manifest.c_str()) ? 0 : 1;
}
