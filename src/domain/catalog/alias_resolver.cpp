#include "ernc/domain/alias_resolver.hpp"

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

/// 嵌套数组和对象的层数上限。别名文档只有三层；超过上限的文本按非法文档拒绝。
constexpr int kMaxJsonDepth = 32;

/// 单个 JSON 数字的最大字符数。schema 版本用得到；超长数字不转成 double。
constexpr std::size_t kMaxNumberLength = 64;

/// 本文件内部使用的 JSON 值。只服务别名 schema，不对外暴露，也不接受 Excel。
struct Json {
    /// 当前激活的备选。Null 只表示 JSON null，不表示未绑定路径。
    enum class Kind { Null, Bool, Number, String, Array, Object };

    /// 当前备选。其余成员只有在对应 Kind 下才有意义。
    Kind kind{Kind::Null};

    /// kind 为 Bool 时的值。
    bool boolean{false};

    /// kind 为 Number 时的有限数值。这里只用于 schema 版本，没有物理单位。
    double number{0};

    /// kind 为 String 时拥有的 UTF-8 文本。
    std::string text;

    /// kind 为 Array 时拥有的元素，顺序与文本一致。
    std::vector<Json> elements;

    /// kind 为 Object 时拥有的键值。同一对象内的键不重复，先出现的键不会被覆盖。
    std::vector<std::pair<std::string, Json>> fields;
};

/// @brief 判断字符是不是 JSON 空白。
/// @param character 单个输入字节。不按当前 locale 解释。
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

/// @brief 把一个 Unicode 码点追加为 UTF-8。
/// @param out 接收字节的字符串，由调用方拥有。
/// @param codePoint 已排除代理项的码点。大于 U+10FFFF 时不写入。
/// @return 码点可以编码时为 true。
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

double convertNumber(std::string_view token, bool& converted);

/// @brief 扫描一段别名 JSON。失败时不产生部分值给别名校验器。
class Parser {
public:
    /// @brief 绑定一段待解析文本。
    /// @param input UTF-8 JSON 文本，只在 Parser 存活期间借用。
    explicit Parser(std::string_view input) : input_(input) {}

    /// @brief 解析整段文本中的唯一 JSON 值。
    /// @return 成功时返回根值。语法错误、超深度、对象内重复键或尾部多余字符时为空。
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
    /// @brief 跳过文件开头的 UTF-8 BOM。BOM 不是 JSON 值的一部分。
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
    /// @return 当前字节；已经到末尾时返回 NUL。NUL 只作为哨兵，不代表文本里编码了空字符。
    char peek() const {
        return index_ < input_.size() ? input_[index_] : '\0';
    }

    /// @brief 读取并前进一个字节。
    /// @return 被消费的字节；末尾时返回 NUL 并标记失败。
    char get() {
        if (index_ >= input_.size()) {
            failed_ = true;
            return '\0';
        }
        return input_[index_++];
    }

    /// @brief 按当前深度解析一个 JSON 值。
    /// @param depth 已进入的数组和对象层数。根值的深度为 0。
    /// @return 解析出的值。失败时返回空对象，调用方只查看 failed_。
    Json parseValue(int depth) {
        if (failed_) {
            return {};
        }
        if (depth > kMaxJsonDepth) {
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

    /// @brief 解析对象。重复键直接失败，避免后出现的键悄悄覆盖名称或路径。
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
            for (const auto& field : object.fields) {
                if (field.first == key.text) {
                    failed_ = true;
                    return object;
                }
            }
            object.fields.emplace_back(std::move(key.text), std::move(value));
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

    /// @brief 解析 JSON 字符串。未转义的控制字节会被拒绝；UTF-8 多字节原样保留。
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

    /// @brief 读取 \u 后面的四个十六进制位并追加 UTF-8。
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
    /// @return kind 为 Number 的值。超过长度限制或结果非有限值时失败。
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

    /// 调用方拥有的原文。Parser 只在自身存活期间借用，不复制整段文本。
    std::string_view input_;

    /// 下一个尚未读取的字节下标。等于 input_.size() 时表示已经读完。
    std::size_t index_{0};

    /// 为 true 后，本次解析的返回值作废，调用方必须丢掉它。
    bool failed_{false};
};

/// @brief 把已经符合 JSON 数字语法的片段换成 double。
/// @param token 不含空白的数字片段，只在本次调用期间借用。
/// @param converted 成功时设为 true。语法之外的片段或结果不是有限值时设为 false。
/// @return 转换结果。失败时返回 0，调用方不能把它当作合法的 schema 版本。
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
/// @return 指向对象内部值的指针。键不存在时为空，不转移所有权。
const Json* findField(const Json& object, std::string_view key) {
    for (const auto& field : object.fields) {
        if (field.first == key) {
            return &field.second;
        }
    }
    return nullptr;
}

/// @brief 判断文本是否没有可见内容。
/// @param text 候选名称、路径或原因，只在本次调用期间借用。
/// @return 空文本或只含 JSON 空白时为 true。
bool isBlank(std::string_view text) {
    for (char character : text) {
        if (!isJsonSpace(character)) {
            return false;
        }
    }
    return true;
}

/// @brief 判断数字是不是某个精确整数。
/// @param value JSON 数字转换后的值。
/// @param expected 期望的整数，当前只用于 schema 版本 1。
/// @return 有限且与期望整数完全相等时为 true。
bool isExactInteger(double value, int expected) {
    return std::isfinite(value) && value == static_cast<double>(expected);
}

/// @brief 判断别名对象的键是否属于本 schema。
/// @param key 对象键名。
/// @return 键被本切片承认时为 true。拼写错误的键返回 false，避免把多余字段读成默认值。
bool isAliasKey(std::string_view key) {
    return key == "name" || key == "category" || key == "status" || key == "firmware_path"
        || key == "shared" || key == "reason";
}

/// @brief 读取对象中的字符串字段。
/// @param object JSON 对象。
/// @param key 字段名。
/// @param text 成功时写入字段文本的副本。
/// @param present 键存在时设为 true，包括类型不正确的键。
/// @return 键不存在，或存在且值是字符串时为 true。类型不正确时为 false。
bool readStringField(const Json& object, std::string_view key, std::string& text, bool& present) {
    const Json* field = findField(object, key);
    present = field != nullptr;
    if (field == nullptr) {
        return true;
    }
    if (field->kind != Json::Kind::String) {
        return false;
    }
    text = field->text;
    return true;
}

/// @brief 判断 manifest 字段树里是否有这条路径。
/// @param layout 已载入的布局，只在本次调用期间借用。
/// @param firmwarePath 候选路径，只在本次调用期间借用。调用前已经排除空白。
/// @return resolve 不是 NotFound 时为 true。Unverified 和 OutOfRange 都表示路径存在，不把它们改成已核验。
bool manifestContainsPath(const FirmwareLayout& layout, std::string_view firmwarePath) {
    const LayoutResolve resolved = layout.resolve(firmwarePath);
    const LayoutError* error = resolved.tryError();
    if (error == nullptr) {
        return true;
    }
    return *error != LayoutError::NotFound;
}

/// @brief 把一条 JSON 对象收成别名记录。此时还不检查路径是否存在，也不检查跨记录冲突。
/// @param object aliases 数组中的一个元素。
/// @param record 成功时写入完整记录。失败时内容不作数。
/// @return 字段组合合法时为空。重复名称不在这里判断。
std::optional<AliasError> readRecord(const Json& object, AliasRecord& record) {
    if (object.kind != Json::Kind::Object) {
        return AliasError::InvalidDocument;
    }
    for (const auto& field : object.fields) {
        if (!isAliasKey(field.first)) {
            return AliasError::InvalidDocument;
        }
    }

    std::string name;
    bool namePresent = false;
    if (!readStringField(object, "name", name, namePresent) || !namePresent || isBlank(name)) {
        return AliasError::InvalidDocument;
    }

    std::optional<std::string> category;
    std::string categoryText;
    bool categoryPresent = false;
    if (!readStringField(object, "category", categoryText, categoryPresent)) {
        return AliasError::InvalidDocument;
    }
    if (categoryPresent) {
        if (isBlank(categoryText)) {
            return AliasError::InvalidDocument;
        }
        category = std::move(categoryText);
    }

    std::string statusText;
    bool statusPresent = false;
    if (!readStringField(object, "status", statusText, statusPresent) || !statusPresent) {
        return AliasError::InvalidDocument;
    }

    std::string pathText;
    bool pathPresent = false;
    if (!readStringField(object, "firmware_path", pathText, pathPresent)) {
        return AliasError::InvalidDocument;
    }

    const Json* sharedField = findField(object, "shared");
    if (sharedField != nullptr && sharedField->kind != Json::Kind::Bool) {
        return AliasError::InvalidDocument;
    }
    const bool sharedPresent = sharedField != nullptr;
    const bool shared = sharedPresent && sharedField->boolean;

    std::string reasonText;
    bool reasonPresent = false;
    if (!readStringField(object, "reason", reasonText, reasonPresent)) {
        return AliasError::InvalidDocument;
    }

    AliasRecord parsed;
    parsed.name = std::move(name);
    parsed.category = std::move(category);
    if (statusText == "unique") {
        if (!pathPresent || isBlank(pathText) || reasonPresent) {
            return AliasError::InvalidDocument;
        }
        parsed.resolution = AliasResolution::Unique;
        parsed.firmwarePath = std::move(pathText);
        parsed.shared = shared;
    } else if (statusText == "unresolved") {
        if (pathPresent || sharedPresent || !reasonPresent || isBlank(reasonText)) {
            return AliasError::InvalidDocument;
        }
        parsed.resolution = AliasResolution::Unresolved;
        parsed.reason = std::move(reasonText);
    } else {
        return AliasError::InvalidDocument;
    }
    record = std::move(parsed);
    return std::nullopt;
}

/// @brief 确认同一路径的多条绑定都显式共享，并且单独一条不能自称共享。
/// @param entries 已经读完的记录。路径是否存在由调用方先检查。
/// @return 绑定关系合法时为空。静默覆盖或孤立的 shared 标志返回对应错误。
std::optional<AliasError> checkSharedPaths(const std::vector<AliasRecord>& entries) {
    for (const AliasRecord& record : entries) {
        if (record.resolution != AliasResolution::Unique || !record.firmwarePath) {
            continue;
        }
        std::size_t users = 0;
        bool allShared = true;
        for (const AliasRecord& other : entries) {
            if (other.resolution != AliasResolution::Unique || !other.firmwarePath
                || *other.firmwarePath != *record.firmwarePath) {
                continue;
            }
            ++users;
            if (!other.shared) {
                allShared = false;
            }
        }
        if (users >= 2 && !allShared) {
            return AliasError::PathConflict;
        }
        if (record.shared && users < 2) {
            return AliasError::InvalidDocument;
        }
    }
    return std::nullopt;
}

/// @brief 校验别名根对象。
/// @param root 解析后的 JSON 根值。
/// @param layout 用于确认 unique 路径出现在字段树里，只在本次调用期间借用。
/// @param schemaVersion 成功时写入 1。
/// @param entries 成功时写入全部记录。失败时内容不作数。
/// @return 文档可以整份接受时为空。
std::optional<AliasError> readAliasDocument(
    const Json& root,
    const FirmwareLayout& layout,
    int& schemaVersion,
    std::vector<AliasRecord>& entries) {
    if (root.kind != Json::Kind::Object) {
        return AliasError::InvalidDocument;
    }
    for (const auto& field : root.fields) {
        if (field.first != "schema_version" && field.first != "aliases") {
            return AliasError::InvalidDocument;
        }
    }
    const Json* schema = findField(root, "schema_version");
    const Json* aliases = findField(root, "aliases");
    if (schema == nullptr || schema->kind != Json::Kind::Number || !isExactInteger(schema->number, 1)
        || aliases == nullptr || aliases->kind != Json::Kind::Array) {
        return AliasError::InvalidDocument;
    }

    std::vector<AliasRecord> parsed;
    parsed.reserve(aliases->elements.size());
    for (const Json& element : aliases->elements) {
        AliasRecord record;
        if (const std::optional<AliasError> error = readRecord(element, record)) {
            return error;
        }
        for (const AliasRecord& existing : parsed) {
            if (existing.name == record.name) {
                return AliasError::DuplicateName;
            }
        }
        if (record.resolution == AliasResolution::Unique
            && !manifestContainsPath(layout, *record.firmwarePath)) {
            return AliasError::UnknownPath;
        }
        parsed.push_back(std::move(record));
    }
    if (const std::optional<AliasError> error = checkSharedPaths(parsed)) {
        return error;
    }
    schemaVersion = 1;
    entries = std::move(parsed);
    return std::nullopt;
}

}  // namespace

ParameterAliasTable::ParameterAliasTable(int schemaVersion, std::vector<AliasRecord> entries)
    : schemaVersion_{schemaVersion}
    , entries_{std::move(entries)} {}

const AliasRecord* ParameterAliasTable::find(std::string_view name) const noexcept {
    for (const AliasRecord& record : entries_) {
        if (record.name == name) {
            return &record;
        }
    }
    return nullptr;
}

std::vector<const AliasRecord*> ParameterAliasTable::findByCategory(std::string_view category) const {
    std::vector<const AliasRecord*> found;
    if (category.empty()) {
        return found;
    }
    for (const AliasRecord& record : entries_) {
        if (record.category && *record.category == category) {
            found.push_back(&record);
        }
    }
    return found;
}

std::size_t ParameterAliasTable::size() const noexcept {
    return entries_.size();
}

const std::vector<AliasRecord>& ParameterAliasTable::entries() const noexcept {
    return entries_;
}

int ParameterAliasTable::schemaVersion() const noexcept {
    return schemaVersion_;
}

AliasLoad ParameterAliasLoader::loadText(std::string_view jsonText, const FirmwareLayout& layout) {
    Parser parser{jsonText};
    const std::optional<Json> root = parser.parseDocument();
    if (!root) {
        return AliasLoad::failure(AliasError::InvalidDocument);
    }
    int schemaVersion = 0;
    std::vector<AliasRecord> entries;
    if (const std::optional<AliasError> error = readAliasDocument(*root, layout, schemaVersion, entries)) {
        return AliasLoad::failure(*error);
    }
    return AliasLoad::success(ParameterAliasTable{schemaVersion, std::move(entries)});
}

AliasLoad ParameterAliasLoader::loadFile(
    const std::filesystem::path& path,
    const FirmwareLayout& layout) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return AliasLoad::failure(AliasError::InvalidDocument);
    }
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        return AliasLoad::failure(AliasError::InvalidDocument);
    }
    return loadText(text, layout);
}

}  // namespace ernc::domain
