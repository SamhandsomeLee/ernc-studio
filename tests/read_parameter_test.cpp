#include "ernc/application/read_parameter.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ParameterReader;
using ernc::application::ReadCommandResult;
using ernc::application::ReadTicket;
using ernc::domain::CatalogLoad;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::SessionId;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// Can_Msg 在正式 manifest 中的起始 slot。
constexpr std::uint16_t kCanMsgSlot = 14869;

/// Can_Msg 的 float 元素个数。
constexpr std::uint16_t kCanMsgSlots = 72;

/// 覆盖到 Can_Msg 末尾的镜像 slot 数。14929 + 12 = 14941。
constexpr std::size_t kImageSlots = 14941;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 记录提交帧并转交给内存从站的端口。
///
/// 持有内部从站端口。本对象和 DeviceSession 使用同一条对外端口。
class RecordingTransport final : public IDeviceTransport {
public:
    /// @brief 绑定从站内存。初始为关闭。
    /// @param image 从站内存，只借用。调用方保证它在本对象存活期间有效。
    explicit RecordingTransport(SlaveImage& image) : inner_{image} {}

    /// @brief 打开内部端口。
    /// @return 内部端口的打开结果。
    Result<void> open() override { return inner_.open(); }

    /// @brief 关闭内部端口。
    /// @return 内部端口的关闭结果。
    Result<void> close() override { return inner_.close(); }

    /// @brief 记下整帧后再交给从站。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。函数保存自己的副本。
    /// @return 内部端口的提交结果。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        submitted_.emplace_back(bytes.begin(), bytes.end());
        return inner_.submit(bytes);
    }

    /// @brief 取走从站的下一步。
    /// @return 内部端口的步骤。
    TransportStep poll() override { return inner_.poll(); }

    /// @brief 按提交顺序借出已记录的请求。
    /// @return 内部向量的常量引用。
    const std::vector<std::vector<std::uint8_t>>& submitted() const noexcept { return submitted_; }

private:
    /// 实际回答 FC03 的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 已连接的读路径。镜像、端口、会话、调度器和读对象的引用都指向本对象的成员。
struct Bench {
    /// 从站镜像。slot 0 在字节 0。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// 运行状态。
    ParameterRuntimeStore store;

    /// 读用例。
    ParameterReader reader;

    /// @brief 用已载入的目录和布局组装一条读路径。
    /// @param catalog 参数目录，只借用。必须比本对象活得久。
    /// @param layout 固件布局，只借用。必须比本对象活得久。
    /// @param bytes 从站初始字节。长度必须是 4 的倍数。
    Bench(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout, std::vector<std::uint8_t> bytes)
        : image{std::move(bytes)}
        , transport{image}
        , session{transport}
        , scheduler{session, transport}
        , reader{catalog, layout, store, session, scheduler} {}
};

/// @brief 做成覆盖 Can_Msg 的空白镜像。
/// @return 14941 个 slot 的零字节。调用方拥有。
std::vector<std::uint8_t> emptyImage() {
    return std::vector<std::uint8_t>(kImageSlots * 4U, 0);
}

/// @brief 把一个 32 位模式写成低字节在前的 4 字节。
/// @param bits 位模式。不调用协议编码器。
/// @param out 至少 4 字节的输出，只在本次调用期间借用。下标 0 接收最低字节。
void storeLittleEndian(std::uint32_t bits, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>(bits & 0xFFU);
    out[1] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    out[2] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
    out[3] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
}

/// @brief 取出 float 的 IEEE-754 位模式。
/// @param value 要查看的 float。
/// @return 与 value 相同的 32 位模式。
std::uint32_t bitsOf(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

/// @brief 解析一个参数 ID。
/// @param text 标识文本。
/// @param name 用例名。
/// @param id 成功时写入解析结果。
/// @return 文本非空且解析成功时为 true。
bool parseId(std::string_view text, const char* name, ParameterId& id) {
    const Result<ParameterId> parsed = ParameterId::parse(text);
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail(name);
    }
    id = *parsed.tryValue();
    return true;
}

/// @brief 把调度器里已经结束的命令送进读对象，直到这笔读结束或次数用尽。
/// @param bench 读路径。
/// @param ticket 这笔读的命令号。
/// @param readAt 成功时记入 Store 的时刻。
/// @param name 用例名。
/// @return 读对象不再忙碌时为 true。
bool drive(Bench& bench, const ReadTicket& ticket, RuntimeTime readAt, const char* name) {
    for (int turn = 0; turn < 8 && bench.reader.busy(); ++turn) {
        for (CommandId id : ticket.commands()) {
            if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(id)) {
                bench.reader.apply(*outcome, readAt);
            }
        }
        bench.scheduler.pump();
    }
    for (CommandId id : ticket.commands()) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(id)) {
            bench.reader.apply(*outcome, readAt);
        }
    }
    if (bench.reader.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief 确认一帧是指定的 FC03 请求头。
/// @param frame 带长度前缀的请求。
/// @param b1 从站地址。
/// @param b2 功能码。
/// @param b3 起始寄存器高字节。
/// @param b4 起始寄存器低字节。
/// @param b5 寄存器数量高字节。
/// @param b6 寄存器数量低字节。
/// @return 长度足够且这 6 个字节匹配时为 true。
bool headerIs(const std::vector<std::uint8_t>& frame, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3, std::uint8_t b4,
              std::uint8_t b5, std::uint8_t b6) {
    return frame.size() >= 7U && frame[1] == b1 && frame[2] == b2 && frame[3] == b3 && frame[4] == b4 && frame[5] == b5
           && frame[6] == b6;
}

/// @brief 确认提交记录里没有 FC16。
/// @param frames 已提交的请求。
/// @return 每一帧的功能码都不是 0x10 时为 true。短帧算失败。
bool noWriteFrames(const std::vector<std::vector<std::uint8_t>>& frames) {
    for (const std::vector<std::uint8_t>& frame : frames) {
        if (frame.size() < 3U || frame[2] == 0x10U) {
            return false;
        }
    }
    return true;
}

/// @brief ANC_OFF 读回独立字面量，并且不把 dirty 设为 true。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return deviceValue 的位模式是 0x12345678 时为 true。
bool ancOffObservesScalar(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    std::vector<std::uint8_t> bytes = emptyImage();
    bytes[0] = 0x78;
    bytes[1] = 0x56;
    bytes[2] = 0x34;
    bytes[3] = 0x12;
    Bench bench{catalog, layout, std::move(bytes)};
    if (!bench.session.connect().ok()) {
        return fail("anc_off_observes_scalar");
    }
    ParameterId id{ParameterId::parse(" ").value()};
    if (!parseId("ANC_OFF", "anc_off_observes_scalar", id)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(id);
    if (!started.ok() || started.tryValue() == nullptr || started.value().commands().size() != 1U) {
        return fail("anc_off_observes_scalar");
    }
    const RuntimeTime readAt{std::chrono::seconds{10}};
    if (!drive(bench, started.value(), readAt, "anc_off_observes_scalar")) {
        return false;
    }
    const ernc::domain::ParameterRuntimeState* state = bench.store.find(id);
    if (state == nullptr || !state->deviceValue || state->dirty || state->stale || state->readAt != readAt
        || state->stagedValue.has_value()) {
        return fail("anc_off_observes_scalar");
    }
    const Result<float> number = state->deviceValue->asFloat();
    if (!number.ok() || bitsOf(number.value()) != 0x12345678U) {
        return fail("anc_off_observes_scalar");
    }
    if (bench.transport.submitted().size() != 1U
        || !headerIs(bench.transport.submitted()[0], 0x01, 0x03, 0x00, 0x00, 0x00, 0x02)
        || !noWriteFrames(bench.transport.submitted())) {
        return fail("anc_off_observes_scalar");
    }
    return true;
}

/// @brief Can_Msg 分成两包 FC03，得到 72 个 float，并且不写入 deviceValue。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 元素位模式依次为 1 到 72，镜像字节保持不变时为 true。
bool canMsgReadsSeventyTwo(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    std::vector<std::uint8_t> bytes = emptyImage();
    for (std::uint16_t index = 0; index < kCanMsgSlots; ++index) {
        storeLittleEndian(static_cast<std::uint32_t>(index) + 1U, bytes.data() + (static_cast<std::size_t>(kCanMsgSlot + index) * 4U));
    }
    const std::vector<std::uint8_t> before = bytes;
    Bench bench{catalog, layout, std::move(bytes)};
    if (!bench.session.connect().ok()) {
        return fail("can_msg_reads_seventy_two");
    }
    ParameterId id{ParameterId::parse(" ").value()};
    if (!parseId("Can_Msg", "can_msg_reads_seventy_two", id)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(id);
    if (!started.ok() || started.tryValue() == nullptr || started.value().commands().size() != 2U) {
        return fail("can_msg_reads_seventy_two");
    }
    if (!drive(bench, started.value(), RuntimeTime{std::chrono::seconds{11}}, "can_msg_reads_seventy_two")) {
        return false;
    }
    const ernc::application::ParameterReadImage* image = bench.reader.image();
    if (image == nullptr || image->scalar || image->deviceScalar.has_value() || image->elements.size() != kCanMsgSlots
        || image->id != id || bench.store.find(id) != nullptr) {
        return fail("can_msg_reads_seventy_two");
    }
    for (std::uint16_t index = 0; index < kCanMsgSlots; ++index) {
        const Result<float> number = image->elements[index].asFloat();
        if (!number.ok() || bitsOf(number.value()) != static_cast<std::uint32_t>(index) + 1U) {
            return fail("can_msg_reads_seventy_two");
        }
    }
    const std::vector<std::uint8_t> after{bench.image.bytes().begin(), bench.image.bytes().end()};
    if (after != before || bench.transport.submitted().size() != 2U
        || !headerIs(bench.transport.submitted()[0], 0x01, 0x03, 0x74, 0x2A, 0x00, 0x78)
        || !headerIs(bench.transport.submitted()[1], 0x01, 0x03, 0x74, 0xA2, 0x00, 0x18)
        || !noWriteFrames(bench.transport.submitted())) {
        return fail("can_msg_reads_seventy_two");
    }
    return true;
}

/// @brief 目录里没有的 ID 不提交请求。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 InvalidParameter 且提交数为 0 时为 true。
bool unknownIdDoesNotEnqueue(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    Bench bench{catalog, layout, emptyImage()};
    if (!bench.session.connect().ok()) {
        return fail("unknown_id_does_not_enqueue");
    }
    ParameterId id{ParameterId::parse(" ").value()};
    if (!parseId("missing-parameter", "unknown_id_does_not_enqueue", id)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(id);
    if (started.ok() || started.tryError() == nullptr || *started.tryError() != ErrorCode::InvalidParameter
        || !bench.transport.submitted().empty() || bench.store.size() != 0U) {
        return fail("unknown_id_does_not_enqueue");
    }
    return true;
}

/// @brief check_data 没有元素标量类型，不提交请求。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 InvalidParameter 且提交数为 0 时为 true。
bool checkDataDoesNotEnqueue(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    Bench bench{catalog, layout, emptyImage()};
    if (!bench.session.connect().ok()) {
        return fail("check_data_does_not_enqueue");
    }
    ParameterId id{ParameterId::parse(" ").value()};
    if (!parseId("check_data", "check_data_does_not_enqueue", id)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(id);
    if (started.ok() || started.tryError() == nullptr || *started.tryError() != ErrorCode::InvalidParameter
        || !bench.transport.submitted().empty() || bench.store.size() != 0U) {
        return fail("check_data_does_not_enqueue");
    }
    return true;
}

/// @brief 会话未连接时不提交请求。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 DeviceDisconnected 且提交数为 0 时为 true。
bool disconnectedDoesNotEnqueue(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    Bench bench{catalog, layout, emptyImage()};
    ParameterId id{ParameterId::parse(" ").value()};
    if (!parseId("ANC_OFF", "disconnected_does_not_enqueue", id)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(id);
    if (started.ok() || started.tryError() == nullptr || *started.tryError() != ErrorCode::DeviceDisconnected
        || !bench.transport.submitted().empty()) {
        return fail("disconnected_does_not_enqueue");
    }
    return true;
}

/// @brief 上一笔读还没收齐时，第二笔不新增请求。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 第二笔为 InvalidParameter，提交数仍为 1 时为 true。
bool secondReadWhileBusy(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    Bench bench{catalog, layout, emptyImage()};
    if (!bench.session.connect().ok()) {
        return fail("second_read_while_busy");
    }
    ParameterId first{ParameterId::parse(" ").value()};
    ParameterId second{ParameterId::parse(" ").value()};
    if (!parseId("ANC_OFF", "second_read_while_busy", first) || !parseId("ENC_OFF", "second_read_while_busy", second)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(first);
    const Result<ReadTicket> again = bench.reader.read(second);
    if (!started.ok() || again.ok() || again.tryError() == nullptr || *again.tryError() != ErrorCode::InvalidParameter
        || bench.transport.submitted().size() != 1U || !bench.reader.busy()) {
        return fail("second_read_while_busy");
    }
    return true;
}

/// @brief 旧会话的结果不写入 Store。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return apply 之后 Store 仍为空，并且读对象不再忙碌时为 true。
bool staleSessionDoesNotObserve(const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    Bench bench{catalog, layout, emptyImage()};
    const Result<SessionId> connected = bench.session.connect();
    if (!connected.ok() || connected.tryValue() == nullptr) {
        return fail("stale_session_does_not_observe");
    }
    const SessionId oldSession = *connected.tryValue();
    ParameterId id{ParameterId::parse(" ").value()};
    if (!parseId("ANC_OFF", "stale_session_does_not_observe", id)) {
        return false;
    }
    const Result<ReadTicket> started = bench.reader.read(id);
    if (!started.ok() || started.tryValue() == nullptr || started.value().commands().empty()) {
        return fail("stale_session_does_not_observe");
    }
    if (!bench.session.disconnect().ok()) {
        return fail("stale_session_does_not_observe");
    }
    CommandOutcome outcome;
    outcome.id = started.value().commands().front();
    outcome.session = oldSession;
    outcome.body = ReadCommandResult{0x01, {0x78, 0x56, 0x34, 0x12}, 1};
    bench.reader.apply(outcome, RuntimeTime{std::chrono::seconds{12}});
    if (bench.reader.busy() || bench.reader.image() != nullptr || bench.store.size() != 0U) {
        return fail("stale_session_does_not_observe");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout, bool& recognized) {
    recognized = true;
    if (name == "anc_off_observes_scalar") {
        return ancOffObservesScalar(catalog, layout);
    }
    if (name == "can_msg_reads_seventy_two") {
        return canMsgReadsSeventyTwo(catalog, layout);
    }
    if (name == "unknown_id_does_not_enqueue") {
        return unknownIdDoesNotEnqueue(catalog, layout);
    }
    if (name == "check_data_does_not_enqueue") {
        return checkDataDoesNotEnqueue(catalog, layout);
    }
    if (name == "disconnected_does_not_enqueue") {
        return disconnectedDoesNotEnqueue(catalog, layout);
    }
    if (name == "second_read_while_busy") {
        return secondReadWhileBusy(catalog, layout);
    }
    if (name == "stale_session_does_not_observe") {
        return staleSessionDoesNotObserve(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个参数读用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_read_parameter_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    bool recognized = false;
    if (!runCase(argv[1], catalog.catalog(), layout.layout(), recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
