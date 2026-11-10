#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace ernc::domain {

/// 领域失败类别。名称来自本文件原先标明的设计 v1.2 第 15.3 节词汇；仓库内的实施计划没有这一节正文。
/// 本切片只定义名称，任何一个枚举值都不会访问设备、文件或数据库。
/// 整数值没有线上编码，不能当作 Modbus 异常码或固件字段。证据等级：设计约束。当前没有已批准 Golden。
enum class ErrorCode {
    /// 链路已经断开。读数变为 Stale 由后续状态保存负责。
    DeviceDisconnected,

    /// 串口没有打开成功，帧收发尚未开始。
    SerialOpenFailed,

    /// 串口打开之后的读或写失败。这个类别不表示帧内容已经通过校验。
    SerialIoFailed,

    /// 帧的实际长度和它所声明的布局不一致。长度字节是否包含前缀属于线协议契约，不由本枚举定义。
    FrameLengthInvalid,

    /// CRC 校验没有通过。覆盖范围和算法不由本枚举定义。
    CrcMismatch,

    /// 对端返回了 Modbus 异常应答。具体异常码不存放在这里。
    ModbusException,

    /// 等待结束时仍没有完整应答。写入路径上的超时不能据此判断设备没有执行，也不能据此重发。
    ResponseTimeout,

    /// 应答和当前在途请求对不上。要比对哪些字段由后续校验器规定。
    ResponseMismatch,

    /// 允许的重试已经用完。重试次数不存放在这里。
    RetryExhausted,

    /// 调用方给出的领域值被本层拒绝，例如空的 ParameterId，或 ScalarValue 的种类不匹配。
    InvalidParameter,

    /// 修改只读参数的尝试。顶层 Step 只读是架构约束；是否产生本错误由后续写入路径决定。
    ReadOnlyViolation,

    /// 载荷的字节数和该值所声称的宽度不一致。slot 与寄存器的换算不在这里。
    PayloadSizeMismatch,

    /// 区域或数组载荷无法按声明的类型解码。解码器不在本切片中。
    RegionDecodeFailed,

    /// 工程或数据文件的读写失败。打开文件不会因此变成一次硬件写入。
    FileIoFailed,

    /// 数据库操作失败。本切片不打开 SQLite。
    DatabaseFailed,

    /// 调用方取消了操作。取消后的设备状态需要另行对账，这个错误码本身不是成功。
    OperationCancelled,

    /// 读回的值与期望不一致。它记录的是读回 Verified 失败，不是设备 ACK。
    ReadBackMismatch,

    /// 没有更具体的类别。出现在失败对象上时是一次真实失败。
    /// Result<void> 成功对象内部也用它做占位，但该占位不能通过 tryError() 或 error() 读到。
    Unknown
};

/// Result 在错误状态上被取值时使用的终止入口。不属于领域词汇。
namespace detail {

/// @brief 终止进程。
///
/// value() 或 error() 发现另一侧才是激活状态时调用。这是调用方的编程错误，不代表某一次设备失败，
/// 也不把该失败写入 Result。函数不返回、不抛出异常。
[[noreturn]] inline void abortInvalidResultAccess() noexcept {
    std::abort();
}

}  // namespace detail

/// @brief 一次同步领域操作的结果：内部恰好激活 T 或 ErrorCode 其中一个。
/// @tparam T 成功载荷。去掉顶层 cv 之后不能是 ErrorCode。
///
/// 调用方按值持有 Result。T 的资源放在对象内部，随对象析构释放；对象能否复制由 T 决定。
/// ok() 只说明 T 处于激活态。Sent、Acked、Verified 属于后续设备会话，不由本类型记录。
/// 本类型没有线程亲和性，也不访问 Qt、串口、文件或数据库。
/// 没有默认构造，因此不会出现既没有载荷也没有错误码的对象。
/// value() 或 error() 用在另一侧激活时会终止进程，不抛异常。
template <typename T>
class Result {
    static_assert(!std::is_same_v<std::remove_cv_t<T>, ErrorCode>);

public:
    /// @brief 把一份载荷做成成功结果。不访问设备、文件或时钟。
    /// @param value 要保存的载荷。按值接收：传入右值时 T 的资源移入函数，传入左值时先拷贝。返回的 Result 持有自己的那一份。
    /// @return 只激活 T。ok() 为 true，tryError() 为空指针。
    /// @note 这里的成功只表示载荷已经放进结果，不是设备 ACK，也不是读回 Verified。T 的拷贝或移动若抛出异常，则不会产生 Result。
    static constexpr Result success(T value) {
        return Result{std::move(value)};
    }

    /// @brief 做成只含错误码的失败结果，不构造 T。
    /// @param code 失败类别。Unknown 也是合法的失败码。
    /// @return ok() 为 false，tryValue() 为空指针。本函数不抛出异常。
    static constexpr Result failure(ErrorCode code) noexcept {
        return Result{code};
    }

    /// @brief 报告当前激活的是不是 T。
    /// @return T 激活时为 true。这不表示设备已经确认写入。
    constexpr bool ok() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    /// @brief 在成功时借出内部载荷。
    /// @return 成功时指向内部 T，失败时为空指针。指针不转移所有权，只在本 Result 存活期间有效。
    constexpr const T* tryValue() const noexcept {
        return std::get_if<T>(&storage_);
    }

    /// @brief 在失败时借出内部错误码。
    /// @return 失败时指向内部 ErrorCode，成功时为空指针。指针不转移所有权，只在本 Result 存活期间有效。
    constexpr const ErrorCode* tryError() const noexcept {
        return std::get_if<ErrorCode>(&storage_);
    }

    /// @brief 取得成功载荷的常量引用。
    /// @return 引用本对象内部的 T，不转移所有权，生存期与本对象相同。
    /// @note 失败时终止进程且不返回。
    constexpr const T& value() const {
        if (const T* held = tryValue()) {
            return *held;
        }
        detail::abortInvalidResultAccess();
    }

    /// @brief 取得失败类别的副本。
    /// @return 按值返回当前的 ErrorCode。
    /// @note 成功时终止进程且不返回。
    constexpr ErrorCode error() const {
        if (const ErrorCode* code = tryError()) {
            return *code;
        }
        detail::abortInvalidResultAccess();
    }

    /// @brief 两侧激活的备选及其内容都相同则为相等。比较不包含发生时间或设备状态。
    constexpr bool operator==(const Result&) const = default;

private:
    /// @brief 把载荷移入 variant 的 T 备选。
    /// @param value 本构造函数持有的载荷。资源再移入 storage_，参数本身留在被移后状态。
    explicit constexpr Result(T value) : storage_{std::move(value)} {}

    /// @brief 把错误码放入 variant 的 ErrorCode 备选，不构造 T。
    /// @param code 失败类别。
    explicit constexpr Result(ErrorCode code) noexcept : storage_{code} {}

    /// 当前结果的唯一存储。T 与 ErrorCode 恰好一个有效。
    std::variant<T, ErrorCode> storage_;
};

/// @brief 没有载荷的同步结果。成功或失败二选一，失败时携带一个 ErrorCode。
///
/// 成功对象不向调用方提供错误码。内部未使用的 error_ 写为 Unknown，tryError() 在成功时为空指针。
/// 因此 Unknown 只有出现在失败对象上时才是一次真实失败。
/// ok() 只表示这次结果被标为成功，不是设备 ACK 或读回 Verified。
template <>
class Result<void> {
public:
    /// @brief 做成没有载荷的成功结果。
    /// @return ok() 为 true，tryError() 为空指针。
    static constexpr Result success() noexcept {
        return Result{true, ErrorCode::Unknown};
    }

    /// @brief 做成没有载荷的失败结果。
    /// @param code 失败类别，按值保存。Unknown 同样表示一次真实失败。
    /// @return ok() 为 false。
    static constexpr Result failure(ErrorCode code) noexcept {
        return Result{false, code};
    }

    /// @brief 报告这个结果是否被标为成功。
    /// @return 成功标记。不表示设备已确认。
    constexpr bool ok() const noexcept { return ok_; }

    /// @brief 失败时借出内部错误码。
    /// @return 失败时指向 error_，成功时为空指针。指针不转移所有权，只在本对象存活期间有效。
    constexpr const ErrorCode* tryError() const noexcept {
        return ok_ ? nullptr : &error_;
    }

    /// @brief 取得失败类别的副本。
    /// @return 按值返回 error_。
    /// @note 成功时终止进程且不返回。
    constexpr ErrorCode error() const {
        if (const ErrorCode* code = tryError()) {
            return *code;
        }
        detail::abortInvalidResultAccess();
    }

    /// @brief 成功标记和错误码都相同则为相等。两个成功对象相等。
    constexpr bool operator==(const Result&) const = default;

private:
    /// @brief 同时写入成功标记和错误槽。
    /// @param ok 为 true 时表示成功，此时 code 只作占位。
    /// @param code 失败时的错误类别。成功时调用方传入 Unknown，该值不对外暴露。
    explicit constexpr Result(bool ok, ErrorCode code) noexcept
        : ok_{ok}, error_{code} {}

    /// 成功标记。true 时 error_ 不是激活的错误。
    bool ok_;

    /// 失败时的错误类别。成功时固定为 Unknown，调用方不能通过 tryError() 或 error() 读到它。
    ErrorCode error_;
};

/// @brief 参数目录里的稳定标识。对象自己持有一份 std::string。
///
/// 标识文本是目录主键的文本形式。固件路径、逻辑 slot、Modbus 寄存器地址和字节偏移使用各自的地址空间，不存放在这里。
/// parse 不查询目录是否存在，也不打开工程或访问设备。没有默认构造，因此不会出现未经解析的空标识。
class ParameterId {
public:
    /// @brief 用一段标识文本构造 ParameterId。
    /// @param text 标识文本，只在本次调用期间借用；返回后不再使用这段 view。空文本被拒绝。文本按原样保留，不做路径、slot 或地址解析。
    /// @return 非空时成功，结果内部持有独立的字符串副本。空文本时失败，错误码为 InvalidParameter。
    static constexpr Result<ParameterId> parse(std::string_view text) {
        if (text.empty()) {
            return Result<ParameterId>::failure(ErrorCode::InvalidParameter);
        }
        return Result<ParameterId>::success(ParameterId{std::string{text}});
    }

    /// @brief 借出本对象持有的标识文本。
    /// @return 引用内部字符串，不转移所有权，生存期与本 ParameterId 相同。
    constexpr const std::string& text() const noexcept { return id_; }

    /// @brief 两侧标识文本相同则为相等。
    constexpr bool operator==(const ParameterId&) const = default;

private:
    /// @brief 接管一份已经判定为非空的标识字符串。
    /// @param id 按值接收的字符串，再移入 id_。本对象成为这段文本的所有者。
    explicit constexpr ParameterId(std::string id) : id_{std::move(id)} {}

    /// 标识文本的拥有副本。parse 拒绝空文本，因此经公开入口得到的对象不会持有空字符串。
    std::string id_;
};

/// @brief 设备会话的不透明代号。这是一个可复制的数值，没有堆资源。
///
/// raw() 为 0 表示没有会话，这和会话存在是两种状态。代号的代数、到期和断连后的失效由会话所有者记录。
/// 本对象不计时，也不连接串口。没有默认构造；0 只能经 none() 或 fromRaw(0) 明确写出。
class SessionId {
public:
    /// @brief 做成“没有会话”的代号。
    /// @return raw() 为 0，valid() 为 false。
    static constexpr SessionId none() noexcept { return SessionId{0}; }

    /// @brief 用既有代号做成 SessionId。不创建会话，也不检查它是否仍然连接。
    /// @param raw 会话代号，无单位。0 与 none() 相同。非 0 只表示代号本身，不证明链路仍然可用，也不包含到期时刻。
    /// @return 保存该代号的值。
    static constexpr SessionId fromRaw(std::uint64_t raw) noexcept {
        return SessionId{raw};
    }

    /// @brief 报告这个代号是不是一次会话。
    /// @return raw() 非 0 时为 true。0 表示没有会话。
    constexpr bool valid() const noexcept { return raw_ != 0; }

    /// @brief 返回底层代号。
    /// @return 无单位整数。0 表示没有会话。
    constexpr std::uint64_t raw() const noexcept { return raw_; }

    /// @brief 两侧代号数值相同则为相等。到期与否由会话所有者判断，不在这次比较里。
    constexpr bool operator==(const SessionId&) const = default;

private:
    /// @brief 保存调用方给出的代号。
    /// @param raw 会话代号，无单位。0 表示没有会话。
    explicit constexpr SessionId(std::uint64_t raw) noexcept : raw_{raw} {}

    /// 会话代号，无单位。0 表示没有会话；非 0 不包含代数或到期时刻。
    std::uint64_t raw_;
};

/// @brief 一个标量参数值，只保存 float、int32_t、uint32_t 三种表示中的一种。
///
/// 三种表示互不转换：数值同为 1 的 Float32、Int32 和 UInt32 仍然不相等。
/// 公开入口只有 fromFloat、fromInt32、fromUInt32；对应的删除重载让 double 和其他整数类型在编译期被拒绝。
/// 本对象不携带物理单位、缩放，也不区分 deviceValue 与 stagedValue。字节序属于后续的 typed codec。
/// 证据等级：设计约束。当前没有已批准 Golden 把这些值定义成线上字节。没有默认构造，也没有线程亲和性。
class ScalarValue {
public:
    /// 当前 storage_ 里激活的是哪一种表示。它不是线上类型码。
    enum class Kind {
        /// IEEE-754 binary32，不是 double。
        Float32,

        /// 二进制补码的 32 位有符号整数。
        Int32,

        /// 32 位无符号整数。高位为 1 时仍表示无符号值。
        UInt32
    };

    /// @brief 拒绝 float 以外的实参进入 Float32。
    /// @tparam T 实参类型。去掉引用和 cv 之后不是 float 时，调用应编译失败。
    template <typename T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, float>)
    static constexpr ScalarValue fromFloat(T) = delete;

    /// @brief 做成 Float32 标量。不换算单位，也不生成线上字节。
    /// @param value IEEE-754 binary32 数值，没有物理单位。
    /// @return kind() 为 Float32 的值。
    static constexpr ScalarValue fromFloat(float value) noexcept {
        return ScalarValue{value};
    }

    /// @brief 拒绝 int32_t 以外的实参进入 Int32。
    /// @tparam T 实参类型。去掉引用和 cv 之后不是 int32_t 时，调用应编译失败。
    template <typename T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, std::int32_t>)
    static constexpr ScalarValue fromInt32(T) = delete;

    /// @brief 做成 Int32 标量。不把位模式解释成无符号数或浮点数。
    /// @param value 二进制补码的 32 位有符号整数，没有物理单位。
    /// @return kind() 为 Int32 的值。
    static constexpr ScalarValue fromInt32(std::int32_t value) noexcept {
        return ScalarValue{value};
    }

    /// @brief 拒绝 uint32_t 以外的实参进入 UInt32。
    /// @tparam T 实参类型。去掉引用和 cv 之后不是 uint32_t 时，调用应编译失败。
    template <typename T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, std::uint32_t>)
    static constexpr ScalarValue fromUInt32(T) = delete;

    /// @brief 做成 UInt32 标量。高位为 1 时保持无符号数值。
    /// @param value 32 位无符号整数，没有物理单位。
    /// @return kind() 为 UInt32 的值。
    static constexpr ScalarValue fromUInt32(std::uint32_t value) noexcept {
        return ScalarValue{value};
    }

    /// @brief 报告当前激活的表示种类。
    /// @return Float32、Int32 或 UInt32 之一。不返回数值本身。
    constexpr Kind kind() const noexcept {
        if (std::holds_alternative<float>(storage_)) {
            return Kind::Float32;
        }
        if (std::holds_alternative<std::int32_t>(storage_)) {
            return Kind::Int32;
        }
        return Kind::UInt32;
    }

    /// @brief 只在当前种类为 Float32 时取出数值。
    /// @return 种类匹配时成功，结果持有 float 副本。种类不匹配时失败，错误码为 InvalidParameter，不做数值转换或位重解释。
    constexpr Result<float> asFloat() const noexcept {
        if (const auto* held = std::get_if<float>(&storage_)) {
            return Result<float>::success(*held);
        }
        return Result<float>::failure(ErrorCode::InvalidParameter);
    }

    /// @brief 只在当前种类为 Int32 时取出数值。
    /// @return 种类匹配时成功，结果持有 int32_t 副本。种类不匹配时失败，错误码为 InvalidParameter，不做数值转换或位重解释。
    constexpr Result<std::int32_t> asInt32() const noexcept {
        if (const auto* held = std::get_if<std::int32_t>(&storage_)) {
            return Result<std::int32_t>::success(*held);
        }
        return Result<std::int32_t>::failure(ErrorCode::InvalidParameter);
    }

    /// @brief 只在当前种类为 UInt32 时取出数值。
    /// @return 种类匹配时成功，结果持有 uint32_t 副本。种类不匹配时失败，错误码为 InvalidParameter，不做数值转换或位重解释。
    constexpr Result<std::uint32_t> asUInt32() const noexcept {
        if (const auto* held = std::get_if<std::uint32_t>(&storage_)) {
            return Result<std::uint32_t>::success(*held);
        }
        return Result<std::uint32_t>::failure(ErrorCode::InvalidParameter);
    }

    /// @brief 种类和数值都相同才相等。
    constexpr bool operator==(const ScalarValue&) const = default;

private:
    /// @brief 把一个 float 放进 Float32 备选。
    /// @param value IEEE-754 binary32 数值，没有物理单位。
    explicit constexpr ScalarValue(float value) noexcept : storage_{value} {}

    /// @brief 把一个 int32_t 放进 Int32 备选。
    /// @param value 二进制补码的 32 位有符号整数，没有物理单位。
    explicit constexpr ScalarValue(std::int32_t value) noexcept : storage_{value} {}

    /// @brief 把一个 uint32_t 放进 UInt32 备选。
    /// @param value 32 位无符号整数，没有物理单位。
    explicit constexpr ScalarValue(std::uint32_t value) noexcept : storage_{value} {}

    /// 三种表示中恰好一种有效。不保存字节序、单位，也不区分 deviceValue 与 stagedValue。
    std::variant<float, std::int32_t, std::uint32_t> storage_;
};

}  // namespace ernc::domain
