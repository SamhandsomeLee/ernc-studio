#pragma once

#include "ernc/domain/catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::domain {

/// 固件路径解析的失败类别。它不进入 ErrorCode，也不表示设备读写已经发生。
enum class LayoutError {
    /// firmwarePath 在 manifest 的 fields 树里没有同名路径。别名和 unresolved 名单不在这里展开。
    NotFound,

    /// 找到了字段，但 status 不是调用方承认的状态。
    /// resolve() 把 pack4-sequential 算在这里。locate() 接受 pack4-sequential 和 verified，其他原文仍算在这里。
    Unverified,

    /// 字段已核验，但 offset、size、slot 或 shape 与 manifest 声明的总范围、4 字节对齐不一致。
    OutOfRange,

    /// JSON 无法解析，或 manifest 缺少对齐、总大小或字段必需成员。不返回部分布局。
    InvalidDocument
};

/// 字段几何的证据等级。它不是写入许可。
enum class LayoutEvidence {
    /// manifest 的 status 原文是 pack4-sequential。几何可以读取，不能当成已核验的真实写入许可。
    Pack4Sequential,

    /// manifest 的 status 原文是 verified。
    Verified
};

/// 一次成功解析得到的字段几何。字节序和 Modbus 寄存器地址不在这里计算。
struct FieldLayout {
    /// 逻辑 slot 起点。不是 Modbus 寄存器地址；寄存器起址属于后续协议任务。
    std::uint32_t slot{0};

    /// 连续占用的 slot 数。每个 slot 按设计是 4 字节。
    std::uint32_t slotCount{0};

    /// 字段长度，单位是字节。必须等于 slotCount * 4 才能解析成功。
    std::uint32_t size{0};

    /// 相对 manifest base 的字节偏移。用于边界检查，不作为报文地址。
    std::uint32_t offset{0};

    /// manifest 里的 type 原文，例如 float、int32_t 或结构体名。
    std::string typeName;

    /// type 原文能对应到 float32、int32、uint32 时填写。结构体类型为空，不猜测成标量。
    std::optional<DataType> scalarType;

    /// 字段自己的 writable。false 表示这份布局不能授权真实写入；解析成功不等于已经写设备。
    bool writable{false};

    /// 这份几何的证据等级。resolve() 成功时为 Verified。locate() 会保留 Pack4Sequential。
    LayoutEvidence evidence{LayoutEvidence::Verified};
};

namespace detail {

/// @brief 终止进程。
///
/// LayoutLoad 或 LayoutResolve 在相反状态下读取结果时调用。这是调用方的编程错误，不表示某个固件路径不合法。
[[noreturn]] inline void abortInvalidLayoutAccess() noexcept {
    std::abort();
}

}  // namespace detail

/// @brief 一次路径解析的两种结局之一：字段几何，或一个 LayoutError。
class [[nodiscard]] LayoutResolve {
public:
    /// @brief 保存一条通过核验和边界检查的几何。
    /// @param field 按值接管的字段布局。
    /// @return 成功结果。tryError() 为空。
    static LayoutResolve success(FieldLayout field) {
        return LayoutResolve{std::move(field)};
    }

    /// @brief 保存一个解析错误。
    /// @param error 找不到、未核验或越界。
    /// @return 失败结果。tryField() 为空。
    static LayoutResolve failure(LayoutError error) noexcept {
        return LayoutResolve{error};
    }

    /// @brief 是否得到了可用几何。
    /// @return 成功时为 true。
    bool ok() const noexcept { return ok_; }

    /// @brief 借出成功解析的几何。
    /// @return 成功时指向内部几何，失败时为空指针。指针不转移所有权。
    const FieldLayout* tryField() const noexcept {
        return ok_ ? &*field_ : nullptr;
    }

    /// @brief 借出失败类别。
    /// @return 失败时指向内部错误码，成功时为空指针。
    const LayoutError* tryError() const noexcept {
        return ok_ ? nullptr : &error_;
    }

    /// @brief 取得成功几何的常量引用。
    /// @return 引用内部 FieldLayout，不转移所有权。
    /// @note 失败时终止进程且不返回。
    const FieldLayout& field() const {
        if (const FieldLayout* resolved = tryField()) {
            return *resolved;
        }
        detail::abortInvalidLayoutAccess();
    }

    /// @brief 取得失败类别的副本。
    /// @return 按值返回 LayoutError。
    /// @note 成功时终止进程且不返回。
    LayoutError error() const {
        if (const LayoutError* code = tryError()) {
            return *code;
        }
        detail::abortInvalidLayoutAccess();
    }

private:
    /// @brief 构造成功结果。
    /// @param field 已通过检查的几何，所有权移入 optional。
    explicit LayoutResolve(FieldLayout field)
        : field_{std::move(field)}, ok_{true} {}

    /// @brief 构造失败结果，不放置几何。
    /// @param error 失败类别。
    explicit LayoutResolve(LayoutError error) noexcept : error_{error}, ok_{false} {}

    /// 成功时持有几何。失败时为空。
    std::optional<FieldLayout> field_;

    /// 失败类别。成功时不对外暴露。
    LayoutError error_{LayoutError::NotFound};

    /// true 表示 field_ 有效且 error_ 不可读。
    bool ok_{false};
};

class FirmwareLayoutLoader;

/// @brief 一份已通过文档结构校验的固件布局。
///
/// 对象拥有从 manifest 展平后的字段。resolve 和 locate 按路径原文查找，不读取 Excel，也不把候选别名改写成路径。
/// resolve 只交出 verified。locate 还交出几何合法的 pack4-sequential。两者都不写设备。
/// 没有后台线程；调用方负责不在移动本对象的同时从另一个线程读取它。
class FirmwareLayout {
    friend class FirmwareLayoutLoader;

public:
    /// @brief 把固件路径解析成 slot、字节长度和类型。
    /// @param firmwarePath manifest 中的完整路径，只在本次调用期间借用。空文本和不存在的文本都找不到。
    /// @return 找到、已核验且几何一致时包含 FieldLayout，evidence 为 Verified。
    /// pack4-sequential 仍是 Unverified，不带几何。越界时为 OutOfRange。
    LayoutResolve resolve(std::string_view firmwarePath) const;

    /// @brief 按路径取出可读几何，包括 pack4-sequential。
    ///
    /// 不把 pack4-sequential 提升为 verified，也不读取 writable 来拒绝。找不到、越界和其他 status 不带几何。
    /// @param firmwarePath manifest 中的完整路径，只在本次调用期间借用。空文本和不存在的文本都找不到。
    /// @return 路径存在、状态是 pack4-sequential 或 verified、并且几何落在镜像内时包含 FieldLayout。
    /// evidence 记录实际 status。否则只有一个 LayoutError。
    LayoutResolve locate(std::string_view firmwarePath) const;

    /// @brief 展平后的字段数量，包含 children 里的完整路径。
    /// @return 可被 resolve 查找的路径数。0 表示文档合法但没有字段。
    std::size_t fieldCount() const noexcept;

    FirmwareLayout(const FirmwareLayout&) = default;
    FirmwareLayout& operator=(const FirmwareLayout&) = default;
    FirmwareLayout(FirmwareLayout&&) noexcept = default;
    FirmwareLayout& operator=(FirmwareLayout&&) noexcept = default;

private:
    /// 索引中的一条字段。几何是否越界留到 resolve 和 locate。resolve 在未核验时不检查几何。
    struct StoredField {
        /// manifest 的 path 原文。children 里也必须是完整路径。
        std::string path;

        /// 字节偏移。单位是字节，不是 slot。
        std::uint32_t offset{0};

        /// 字节长度。
        std::uint32_t size{0};

        /// 逻辑 slot 起点。
        std::uint32_t slot{0};

        /// 占用的 slot 数。
        std::uint32_t slotCount{0};

        /// type 原文。
        std::string typeName;

        /// shape 各维长度。空表示 manifest 没有给出数组维度。
        std::vector<std::uint32_t> shape;

        /// status 原文。resolve() 只承认 verified。locate() 还承认 pack4-sequential。
        std::string status;

        /// 字段级 writable。文档级 writable 不代替它。
        bool writable{false};
    };

    /// @brief 按路径原文查找已索引字段。
    /// @param firmwarePath manifest 路径，只在本次调用期间借用。空文本视为找不到。
    /// @return 指向内部字段的指针。没有匹配时为空。指针不转移所有权。
    const StoredField* findStored(std::string_view firmwarePath) const;

    /// @brief 检查已索引字段的几何是否落在这份布局里。
    /// @param found 内部字段，只在本次调用期间借用。
    /// @return 几何一致时为空。越界或和 4 字节 slot 矛盾时为 OutOfRange。
    std::optional<LayoutError> geometryError(const StoredField& found) const;

    /// @brief 把已通过几何检查的字段做成对外几何。
    /// @param found 内部字段，只在本次调用期间借用。
    /// @param evidence 调用方已经按 status 判定的证据等级。
    /// @return 调用方拥有的 FieldLayout。不访问设备。
    FieldLayout toField(const StoredField& found, LayoutEvidence evidence) const;

    /// @brief 接管已经索引好的字段。
    /// @param base 布局起点，单位是字节。
    /// @param totalSize 从 base 起的总字节数。必须等于 totalSlots * 4。
    /// @param totalSlots 总 slot 数。
    /// @param fields 展平后的字段，所有权移入本对象。
    FirmwareLayout(
        std::uint32_t base,
        std::uint32_t totalSize,
        std::uint32_t totalSlots,
        std::vector<StoredField> fields);

    /// 布局字节起点。
    std::uint32_t base_{0};

    /// 从 base 起的镜像字节长度。
    std::uint32_t totalSize_{0};

    /// 镜像中的 slot 总数。一个 slot 固定按 4 字节解释。
    std::uint32_t totalSlots_{0};

    /// 可查找的字段。路径在加载时已保证不重复。
    std::vector<StoredField> fields_;
};

/// @brief 布局文档加载的两种结局之一：整份布局，或一个 LayoutError。
class [[nodiscard]] LayoutLoad {
public:
    /// @brief 保存一份结构合法的布局。
    /// @param layout 按值接管的布局。字段是否已核验仍由 resolve 判断。
    /// @return 成功结果。tryError() 为空。
    static LayoutLoad success(FirmwareLayout layout) {
        return LayoutLoad{std::move(layout)};
    }

    /// @brief 保存一个文档错误。
    /// @param error 失败类别。不附带部分字段索引。
    /// @return 失败结果。tryLayout() 为空。
    static LayoutLoad failure(LayoutError error) noexcept {
        return LayoutLoad{error};
    }

    /// @brief 是否载入了完整布局文档。
    /// @return 成功时为 true。
    bool ok() const noexcept { return ok_; }

    /// @brief 借出成功载入的布局。
    /// @return 成功时指向内部布局，失败时为空指针。指针不转移所有权。
    const FirmwareLayout* tryLayout() const noexcept {
        return ok_ ? &*layout_ : nullptr;
    }

    /// @brief 借出失败类别。
    /// @return 失败时指向内部错误码，成功时为空指针。
    const LayoutError* tryError() const noexcept {
        return ok_ ? nullptr : &error_;
    }

    /// @brief 取得成功布局的常量引用。
    /// @return 引用内部布局，不转移所有权。
    /// @note 失败时终止进程且不返回。
    const FirmwareLayout& layout() const {
        if (const FirmwareLayout* loaded = tryLayout()) {
            return *loaded;
        }
        detail::abortInvalidLayoutAccess();
    }

    /// @brief 取得失败类别的副本。
    /// @return 按值返回 LayoutError。
    /// @note 成功时终止进程且不返回。
    LayoutError error() const {
        if (const LayoutError* code = tryError()) {
            return *code;
        }
        detail::abortInvalidLayoutAccess();
    }

private:
    /// @brief 构造成功结果。
    /// @param layout 已索引布局，所有权移入 optional。
    explicit LayoutLoad(FirmwareLayout layout)
        : layout_{std::move(layout)}, ok_{true} {}

    /// @brief 构造失败结果，不放置布局。
    /// @param error 失败类别。
    explicit LayoutLoad(LayoutError error) noexcept : error_{error}, ok_{false} {}

    /// 成功时持有布局。失败时为空。
    std::optional<FirmwareLayout> layout_;

    /// 失败类别。成功时不对外暴露。
    LayoutError error_{LayoutError::InvalidDocument};

    /// true 表示 layout_ 有效且 error_ 不可读。
    bool ok_{false};
};

/// @brief 从固件 layout manifest 载入字段索引。
///
/// 只解析调用方提供的 JSON。不打开 Excel，不读取参数目录，不把未核验字段改成可写。
class FirmwareLayoutLoader {
public:
    /// @brief 读取一个 manifest 文件并建立字段索引。
    /// @param path manifest 路径，只在本次调用期间借用。函数打开、读完并关闭文件，不保留句柄。
    /// @return 文档结构合法时包含布局。打不开、JSON 非法或字段索引冲突时失败，不返回部分索引。
    static LayoutLoad loadFile(const std::filesystem::path& path);

    /// @brief 校验一段已经在内存中的 manifest JSON。
    /// @param jsonText UTF-8 JSON 文本，只在本次调用期间借用。路径和类型字符串会被复制到结果里。
    /// @return 与 loadFile 相同的成功或失败结局。本函数不访问文件系统。
    static LayoutLoad loadText(std::string_view jsonText);
};

}  // namespace ernc::domain
