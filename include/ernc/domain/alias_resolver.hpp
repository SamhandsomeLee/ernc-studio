#pragma once

#include "ernc/domain/firmware_layout.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::domain {

/// 说明表有、工作表 1 没有、且 manifest 路径唯一的参数使用的主分类。
/// 文本与设计里的导航入口一致。它不是固件路径，也不表示该参数已经可以写入。
inline constexpr std::string_view kAdvancedParameterCategory = "高级参数";

/// 别名文档的失败类别。它不进入 ErrorCode，也不表示已经向设备发出读取。
enum class AliasError {
    /// 同一 Excel 名称出现了两次。加载失败，不保留先出现的那条。
    DuplicateName,

    /// 两个名称指向同一固件路径，但没有双方都标明的共享绑定。后一条不能盖掉前一条。
    PathConflict,

    /// status 为 unique 的 firmware_path 在 manifest 字段树里不存在。空路径不属于这一类。
    UnknownPath,

    /// JSON 无法解析，或文档结构、状态和字段组合不符合本切片的 schema。不返回部分别名表。
    InvalidDocument
};

/// 一条 Excel 名称的解析结局。未写入本表的名称不是 Unresolved，查找结果只是空指针。
enum class AliasResolution {
    /// 恰好对应一个 manifest 字段路径。slot、字节长度和类型仍由 FirmwareLayout::resolve 回答。
    Unique,

    /// 表里明确写了不绑定路径。调用方不能为此名称生成读取范围。
    Unresolved
};

/// 一条已通过加载校验的别名。字符串成员由本对象拥有。
struct AliasRecord {
    /// Excel 名称原文。比较时区分大小写，不与固件路径互相替代。
    std::string name;

    /// 工作表 1 的主分类，或 kAdvancedParameterCategory。文档没有给出时为空。
    std::optional<std::string> category;

    /// Unique 或 Unresolved。本表没有第三种成功结局。
    AliasResolution resolution{AliasResolution::Unresolved};

    /// Unique 时的 manifest 路径原文。Unresolved 必须为空。加载器不把它换成 slot。
    std::optional<std::string> firmwarePath;

    /// 为 true 时，至少还有另一条 Unique 记录显式共享同一 firmwarePath。
    /// 单独一条记录不能把该标志设为 true。Unresolved 必须为 false。
    bool shared{false};

    /// Unresolved 的原因。Unique 必须为空。原因只解释为何没有读取范围，不提供替代路径。
    std::optional<std::string> reason;
};

class ParameterAliasLoader;

/// @brief 一份已经完成校验的参数别名表。
///
/// 对象拥有全部记录。find 返回的指针指向内部向量，表被销毁、移动或重新赋值后失效。
/// 加载时借用 FirmwareLayout 只为确认路径是否出现在字段树里，不保留布局对象。
/// 没有后台线程；调用方负责不在写入本对象的同时从另一个线程读取它。
class ParameterAliasTable {
    friend class ParameterAliasLoader;

public:
    /// @brief 按 Excel 名称原文查找。
    /// @param name 别名表中的名称，只在本次调用期间借用。空文本、大小写不同的文本和未收录的文本都只表示未找到。
    /// @return 指向内部记录的指针，不转移所有权。未找到时为空指针。Unresolved 记录也会返回指针。
    const AliasRecord* find(std::string_view name) const noexcept;

    /// @brief 按主分类收集记录。
    /// @param category 分类原文，只在本次调用期间借用。空文本不匹配任何记录。
    /// @return 按文件顺序排列的内部指针。指针不转移所有权，生存期与本表对象相同。
    std::vector<const AliasRecord*> findByCategory(std::string_view category) const;

    /// @brief 已载入的记录数。
    /// @return entries() 的元素个数。0 表示一份合法但没有别名的表，不是加载失败。
    std::size_t size() const noexcept;

    /// @brief 按文件中的顺序借出全部记录。
    /// @return 内部向量的常量引用，不转移所有权，生存期与本表对象相同。
    const std::vector<AliasRecord>& entries() const noexcept;

    /// @brief 文档 schema 版本。
    /// @return 本切片只接受 1。
    int schemaVersion() const noexcept;

    ParameterAliasTable(const ParameterAliasTable&) = default;
    ParameterAliasTable& operator=(const ParameterAliasTable&) = default;
    ParameterAliasTable(ParameterAliasTable&&) noexcept = default;
    ParameterAliasTable& operator=(ParameterAliasTable&&) noexcept = default;

private:
    /// @brief 接管加载器已经校验过的记录。
    /// @param schemaVersion 已确认等于 1 的 schema 版本。
    /// @param entries 校验后的记录，所有权移入本对象。
    ParameterAliasTable(int schemaVersion, std::vector<AliasRecord> entries);

    /// 已接受的 schema 版本。公开加载路径只会产生 1。
    int schemaVersion_{1};

    /// 按文档顺序存放的别名。
    std::vector<AliasRecord> entries_;
};

namespace detail {

/// @brief 终止进程。
///
/// AliasLoad 在相反状态下读取别名表或错误码时调用。这是调用方的编程错误，不表示某一条别名不合法。
[[noreturn]] inline void abortInvalidAliasAccess() noexcept {
    std::abort();
}

}  // namespace detail

/// @brief 别名加载的两种结局之一：整份别名表，或一个 AliasError。
class [[nodiscard]] AliasLoad {
public:
    /// @brief 保存一份校验通过的别名表。
    /// @param table 按值接管的别名表。
    /// @return 成功结果。tryError() 为空。
    static AliasLoad success(ParameterAliasTable table) {
        return AliasLoad{std::move(table)};
    }

    /// @brief 保存一个别名错误。
    /// @param error 失败类别。不附带部分记录。
    /// @return 失败结果。tryTable() 为空。
    static AliasLoad failure(AliasError error) noexcept {
        return AliasLoad{error};
    }

    /// @brief 是否载入了完整别名表。
    /// @return 成功时为 true。
    bool ok() const noexcept { return ok_; }

    /// @brief 借出成功载入的别名表。
    /// @return 成功时指向内部表，失败时为空指针。指针不转移所有权，只在本对象存活且未被移动期间有效。
    const ParameterAliasTable* tryTable() const noexcept {
        return ok_ ? &*table_ : nullptr;
    }

    /// @brief 借出失败类别。
    /// @return 失败时指向内部错误码，成功时为空指针。指针不转移所有权。
    const AliasError* tryError() const noexcept {
        return ok_ ? nullptr : &error_;
    }

    /// @brief 取得成功别名表的常量引用。
    /// @return 引用内部表，不转移所有权。
    /// @note 失败时终止进程且不返回。
    const ParameterAliasTable& table() const {
        if (const ParameterAliasTable* loaded = tryTable()) {
            return *loaded;
        }
        detail::abortInvalidAliasAccess();
    }

    /// @brief 取得失败类别的副本。
    /// @return 按值返回 AliasError。
    /// @note 成功时终止进程且不返回。
    AliasError error() const {
        if (const AliasError* code = tryError()) {
            return *code;
        }
        detail::abortInvalidAliasAccess();
    }

private:
    /// @brief 构造成功结果。
    /// @param table 已校验别名表，所有权移入 optional。
    explicit AliasLoad(ParameterAliasTable table)
        : table_{std::move(table)}, ok_{true} {}

    /// @brief 构造失败结果，不放置别名表。
    /// @param error 失败类别。
    explicit AliasLoad(AliasError error) noexcept : error_{error}, ok_{false} {}

    /// 成功时持有别名表。失败时为空。
    std::optional<ParameterAliasTable> table_;

    /// 失败类别。成功时不对外暴露。
    AliasError error_{AliasError::InvalidDocument};

    /// true 表示 table_ 有效且 error_ 不可读。
    bool ok_{false};
};

/// @brief 从版本化 JSON 载入 Excel 名称到固件路径的显式绑定。
///
/// 只解析调用方提供的 JSON 文本或那个文件的字节，并借用一份已经载入的 FirmwareLayout 核对路径是否存在。
/// 不打开 Excel，不读取 catalog/source，也不调用会把别名改写成路径的布局逻辑。
/// resolve 对 pack4-sequential 仍返回 Unverified；本加载器只把“不是 NotFound”视为路径存在。
class ParameterAliasLoader {
public:
    /// @brief 读取一个 JSON 文件并整份校验。
    /// @param path 别名文件路径，只在本次调用期间借用。函数打开、读完并关闭文件，不保留句柄。
    /// @param layout 已载入的固件布局，只在本次调用期间借用。函数不保存它，也不修改它。
    /// @return 成功时包含完整别名表。打不开文件、内容不是本 schema、路径不存在或绑定冲突时失败，不返回部分表。
    static AliasLoad loadFile(const std::filesystem::path& path, const FirmwareLayout& layout);

    /// @brief 校验一段已经在内存中的 JSON 文本。
    /// @param jsonText UTF-8 JSON 文本，只在本次调用期间借用。文本里的字符串会被复制到结果表。
    /// @param layout 已载入的固件布局，只在本次调用期间借用。函数不保存它，也不修改它。
    /// @return 与 loadFile 相同的成功或失败结局。本函数不访问文件系统。
    static AliasLoad loadText(std::string_view jsonText, const FirmwareLayout& layout);
};

}  // namespace ernc::domain
