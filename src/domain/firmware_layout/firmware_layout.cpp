#include "ernc/domain/firmware_layout.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

constexpr int kMaxJsonDepth = 32;
constexpr std::size_t kMaxNumberLength = 64;
constexpr std::uint32_t kSlotBytes = 4;
constexpr std::string_view kVerifiedStatus = "verified";

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    /// 当前激活的 JSON 备选。
    Kind kind{Kind::Null};

    /// kind 为 Bool 时的值。
    bool boolean{false};

    /// kind 为 Number 时的有限数值。还没有换成字节或 slot。
    double number{0};

    /// kind 为 String 时拥有的 UTF-8 文本。
    std::string text;

    /// kind 为 Array 时的元素，顺序与文本一致。
    std::vector<Json> elements;

    /// kind 为 Object 时的键。与 fieldValues 下标一一对应，同一对象内不重复。
    /// Json 的定义尚未结束，std::pair 不能在这里存放 Json；std::vector 可以。
    std::vector<std::string> fieldKeys;

    /// kind 为 Object 时的值，顺序与 fieldKeys 相同。
    std::vector<Json> fieldValues;
};

/// 展平前的字段。加载器再把它移入 FirmwareLayout 的私有索引。
struct RawField {
    /// path 原文。
    std::string path;

    /// 字节偏移。
    std::uint32_t offset{0};

    /// 字节长度。
    std::uint32_t size{0};

    /// 逻辑 slot 起点。
    std::uint32_t slot{0};

    /// 占用的 slot 数。
    std::uint32_t slotCount{0};

    /// type 原文。
    std::string typeName;

    /// shape 各维。空数组表示没有数组维度。
    std::vector<std::uint32_t> shape;

    /// status 原文。
    std::string status;

    /// 字段级 writable。
    bool writable{false};
};

/// @brief 判断字符是不是 JSON 空白。
/// @param character 单个输入字节。不按 locale 解释。
/// @return 空格、制表符、回车或换行时为 true。
bool isJsonSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

/// @brief 判断字符是不是十进制数字。
/// @param character 单个输入字节。
/// @return ASCII 0 到 9 时为 true。
bool isDigit(char character) {
    return character >= '0' && character <= '9';
}

/// @brief 判断文本是否没有可见内容。
/// @param text 候选路径，只在本次调用期间借用。
/// @return 空文本或只含 JSON 空白时为 true。
bool isBlank(std::string_view text) {
    for (char character : text) {
        if (!isJsonSpace(character)) {
            return false;
        }
    }
    return true;
}

/// @brief 把一个 Unicode 码点追加为 UTF-8。
/// @param out 接收字节的字符串，由调用方拥有。
/// @param codePoint 码点。代理项和大于 U+10FFFF 的值被拒绝。
/// @return 可以编码时为 true。
bool appendUtf8(std::string& out, char32_t codePoint) {
    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        return false;
    }
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    return true;
}

/// @brief 把符合 JSON 数字语法的片段换成 double。
/// @param token 不含空白的数字片段，只在本次调用期间借用。
/// @param converted 成功且结果有限时设为 true。
/// @return 转换结果。失败时返回 0，调用方不能把它当成偏移或长度。
double convertNumber(std::string_view token, bool& converted);

/// @brief 扫描 manifest JSON。对象内重复键直接失败，避免后写的 offset 覆盖先写的值。
class Parser {
public:
    /// @brief 绑定待解析文本。
    /// @param input UTF-8 JSON，只在 Parser 存活期间借用。
    explicit Parser(std::string_view input) : input_(input) {}

    /// @brief 解析整段文本中的唯一 JSON 值。
    /// @return 成功时返回根值。语法错误、超深度或尾部多余字符时为空。
    std::optional<Json> parseDocument() {
        skipBom();
        skipSpace();
        Json root = parseValue(0);
        skipSpace();
        if (failed_ || index_ != input_.size()) {
            return std::nullopt;
        }
        return root;
    }

private:
    /// @brief 跳过开头的 UTF-8 BOM。BOM 不是 JSON 值。
    void skipBom() {
        if (input_.size() >= 3 && static_cast<unsigned char>(input_[0]) == 0xEF
            && static_cast<unsigned char>(input_[1]) == 0xBB
            && static_cast<unsigned char>(input_[2]) == 0xBF) {
            index_ = 3;
        }
    }

    /// @brief 跳过连续 JSON 空白。
    void skipSpace() {
        while (index_ < input_.size() && isJsonSpace(input_[index_])) {
            ++index_;
        }
    }

    /// @brief 查看当前字节。
    /// @return 当前字节。已经到末尾时返回 NUL，这个 NUL 不是文本里的空字符。
    char peek() const {
        return index_ < input_.size() ? input_[index_] : '\0';
    }

    /// @brief 读取并前进一个字节。
    /// @return 被消费的字节。末尾时标记失败并返回 NUL。
    char get() {
        if (index_ >= input_.size()) {
            failed_ = true;
            return '\0';
        }
        return input_[index_++];
    }

    /// @brief 按当前深度解析一个 JSON 值。
    /// @param depth 已进入的数组和对象层数。根值深度为 0。
    /// @return 解析出的值。失败时调用方只查看 failed_。
    Json parseValue(int depth) {
        if (failed_ || depth > kMaxJsonDepth) {
            failed_ = true;
            return {};
        }
        skipSpace();
        const char current = peek();
        if (current == '{') {
            return parseObject(depth);
        }
        if (current == '[') {
            return parseArray(depth);
        }
        if (current == '"') {
            return parseString();
        }
        if (current == 't' || current == 'f') {
            return parseBoolean();
        }
        if (current == 'n') {
            return parseNull();
        }
        if (current == '-' || isDigit(current)) {
            return parseNumber();
        }
        failed_ = true;
        return {};
    }

    /// @brief 解析对象。重复键失败。
    /// @param depth 当前层数。
    /// @return kind 为 Object 的值。
    Json parseObject(int depth) {
        Json object;
        object.kind = Json::Kind::Object;
        if (get() != '{') {
            failed_ = true;
            return object;
        }
        skipSpace();
        if (peek() == '}') {
            get();
            return object;
        }
        while (!failed_) {
            skipSpace();
            Json key = parseString();
            if (failed_ || key.kind != Json::Kind::String) {
                failed_ = true;
                return object;
            }
            skipSpace();
            if (get() != ':') {
                failed_ = true;
                return object;
            }
            Json value = parseValue(depth + 1);
            if (failed_) {
                return object;
            }
            for (const std::string& existing : object.fieldKeys) {
                if (existing == key.text) {
                    failed_ = true;
                    return object;
                }
            }
            object.fieldKeys.reserve(object.fieldKeys.size() + 1);
            object.fieldValues.reserve(object.fieldValues.size() + 1);
            object.fieldKeys.push_back(std::move(key.text));
            object.fieldValues.push_back(std::move(value));
            skipSpace();
            const char separator = get();
            if (separator == '}') {
                return object;
            }
            if (separator != ',') {
                failed_ = true;
                return object;
            }
        }
        return object;
    }

    /// @brief 解析数组。
    /// @param depth 当前层数。
    /// @return kind 为 Array 的值。
    Json parseArray(int depth) {
        Json array;
        array.kind = Json::Kind::Array;
        if (get() != '[') {
            failed_ = true;
            return array;
        }
        skipSpace();
        if (peek() == ']') {
            get();
            return array;
        }
        while (!failed_) {
            array.elements.push_back(parseValue(depth + 1));
            if (failed_) {
                return array;
            }
            skipSpace();
            const char separator = get();
            if (separator == ']') {
                return array;
            }
            if (separator != ',') {
                failed_ = true;
                return array;
            }
        }
        return array;
    }

    /// @brief 解析字符串。未转义控制字节被拒绝，UTF-8 多字节原样保留。
    /// @return kind 为 String 的值。
    Json parseString() {
        Json value;
        value.kind = Json::Kind::String;
        if (get() != '"') {
            failed_ = true;
            return value;
        }
        while (!failed_) {
            if (index_ >= input_.size()) {
                failed_ = true;
                return value;
            }
            const char current = get();
            if (current == '"') {
                return value;
            }
            if (static_cast<unsigned char>(current) < 0x20) {
                failed_ = true;
                return value;
            }
            if (current != '\\') {
                value.text.push_back(current);
                continue;
            }
            const char escaped = get();
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.text.push_back(escaped);
                break;
            case 'b':
                value.text.push_back('\b');
                break;
            case 'f':
                value.text.push_back('\f');
                break;
            case 'n':
                value.text.push_back('\n');
                break;
            case 'r':
                value.text.push_back('\r');
                break;
            case 't':
                value.text.push_back('\t');
                break;
            case 'u':
                if (!appendHexEscape(value.text)) {
                    failed_ = true;
                    return value;
                }
                break;
            default:
                failed_ = true;
                return value;
            }
        }
        return value;
    }

    /// @brief 读取 \u 后的四个十六进制位并追加 UTF-8。
    /// @param out 目标字符串。
    /// @return 四位都有效且不是 UTF-16 代理项时为 true。
    bool appendHexEscape(std::string& out) {
        char32_t codePoint = 0;
        for (int nibble = 0; nibble < 4; ++nibble) {
            if (index_ >= input_.size()) {
                return false;
            }
            const char digit = get();
            int value = -1;
            if (digit >= '0' && digit <= '9') {
                value = digit - '0';
            } else if (digit >= 'a' && digit <= 'f') {
                value = digit - 'a' + 10;
            } else if (digit >= 'A' && digit <= 'F') {
                value = digit - 'A' + 10;
            }
            if (value < 0) {
                return false;
            }
            codePoint = (codePoint << 4) | static_cast<char32_t>(value);
        }
        return appendUtf8(out, codePoint);
    }

    /// @brief 解析 true 或 false。
    /// @return kind 为 Bool 的值。
    Json parseBoolean() {
        Json value;
        value.kind = Json::Kind::Bool;
        if (input_.substr(index_, 4) == "true") {
            value.boolean = true;
            index_ += 4;
            return value;
        }
        if (input_.substr(index_, 5) == "false") {
            value.boolean = false;
            index_ += 5;
            return value;
        }
        failed_ = true;
        return value;
    }

    /// @brief 解析 null。
    /// @return kind 为 Null 的值。
    Json parseNull() {
        Json value;
        if (input_.substr(index_, 4) != "null") {
            failed_ = true;
            return value;
        }
        index_ += 4;
        value.kind = Json::Kind::Null;
        return value;
    }

    /// @brief 解析 JSON 数字。不接受前导加号、前导零和 NaN。
    /// @return kind 为 Number 的值。超长或结果非有限值时失败。
    Json parseNumber() {
        Json value;
        value.kind = Json::Kind::Number;
        const std::size_t begin = index_;
        if (peek() == '-') {
            get();
        }
        if (peek() == '0') {
            get();
        } else if (isDigit(peek())) {
            while (isDigit(peek())) {
                get();
            }
        } else {
            failed_ = true;
            return value;
        }
        if (peek() == '.') {
            get();
            if (!isDigit(peek())) {
                failed_ = true;
                return value;
            }
            while (isDigit(peek())) {
                get();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') {
                get();
            }
            if (!isDigit(peek())) {
                failed_ = true;
                return value;
            }
            while (isDigit(peek())) {
                get();
            }
        }
        if (failed_ || index_ - begin > kMaxNumberLength) {
            failed_ = true;
            return value;
        }
        bool converted = false;
        value.number = convertNumber(input_.substr(begin, index_ - begin), converted);
        if (!converted) {
            failed_ = true;
        }
        return value;
    }

    /// 调用方拥有的原文。Parser 只在自身存活期间借用。
    std::string_view input_;

    /// 下一个尚未读取的字节下标。
    std::size_t index_{0};

    /// 为 true 后，本次解析结果作废。
    bool failed_{false};
};

double convertNumber(std::string_view token, bool& converted) {
    converted = false;
    if (token.empty()) {
        return 0;
    }
    std::size_t index = 0;
    double sign = 1;
    if (token[index] == '-') {
        sign = -1;
        ++index;
    }
    double magnitude = 0;
    if (index < token.size() && token[index] == '0') {
        ++index;
    } else {
        while (index < token.size() && isDigit(token[index])) {
            magnitude = magnitude * 10 + (token[index] - '0');
            ++index;
        }
    }
    if (index < token.size() && token[index] == '.') {
        ++index;
        double place = 0.1;
        while (index < token.size() && isDigit(token[index])) {
            magnitude += (token[index] - '0') * place;
            place *= 0.1;
            ++index;
        }
    }
    int exponentSign = 1;
    int exponent = 0;
    if (index < token.size() && (token[index] == 'e' || token[index] == 'E')) {
        ++index;
        if (index < token.size() && (token[index] == '+' || token[index] == '-')) {
            if (token[index] == '-') {
                exponentSign = -1;
            }
            ++index;
        }
        while (index < token.size() && isDigit(token[index])) {
            exponent = exponent * 10 + (token[index] - '0');
            if (exponent > 400) {
                return 0;
            }
            ++index;
        }
    }
    if (index != token.size()) {
        return 0;
    }
    double value = magnitude;
    for (int step = 0; step < exponent; ++step) {
        if (exponentSign > 0) {
            value *= 10;
        } else {
            value /= 10;
        }
    }
    value *= sign;
    if (!std::isfinite(value)) {
        return 0;
    }
    converted = true;
    return value;
}

/// @brief 在对象里按键名查找。
/// @param object kind 必须是 Object。
/// @param key 键名，只在本次调用期间借用。
/// @return 指向对象内部值的指针。键不存在时为空。
const Json* findField(const Json& object, std::string_view key) {
    for (std::size_t index = 0; index < object.fieldKeys.size(); ++index) {
        if (object.fieldKeys[index] == key) {
            return &object.fieldValues[index];
        }
    }
    return nullptr;
}

/// @brief 把 JSON 数字收成无符号 32 位整数。
/// @param value JSON 数字。
/// @param out 成功时写入整数。
/// @return 有限、非负、没有小数部分且不超过 uint32 时为 true。
bool readUint32(double value, std::uint32_t& out) {
    if (!std::isfinite(value) || value < 0 || value > 4294967295.0 || std::floor(value) != value) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

/// @brief 读取对象中的无符号整数字段。
/// @param object JSON 对象。
/// @param key 字段名。
/// @param out 成功时写入整数值。
/// @return 键存在且值是合法 uint32 时为 true。
bool readRequiredUint(const Json& object, std::string_view key, std::uint32_t& out) {
    const Json* field = findField(object, key);
    return field != nullptr && field->kind == Json::Kind::Number && readUint32(field->number, out);
}

/// @brief 读取对象中的非空字符串字段。
/// @param object JSON 对象。
/// @param key 字段名。
/// @param out 成功时写入文本副本。
/// @return 键存在、值是字符串且不是空白时为 true。
bool readRequiredString(const Json& object, std::string_view key, std::string& out) {
    const Json* field = findField(object, key);
    if (field == nullptr || field->kind != Json::Kind::String || isBlank(field->text)) {
        return false;
    }
    out = field->text;
    return true;
}

/// @brief 把 type 原文映射到三种标量。结构体名称返回空。
/// @param typeName manifest 的 type 文本。
/// @return float、int32_t、uint32_t 对应的 DataType。其他文本没有标量类型。
std::optional<DataType> scalarTypeFor(std::string_view typeName) {
    if (typeName == "float") {
        return DataType::Float32;
    }
    if (typeName == "int32_t") {
        return DataType::Int32;
    }
    if (typeName == "uint32_t") {
        return DataType::UInt32;
    }
    return std::nullopt;
}

/// @brief 检查已核验字段的几何是否落在镜像内，并且符合 4 字节 slot。
/// @param field 已索引字段。
/// @param base 布局起点，单位是字节。
/// @param totalSize 从 base 起的总字节数。
/// @param totalSlots 总 slot 数。
/// @return 几何一致时为空。越界或和 slot 宽度矛盾时为 OutOfRange。
std::optional<LayoutError> checkGeometry(
    const RawField& field,
    std::uint32_t base,
    std::uint32_t totalSize,
    std::uint32_t totalSlots) {
    if (field.offset < base || field.slotCount == 0) {
        return LayoutError::OutOfRange;
    }
    const std::uint32_t origin = field.offset - base;
    if (field.size > totalSize || origin > totalSize - field.size) {
        return LayoutError::OutOfRange;
    }
    if (field.slotCount > totalSlots || field.slot > totalSlots - field.slotCount) {
        return LayoutError::OutOfRange;
    }
    if (field.slotCount > (std::uint32_t{0xFFFFFFFF} / kSlotBytes)
        || field.size != field.slotCount * kSlotBytes) {
        return LayoutError::OutOfRange;
    }
    std::uint32_t product = 1;
    for (std::uint32_t dimension : field.shape) {
        if (dimension == 0 || product > (std::uint32_t{0xFFFFFFFF} / dimension)) {
            return LayoutError::OutOfRange;
        }
        product *= dimension;
    }
    if (!field.shape.empty() && product != field.slotCount) {
        return LayoutError::OutOfRange;
    }
    return std::nullopt;
}

/// @brief 读取一个字段对象，并递归读取它的 children。
/// @param object 字段 JSON 对象。
/// @param out 已收集的字段。路径重复时整份文档失败。
/// @return 字段结构合法且路径不重复时为空。
std::optional<LayoutError> readField(const Json& object, std::vector<RawField>& out) {
    if (object.kind != Json::Kind::Object) {
        return LayoutError::InvalidDocument;
    }
    RawField field;
    if (!readRequiredString(object, "path", field.path) || !readRequiredString(object, "type", field.typeName)
        || !readRequiredString(object, "status", field.status)
        || !readRequiredUint(object, "offset", field.offset) || !readRequiredUint(object, "size", field.size)
        || !readRequiredUint(object, "slot", field.slot)
        || !readRequiredUint(object, "slot_count", field.slotCount)) {
        return LayoutError::InvalidDocument;
    }
    const Json* writable = findField(object, "writable");
    if (writable == nullptr || writable->kind != Json::Kind::Bool) {
        return LayoutError::InvalidDocument;
    }
    field.writable = writable->boolean;
    const Json* shape = findField(object, "shape");
    if (shape == nullptr || shape->kind != Json::Kind::Array) {
        return LayoutError::InvalidDocument;
    }
    for (const Json& dimension : shape->elements) {
        std::uint32_t value = 0;
        if (dimension.kind != Json::Kind::Number || !readUint32(dimension.number, value)) {
            return LayoutError::InvalidDocument;
        }
        field.shape.push_back(value);
    }
    for (const RawField& existing : out) {
        if (existing.path == field.path) {
            return LayoutError::InvalidDocument;
        }
    }
    out.push_back(std::move(field));
    const Json* children = findField(object, "children");
    if (children == nullptr) {
        return std::nullopt;
    }
    if (children->kind != Json::Kind::Array) {
        return LayoutError::InvalidDocument;
    }
    for (const Json& child : children->elements) {
        if (const std::optional<LayoutError> error = readField(child, out)) {
            return error;
        }
    }
    return std::nullopt;
}

/// @brief 把根对象收成布局参数和字段列表。
/// @param root 解析后的 JSON 根值。
/// @param base 成功时写入字节起点。
/// @param totalSize 成功时写入总字节数。
/// @param totalSlots 成功时写入总 slot 数。
/// @param fields 成功时写入展平字段。
/// @return 文档结构合法时为空。
std::optional<LayoutError> readManifest(
    const Json& root,
    std::uint32_t& base,
    std::uint32_t& totalSize,
    std::uint32_t& totalSlots,
    std::vector<RawField>& fields) {
    if (root.kind != Json::Kind::Object) {
        return LayoutError::InvalidDocument;
    }
    std::uint32_t alignment = 0;
    if (!readRequiredUint(root, "alignment", alignment) || alignment != kSlotBytes
        || !readRequiredUint(root, "base", base) || !readRequiredUint(root, "total_size", totalSize)
        || !readRequiredUint(root, "total_slots", totalSlots)) {
        return LayoutError::InvalidDocument;
    }
    if (totalSlots > (std::uint32_t{0xFFFFFFFF} / kSlotBytes) || totalSize != totalSlots * kSlotBytes) {
        return LayoutError::InvalidDocument;
    }
    const Json* fieldArray = findField(root, "fields");
    if (fieldArray == nullptr || fieldArray->kind != Json::Kind::Array) {
        return LayoutError::InvalidDocument;
    }
    fields.clear();
    for (const Json& element : fieldArray->elements) {
        if (const std::optional<LayoutError> error = readField(element, fields)) {
            return error;
        }
    }
    return std::nullopt;
}

}  // namespace

FirmwareLayout::FirmwareLayout(
    std::uint32_t base,
    std::uint32_t totalSize,
    std::uint32_t totalSlots,
    std::vector<StoredField> fields)
    : base_{base}
    , totalSize_{totalSize}
    , totalSlots_{totalSlots}
    , fields_{std::move(fields)} {}

std::size_t FirmwareLayout::fieldCount() const noexcept {
    return fields_.size();
}

LayoutResolve FirmwareLayout::resolve(std::string_view firmwarePath) const {
    if (isBlank(firmwarePath)) {
        return LayoutResolve::failure(LayoutError::NotFound);
    }
    const StoredField* found = nullptr;
    for (const StoredField& field : fields_) {
        if (field.path == firmwarePath) {
            found = &field;
            break;
        }
    }
    if (found == nullptr) {
        return LayoutResolve::failure(LayoutError::NotFound);
    }
    if (found->status != kVerifiedStatus) {
        return LayoutResolve::failure(LayoutError::Unverified);
    }
    RawField geometry{
        found->path,
        found->offset,
        found->size,
        found->slot,
        found->slotCount,
        found->typeName,
        found->shape,
        found->status,
        found->writable};
    if (const std::optional<LayoutError> error = checkGeometry(geometry, base_, totalSize_, totalSlots_)) {
        return LayoutResolve::failure(*error);
    }
    FieldLayout layout;
    layout.slot = found->slot;
    layout.slotCount = found->slotCount;
    layout.size = found->size;
    layout.offset = found->offset;
    layout.typeName = found->typeName;
    layout.scalarType = scalarTypeFor(found->typeName);
    layout.writable = found->writable;
    return LayoutResolve::success(std::move(layout));
}

LayoutLoad FirmwareLayoutLoader::loadText(std::string_view jsonText) {
    Parser parser{jsonText};
    const std::optional<Json> root = parser.parseDocument();
    if (!root) {
        return LayoutLoad::failure(LayoutError::InvalidDocument);
    }
    std::uint32_t base = 0;
    std::uint32_t totalSize = 0;
    std::uint32_t totalSlots = 0;
    std::vector<RawField> rawFields;
    if (const std::optional<LayoutError> error =
            readManifest(*root, base, totalSize, totalSlots, rawFields)) {
        return LayoutLoad::failure(*error);
    }
    std::vector<FirmwareLayout::StoredField> fields;
    fields.reserve(rawFields.size());
    for (RawField& raw : rawFields) {
        FirmwareLayout::StoredField stored;
        stored.path = std::move(raw.path);
        stored.offset = raw.offset;
        stored.size = raw.size;
        stored.slot = raw.slot;
        stored.slotCount = raw.slotCount;
        stored.typeName = std::move(raw.typeName);
        stored.shape = std::move(raw.shape);
        stored.status = std::move(raw.status);
        stored.writable = raw.writable;
        fields.push_back(std::move(stored));
    }
    return LayoutLoad::success(FirmwareLayout{base, totalSize, totalSlots, std::move(fields)});
}

LayoutLoad FirmwareLayoutLoader::loadFile(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return LayoutLoad::failure(LayoutError::InvalidDocument);
    }
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        return LayoutLoad::failure(LayoutError::InvalidDocument);
    }
    return loadText(text);
}

}  // namespace ernc::domain
