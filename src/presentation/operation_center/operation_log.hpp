#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace ernc::presentation {

/// 操作中心的示例日志。它只保留有限行数，不写文件，也不是通信记录。
class OperationLog {
public:
    /// 最多保留的行数。超出后丢掉最旧的一行。单位是行，不是字节。
    static constexpr std::size_t kCapacity = 200;

    /// @brief 追加一行，并在超过上限时丢掉最旧的一行。
    /// @param line 一行文本。调用方拥有原件，本函数复制它。空文本也占一行。本函数不解析报文。
    void append(std::string_view line);

    /// @brief 当前保留的行数。
    /// @return 0 到 `kCapacity`。没有行时为 0。
    std::size_t size() const noexcept;

    /// @brief 借出当前保留的某一行。
    /// @param index 从 0 开始。0 是当前最旧的一行。
    /// @return 指向内部字符串的视图。越界时为空。下一次 `append` 后失效。
    std::string_view lineAt(std::size_t index) const;

    /// @brief 把当前保留的行拼成一段文本。
    /// @return 调用方拥有的文本。行之间用换行连接，最后一行后面不加换行。没有行时为空字符串。
    std::string text() const;

private:
    /// 按从旧到新排列的行。本对象拥有这些字符串。长度不超过 `kCapacity`。
    std::deque<std::string> lines_;
};

inline void OperationLog::append(std::string_view line) {
    if (lines_.size() == kCapacity) {
        lines_.pop_front();
    }
    lines_.emplace_back(line);
}

inline std::size_t OperationLog::size() const noexcept {
    return lines_.size();
}

inline std::string_view OperationLog::lineAt(std::size_t index) const {
    if (index >= lines_.size()) {
        return {};
    }
    return lines_[index];
}

inline std::string OperationLog::text() const {
    std::string joined;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (index != 0) {
            joined.push_back('\n');
        }
        joined.append(lines_[index]);
    }
    return joined;
}

} // namespace ernc::presentation
