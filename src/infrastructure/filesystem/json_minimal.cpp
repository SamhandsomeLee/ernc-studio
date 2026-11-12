#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ernc::infrastructure::json_minimal {

constexpr int kMaxJsonDepth = 32;
constexpr std::size_t kMaxNumberLength = 64;

struct Json {
    /// 当前激活的备选。Null 只表示 JSON null，不表示固件路径缺失。
    enum class Kind { Null, Bool, Number, String, Array, Object };

    /// 当前备选。其余成员只有在对应 Kind 下才有意义。
    Kind kind{Kind::Null};

    /// kind 为 Bool 时的值。
    bool boolean{false};

    /// kind 为 Number 时的有限数值。它还不是参数线值，没有物理单位。
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

/// @brief 扫描一段目录 JSON。失败时不产生部分值给目录校验器。
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

    /// @brief 解析对象。重复键直接失败，避免后出现的键悄悄覆盖路径或 id。
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
/// @return 转换结果。失败时返回 0，调用方不能把它当作目录里的合法线值。
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
/// @param text 候选路径或说明，只在本次调用期间借用。
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

}  // namespace ernc::infrastructure::json_minimal
