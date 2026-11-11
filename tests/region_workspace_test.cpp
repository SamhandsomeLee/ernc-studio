#include "regions/region_workspace.hpp"

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <QApplication>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::RegionReader;
using ernc::application::RegionTransferRecord;
using ernc::application::RegionWriter;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::RegionCatalog;
using ernc::domain::Result;
using ernc::ports::IDeviceTransport;
using ernc::presentation::RegionWorkspace;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 用完就删掉的临时目录。
struct TempDir {
    /// 目录路径。
    std::filesystem::path dir;

    /// @brief 创建空目录。
    TempDir() {
        std::random_device device;
        dir = std::filesystem::temp_directory_path() / ("ernc-es076-" + std::to_string(device()));
        std::filesystem::create_directories(dir);
    }

    /// @brief 删除目录。
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

/// @brief 记录提交帧的端口。
class RecordingTransport final : public IDeviceTransport {
public:
    /// @brief 绑定从站内存。
    /// @param image 从站内存，只借用。
    explicit RecordingTransport(SlaveImage& image) : inner_{image} {}

    /// @brief 打开内部端口。
    /// @return 内部端口的打开结果。
    ernc::domain::Result<void> open() override { return inner_.open(); }

    /// @brief 关闭内部端口。
    /// @return 内部端口的关闭结果。
    ernc::domain::Result<void> close() override { return inner_.close(); }

    /// @brief 记下整帧后再交给从站。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 内部端口的提交结果。
    ernc::domain::Result<void> submit(std::span<const std::uint8_t> bytes) override {
        submitted_.emplace_back(bytes.begin(), bytes.end());
        return inner_.submit(bytes);
    }

    /// @brief 取走从站的下一步。
    /// @return 内部端口的步骤。
    ernc::ports::TransportStep poll() override { return inner_.poll(); }

    /// @brief 已提交的帧数。
    /// @return 帧数。没有入队时为 0。
    std::size_t submittedCount() const noexcept { return submitted_.size(); }

private:
    /// 实际从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 载入正式区域目录。
/// @param manifestPath 布局文件。
/// @param name 失败时打印的用例名。
/// @return 成功时为目录。失败时已经打印用例名。
Result<RegionCatalog> openCatalog(const char* manifestPath, const char* name) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        fail(name);
        return Result<RegionCatalog>::failure(ErrorCode::InvalidParameter);
    }
    Result<RegionCatalog> built = RegionCatalog::load(loaded.layout());
    if (!built.ok()) {
        fail(name);
    }
    return built;
}

/// @brief 选中谱半径后显示 120 行。
/// @param manifestPath 正式 manifest。
/// @return 行数是 120 时为 true。
bool specRadiusShows120Rows(const char* manifestPath) {
    constexpr const char* name = "spec_radius_shows_120_rows";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    SlaveImage image{std::vector<std::uint8_t>(4U, 0)};
    RecordingTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    RegionReader reader{session, scheduler};
    RegionWriter writer{session, scheduler};
    RegionWorkspace workspace(catalog.value(), &reader, &writer);
    if (!workspace.selectRegion(QStringLiteral("ERNC_TF.SpecRadius")) || !workspace.specRadiusVisible() || workspace.specRadiusRows() != 120) {
        return fail(name);
    }
    if (!workspace.selectRegion(QStringLiteral("ERNC_TF")) || !workspace.selectField(QStringLiteral("ERNC_TF.SpecRadius"))
        || workspace.specRadiusRows() != 120) {
        return fail(name);
    }
    return true;
}

/// @brief 只导入时不发出写请求。
/// @param manifestPath 正式 manifest。
/// @return 状态是已导入尚未写入，写入按钮不可用，提交数为 0 时为 true。
bool importDoesNotWrite(const char* manifestPath) {
    constexpr const char* name = "import_does_not_write";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    TempDir temp;
    const std::filesystem::path source = temp.dir / "radius.bin";
    {
        std::ofstream output(source, std::ios::binary | std::ios::trunc);
        const std::vector<char> bytes(480, 0);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            return fail(name);
        }
    }
    SlaveImage image{std::vector<std::uint8_t>(4U, 0)};
    RecordingTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{session, scheduler};
    RegionWriter writer{session, scheduler};
    RegionWorkspace workspace(catalog.value(), &reader, &writer);
    if (!workspace.selectRegion(QStringLiteral("ERNC_TF.SpecRadius"))) {
        return fail(name);
    }
    const Result<void> imported = workspace.importBin(source);
    if (!imported.ok() || workspace.statusText() != QStringLiteral("已导入，尚未写入") || workspace.writeActionEnabled()
        || transport.submittedCount() != 0U || workspace.progressText() != QStringLiteral("空闲")) {
        return fail(name);
    }
    return true;
}

/// @brief 预览之后才能写入，正式区域仍然不入队。
/// @param manifestPath 正式 manifest。
/// @return 预览前写入被拒绝，预览后写入被拒绝且提交数仍为 0 时为 true。
bool previewThenWriteDoesNotEnqueue(const char* manifestPath) {
    constexpr const char* name = "preview_then_write_does_not_enqueue";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    TempDir temp;
    const std::filesystem::path source = temp.dir / "radius.bin";
    {
        std::ofstream output(source, std::ios::binary | std::ios::trunc);
        const std::vector<char> bytes(480, 0);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            return fail(name);
        }
    }
    SlaveImage image{std::vector<std::uint8_t>(4U, 0)};
    RecordingTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{session, scheduler};
    RegionWriter writer{session, scheduler};
    RegionWorkspace workspace(catalog.value(), &reader, &writer);
    if (!workspace.selectRegion(QStringLiteral("ERNC_TF.SpecRadius")) || !workspace.importBin(source).ok()) {
        return fail(name);
    }
    const Result<void> early = workspace.writeSelected();
    if (early.ok() || workspace.writeActionEnabled()) {
        return fail(name);
    }
    const auto preview = workspace.previewSelected();
    const Result<void> started = workspace.writeSelected();
    if (!preview.ok() || preview.tryValue() == nullptr || preview.value().commitPermitted || !workspace.writeActionEnabled() || started.ok()
        || started.tryError() == nullptr || *started.tryError() != ErrorCode::InvalidParameter || transport.submittedCount() != 0U
        || workspace.progressText() != QStringLiteral("空闲")) {
        return fail(name);
    }
    return true;
}

/// @brief 取消记录不能导出成 BIN。
/// @return 不创建目标文件时为 true。
bool cancelledRecordIsNotExported() {
    constexpr const char* name = "cancelled_record_is_not_exported";
    TempDir temp;
    const std::filesystem::path destination = temp.dir / "partial.bin";
    RegionTransferRecord record;
    record.prefix = {0x00, 0x00, 0x80, 0x3F};
    record.completedSlots = 1;
    SlaveImage image{std::vector<std::uint8_t>(4U, 0)};
    RecordingTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    RegionReader reader{session, scheduler};
    RegionWriter writer{session, scheduler};
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(
        R"({"alignment":4,"base":0,"total_size":16,"total_slots":4,"fields":[{"path":"ERNC_TF","offset":0,"size":8,"slot":0,"slot_count":2,"type":"ANC_TransFunc_f","shape":[],"status":"pack4-sequential","writable":false,"children":[{"path":"ERNC_TF.SpecRadius","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[1],"status":"pack4-sequential","writable":false},{"path":"ERNC_TF.ENCTF","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[1],"status":"pack4-sequential","writable":false}]},{"path":"check_data","offset":8,"size":8,"slot":2,"slot_count":2,"type":"Check_data","shape":[],"status":"pack4-sequential","writable":false,"children":[{"path":"check_data.OutputCheck","offset":8,"size":8,"slot":2,"slot_count":2,"type":"float","shape":[2],"status":"pack4-sequential","writable":false}]}]})");
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (!catalog.ok()) {
        return fail(name);
    }
    RegionWorkspace workspace(catalog.value(), &reader, &writer);
    const Result<void> exported = workspace.exportCancelled(record, destination);
    if (record.canSaveAsCompleteBin() || exported.ok() || std::filesystem::exists(destination) || transport.submittedCount() != 0U) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。取消导出用例不读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "spec_radius_shows_120_rows") {
        return specRadiusShows120Rows(manifestPath);
    }
    if (name == "import_does_not_write") {
        return importDoesNotWrite(manifestPath);
    }
    if (name == "preview_then_write_does_not_enqueue") {
        return previewThenWriteDoesNotEnqueue(manifestPath);
    }
    if (name == "cancelled_record_is_not_exported") {
        return cancelledRecordIsNotExported();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_region_workspace_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    const std::string manifest = argv[3];
    QApplication app(argc, argv);
    return runCase(caseName, manifest.c_str()) ? 0 : 1;
}
