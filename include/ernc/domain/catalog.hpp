#pragma once

#include "ernc/domain/types.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::domain {

/// 目录文档本身的失败类别。它不进入 ErrorCode：那份枚举描述设备与文件操作，不能区分这三种目录拒绝。
enum class CatalogError {
    /// 两份记录使用了相同的 id 文本。加载失败，不返回部分目录。
    DuplicateId,

    /// 给出的 editor 不是文本，空白，或不在本切片承认的控制类型闭集里。缺省表示没有声明控件。
    InvalidControl,

    /// 给出了 firmware_path，但它不是文本或只含空白。缺省路径表示没有读取范围，这里不把缺省当成错误。
    MissingPath,

    /// 控制类型本身合法，但配套语义矛盾。toggle 必须同时给出不相等的 on/off 线值；其他控制类型不能携带 wire_semantics。
    SemanticConflict,

    /// JSON 无法解析，或文档结构、版本、空白说明不符合本切片的 schema。说明可以缺省。
    InvalidDocument
};

/// 参数在目录里的标量数据类型。文本来自同名固件字段，不在这里做字节序转换。
enum class DataType {
    /// JSON `float32`。对应固件字段类型 float，占一个 4 字节 slot 的逻辑宽度；编码不在本类型完成。
    Float32,

    /// JSON `int32`。对应固件字段类型 int32_t。
    Int32,

    /// JSON `uint32`。对应固件字段类型 uint32_t。
    UInt32
};

/// 参数值的形状。本切片只保存声明，不解码数组或区域字节。
enum class ValueShape {
    /// JSON `scalar`。一个标量，不是数组。
    Scalar,

    /// JSON `array`。
    Array,

    /// JSON `region`。
    Region
};

/// 目录中的访问声明。manifest 的 writable:false 不是这种权限，不能从那里抄过来。
enum class AccessMode {
    /// JSON `read_only`。目录声明不生成普通写请求；本类型本身不发设备命令。
    ReadOnly,

    /// JSON `read_write`。
    ReadWrite,

    /// JSON `write_only`。
    WriteOnly,

    /// JSON `command`。瞬时命令的调度和车速联锁不在本切片。
    Command
};

/// 目录中的控制类型。闭集以外的文本都是 InvalidControl。
enum class EditorKind {
    /// JSON `numeric`。
    Numeric,

    /// JSON `read_only`。
    ReadOnly,

    /// JSON `toggle`。必须带 wire_semantics。
    Toggle,

    /// JSON `mode_selector`。
    ModeSelector,

    /// JSON `momentary_command`。
    MomentaryCommand,

    /// JSON `enum_selector`。
    EnumSelector,

    /// JSON `array_editor`。
    ArrayEditor,

    /// JSON `region_editor`。
    RegionEditor
};

/// toggle 的开、关线值。数值是目录里的十进制数，不是 float、int32 或 uint32 三种标量值之间的转换结果。
struct WireLevels {
    /// 说明文字里的“开启”线值。ANC_OFF、ENC_OFF、RNC_OFF 的证据是 0。没有物理单位。
    double on{0};

    /// 说明文字里的“关闭”线值。上述三个开关的证据是 1。没有物理单位。
    double off{0};
};

/// 工作表 1 对不上的结构体参数暂时使用的页面分类。它不是固件路径。
inline constexpr std::string_view kOtherPageCategory = "other";

/// 一条已通过加载校验的参数定义。字符串成员由本对象拥有。
struct ParameterDefinition {
    /// 目录主键。结构体参数使用成员的完整路径。
    ParameterId id;

    /// 固件字段路径的原文。加载器不把它解析成 offset 或 slot。缺省表示这条记录没有读取范围。
    std::optional<std::string> firmwarePath;

    /// 分组表里的分类。原始表没有分组时为空。工作表 1 对不上的结构体参数使用 other。
    std::optional<std::string> category;

    /// 参数说明表 B 列原文。加载器不改写拼写。说明表没有这一行时为空。
    std::optional<std::string> descriptionZh;

    /// 同名固件字段的数据类型。目录没有给出时为空，加载器不猜测。
    std::optional<DataType> dataType;

    /// 值的形状。目录没有给出时为空。
    std::optional<ValueShape> shape;

    /// 访问声明。没有单独证据时为空，不用 manifest 的 writable 代替。
    std::optional<AccessMode> access;

    /// 控制类型。目录没有声明控件时为空，不用 Numeric 填上。
    std::optional<EditorKind> editor;

    /// toggle 的 on/off 线值。其他控制类型必须为空。
    std::optional<WireLevels> wireLevels;
};

class ParameterCatalogLoader;

/// @brief 一份已经完成校验的参数目录。
///
/// 对象拥有全部定义。find 返回的指针指向内部向量，目录被销毁、移动或重新赋值后失效。
/// 加载完成后没有后台线程；调用方负责不在写入本对象的同时从另一个线程读取它。
class ParameterCatalog {
    friend class ParameterCatalogLoader;

public:
    /// @brief 按 id 文本查找定义。
    /// @param id 目录主键文本，只在本次调用期间借用。空文本和不存在的文本都只表示未找到。
    /// @return 指向内部定义的指针，不转移所有权。未找到时为空指针。
    const ParameterDefinition* find(std::string_view id) const noexcept;

    /// @brief 已载入的记录数。
    /// @return entries() 的元素个数。0 表示一份合法但没有参数的目录，不是加载失败。
    std::size_t size() const noexcept;

    /// @brief 按文件中的顺序借出全部记录。
    /// @return 内部向量的常量引用，不转移所有权，生存期与本目录对象相同。
    const std::vector<ParameterDefinition>& entries() const noexcept;

    /// @brief 文档 schema 版本。
    /// @return 本切片只接受 1。
    int schemaVersion() const noexcept;

    /// @brief 目录内容版本。
    /// @return 内部字符串引用，不转移所有权。0.1.0 表示当前只收录已有证据的条目。
    const std::string& catalogVersion() const noexcept;

    ParameterCatalog(const ParameterCatalog&) = default;
    ParameterCatalog& operator=(const ParameterCatalog&) = default;
    ParameterCatalog(ParameterCatalog&&) noexcept = default;
    ParameterCatalog& operator=(ParameterCatalog&&) noexcept = default;

private:
    /// @brief 接管加载器已经校验过的记录。
    /// @param schemaVersion 已确认等于 1 的 schema 版本。
    /// @param catalogVersion 非空的内容版本文本，所有权移入本对象。
    /// @param entries 校验后的记录，所有权移入本对象。
    ParameterCatalog(
        int schemaVersion,
        std::string catalogVersion,
        std::vector<ParameterDefinition> entries);

    /// 已接受的 schema 版本。公开加载路径只会产生 1。
    int schemaVersion_{1};

    /// 目录内容版本。空字符串不能从公开加载路径产生。
    std::string catalogVersion_;

    /// 按文档顺序存放的参数定义。
    std::vector<ParameterDefinition> entries_;
};

namespace detail {

/// @brief 终止进程。
///
/// CatalogLoad 在相反状态下读取目录或错误码时调用。这是调用方的编程错误，不表示某一条参数定义不合法。
[[noreturn]] inline void abortInvalidCatalogAccess() noexcept {
    std::abort();
}

}  // namespace detail

/// @brief 目录加载的两种结局之一：整份目录，或一个 CatalogError。
class [[nodiscard]] CatalogLoad {
public:
    /// @brief 保存一份校验通过的目录。
    /// @param catalog 按值接管的目录。
    /// @return 成功结果。tryError() 为空。
    static CatalogLoad success(ParameterCatalog catalog) {
        return CatalogLoad{std::move(catalog)};
    }

    /// @brief 保存一个目录错误。
    /// @param error 失败类别。不附带部分记录。
    /// @return 失败结果。tryCatalog() 为空。
    static CatalogLoad failure(CatalogError error) noexcept {
        return CatalogLoad{error};
    }

    /// @brief 是否载入了完整目录。
    /// @return 成功时为 true。
    bool ok() const noexcept { return ok_; }

    /// @brief 借出成功载入的目录。
    /// @return 成功时指向内部目录，失败时为空指针。指针不转移所有权，只在本对象存活且未被移动期间有效。
    const ParameterCatalog* tryCatalog() const noexcept {
        return ok_ ? &*catalog_ : nullptr;
    }

    /// @brief 借出失败类别。
    /// @return 失败时指向内部错误码，成功时为空指针。指针不转移所有权。
    const CatalogError* tryError() const noexcept {
        return ok_ ? nullptr : &error_;
    }

    /// @brief 取得成功目录的常量引用。
    /// @return 引用内部目录，不转移所有权。
    /// @note 失败时终止进程且不返回。
    const ParameterCatalog& catalog() const {
        if (const ParameterCatalog* loaded = tryCatalog()) {
            return *loaded;
        }
        detail::abortInvalidCatalogAccess();
    }

    /// @brief 取得失败类别的副本。
    /// @return 按值返回 CatalogError。
    /// @note 成功时终止进程且不返回。
    CatalogError error() const {
        if (const CatalogError* code = tryError()) {
            return *code;
        }
        detail::abortInvalidCatalogAccess();
    }

private:
    /// @brief 构造成功结果。
    /// @param catalog 已校验目录，所有权移入 optional。
    explicit CatalogLoad(ParameterCatalog catalog)
        : catalog_{std::move(catalog)}, ok_{true} {}

    /// @brief 构造失败结果，不放置目录。
    /// @param error 失败类别。
    explicit CatalogLoad(CatalogError error) noexcept : error_{error}, ok_{false} {}

    /// 成功时持有目录。失败时为空。
    std::optional<ParameterCatalog> catalog_;

    /// 失败类别。成功时不对外暴露。
    CatalogError error_{CatalogError::InvalidDocument};

    /// true 表示 catalog_ 有效且 error_ 不可读。
    bool ok_{false};
};

/// @brief 从版本化 JSON 目录载入参数定义和中文说明。
///
/// 只解析调用方提供的 JSON 文本或那个文件的字节。不打开 Excel，不读取 catalog/source 下的原始表，
/// 也不查询固件 manifest。
class ParameterCatalogLoader {
public:
    /// @brief 读取一个 JSON 文件并整份校验。
    /// @param path 目录文件路径，只在本次调用期间借用。函数打开、读完并关闭文件，不保留句柄。
    /// @return 成功时包含完整目录。打不开文件、内容不是本 schema，或任一记录不合法时失败，不返回部分目录。
    static CatalogLoad loadFile(const std::filesystem::path& path);

    /// @brief 校验一段已经在内存中的 JSON 文本。
    /// @param jsonText UTF-8 JSON 文本，只在本次调用期间借用。文本里的字符串会被复制到结果目录。
    /// @return 与 loadFile 相同的成功或失败结局。本函数不访问文件系统。
    static CatalogLoad loadText(std::string_view jsonText);
};

}  // namespace ernc::domain
