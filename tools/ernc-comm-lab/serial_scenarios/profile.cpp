#include "profile.hpp"

#include <fstream>
#include <limits>
#include <string_view>

namespace ernc::lab {

namespace {

/// 一次 FC03 最多携带的 slot 数。更长的只读白名单不能当成一次请求。
constexpr int kMaxReadSlots = 62;

/// 一次 FC16 最多携带的 slot 数。更长的写白名单不能当成一次请求。
constexpr int kMaxWriteSlots = 61;

/// 档案正文上限，单位是字节。更大的文件直接拒绝。
constexpr std::streamsize kMaxProfileBytes = 65536;

/// @brief 档案解析过程中见过哪些键。
struct SeenKeys {
    /// 出现过 port。
    bool port{false};

    /// 出现过 baudRate。
    bool baudRate{false};

    /// 出现过 slave。
    bool slave{false};

    /// 出现过 readSlots。
    bool readSlots{false};

    /// 出现过 writeSlots。缺省表示写白名单为空。
    bool writeSlots{false};
};

/// @brief 只接受空格、制表和换行。
/// @param value 档案里的一个字节。
/// @return 属于这四类空白时为 true。
bool isJsonSpace(unsigned char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

/// @brief 判断 slot 列表是否落在单包上限内，并且没有负数或重复。
/// @param slots 档案里的槽位，只在本次调用期间借用。
/// @param maxCount 这一类请求允许的最大个数。
/// @param allowEmpty 写白名单可以为空。只读白名单不可以为空。
/// @return 列表符合这三项时为 true。
bool slotsFit(const std::vector<int>& slots, int maxCount, bool allowEmpty) {
    if (slots.empty()) {
        return allowEmpty;
    }
    if (static_cast<int>(slots.size()) > maxCount) {
        return false;
    }
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index] < 0) {
            return false;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (slots[earlier] == slots[index]) {
                return false;
            }
        }
    }
    return true;
}

/// @brief 必填项齐、从站地址和两类白名单都合法时，档案才可用。
/// @param profile 已经填入字段的档案，只在本次调用期间借用。
/// @param seen 解析时见过的键，只在本次调用期间借用。
/// @return 可以进入打开端口这一步时为 true。此处仍不打开端口。
bool profileUsable(const SerialProfile& profile, const SeenKeys& seen) {
    if (!seen.port || !seen.baudRate || !seen.slave || !seen.readSlots) {
        return false;
    }
    if (profile.port.empty() || profile.baudRate <= 0) {
        return false;
    }
    if (profile.slave < 1 || profile.slave > 247) {
        return false;
    }
    if (!slotsFit(profile.readSlots, kMaxReadSlots, false)) {
        return false;
    }
    return slotsFit(profile.writeSlots, kMaxWriteSlots, true);
}

/// @brief 扫描一份没有嵌套对象的档案。
class Scanner {
public:
    /// @brief 绑定档案正文。正文只在扫描期间借用。
    /// @param text 文件里的全部字节。
    explicit Scanner(std::string_view text) : text_(text) {}

    /// @brief 解析单一对象并检查必填项。
    /// @param profile 成功时写入可用档案。调用方拥有。
    /// @return 正文被完整消费且档案可用时为 true。
    bool parse(SerialProfile& profile) {
        if (!eat('{')) {
            return false;
        }
        skip();
        if (index_ < text_.size() && text_[index_] == '}') {
            ++index_;
            return false;
        }
        for (;;) {
            std::string key;
            if (!parseString(key) || !eat(':') || !assign(key, profile)) {
                return false;
            }
            skip();
            if (index_ < text_.size() && text_[index_] == ',') {
                ++index_;
                continue;
            }
            break;
        }
        if (!eat('}')) {
            return false;
        }
        skip();
        if (index_ != text_.size()) {
            return false;
        }
        profile.usable = profileUsable(profile, seen_);
        return profile.usable;
    }

private:
    /// @brief 跳过空白。
    /// @return 跳过后仍有剩余字节时为 true。文件结束时为 false。
    bool skip() {
        while (index_ < text_.size() && isJsonSpace(static_cast<unsigned char>(text_[index_]))) {
            ++index_;
        }
        return index_ < text_.size();
    }

    /// @brief 跳过空白后消费一个指定字符。
    /// @param expected 期望的下一个有效字符。
    /// @return 对上时为 true，并前进一个字节。对不上时不前进。
    bool eat(char expected) {
        skip();
        if (index_ >= text_.size() || text_[index_] != expected) {
            return false;
        }
        ++index_;
        return true;
    }

    /// @brief 读取一个不含转义的 JSON 字符串。
    /// @param out 成功时写入字符内容。调用方拥有。失败时内容无意义。
    /// @return 引号成对且中间没有控制字符或反斜杠时为 true。
    bool parseString(std::string& out) {
        if (!eat('"')) {
            return false;
        }
        out.clear();
        while (index_ < text_.size()) {
            const unsigned char value = static_cast<unsigned char>(text_[index_]);
            ++index_;
            if (value == '"') {
                return true;
            }
            if (value == '\\' || value < 0x20 || value > 0x7E) {
                return false;
            }
            out.push_back(static_cast<char>(value));
        }
        return false;
    }

    /// @brief 读取一个非负十进制整数。
    /// @param out 成功时写入该整数。调用方拥有。
    /// @return 至少一位数字且不超过 int 上限时为 true。
    bool parseInt(int& out) {
        skip();
        if (index_ >= text_.size() || text_[index_] < '0' || text_[index_] > '9') {
            return false;
        }
        long long value = 0;
        while (index_ < text_.size() && text_[index_] >= '0' && text_[index_] <= '9') {
            value = value * 10 + (text_[index_] - '0');
            if (value > std::numeric_limits<int>::max()) {
                return false;
            }
            ++index_;
        }
        out = static_cast<int>(value);
        return true;
    }

    /// @brief 读取整数数组。空数组也是合法语法。
    /// @param out 成功时写入全部整数。调用方拥有。
    /// @return 方括号匹配且每个元素都是整数时为 true。
    bool parseIntArray(std::vector<int>& out) {
        if (!eat('[')) {
            return false;
        }
        out.clear();
        skip();
        if (index_ < text_.size() && text_[index_] == ']') {
            ++index_;
            return true;
        }
        for (;;) {
            int value = 0;
            if (!parseInt(value)) {
                return false;
            }
            out.push_back(value);
            skip();
            if (index_ < text_.size() && text_[index_] == ',') {
                ++index_;
                continue;
            }
            if (index_ < text_.size() && text_[index_] == ']') {
                ++index_;
                return true;
            }
            return false;
        }
    }

    /// @brief 按键名写入一个字段。重复键和未知键都拒绝。
    /// @param key 不含引号的键名，只在本次调用期间借用。
    /// @param profile 正在填写的档案。调用方拥有。
    /// @return 键被接受时为 true。
    bool assign(const std::string& key, SerialProfile& profile) {
        if (key == "port") {
            if (seen_.port || !parseString(profile.port)) {
                return false;
            }
            seen_.port = true;
            return true;
        }
        if (key == "baudRate") {
            int value = 0;
            if (seen_.baudRate || !parseInt(value)) {
                return false;
            }
            profile.baudRate = value;
            seen_.baudRate = true;
            return true;
        }
        if (key == "slave") {
            if (seen_.slave || !parseInt(profile.slave)) {
                return false;
            }
            seen_.slave = true;
            return true;
        }
        if (key == "readSlots") {
            if (seen_.readSlots || !parseIntArray(profile.readSlots)) {
                return false;
            }
            seen_.readSlots = true;
            return true;
        }
        if (key == "writeSlots") {
            if (seen_.writeSlots || !parseIntArray(profile.writeSlots)) {
                return false;
            }
            seen_.writeSlots = true;
            return true;
        }
        return false;
    }

    /// 档案正文。只在本对象存活期间借用。
    std::string_view text_;

    /// 下一个要读的字节下标。
    std::size_t index_{0};

    /// 已经接受的键。
    SeenKeys seen_;
};

}  // namespace

bool loadSerialProfile(const std::string& path, SerialProfile& profile) {
    profile = SerialProfile{};
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }
    std::string text(static_cast<std::size_t>(kMaxProfileBytes) + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize got = file.gcount();
    if (got <= 0 || got > kMaxProfileBytes) {
        return false;
    }
    text.resize(static_cast<std::size_t>(got));
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        return false;
    }
    Scanner scanner{text};
    return scanner.parse(profile);
}

}  // namespace ernc::lab
