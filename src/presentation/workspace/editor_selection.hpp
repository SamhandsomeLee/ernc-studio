#pragma once

#include <string>
#include <string_view>

namespace ernc::presentation {

/// 两种编辑视图共用的选择。这里只保存页面 id 和参数 id，不保存参数值。
/// 切换视图不能通过本对象清掉参数 id，因为本对象不记录当前打开的是哪一个视图。
class EditorSelection {
public:
    /// @brief 记住一个参数 id。
    /// @param parameterId 目录主键，只在本次调用期间借用。空文本被忽略，已有参数 id 保持不变。
    void selectParameter(std::string_view parameterId);

    /// @brief 记住当前页面，不改参数 id。
    /// @param pageId 项目树页面 id，只在本次调用期间借用。空文本被忽略，已有页面和参数 id 都保持不变。
    void selectPage(std::string_view pageId);

    /// @brief 借出当前参数 id。
    /// @return 内部字符串引用。没有选择时为空字符串，不表示 Off 或设备值。
    const std::string& parameterId() const noexcept;

    /// @brief 借出当前页面 id。
    /// @return 内部字符串引用。没有选择时为空字符串。
    const std::string& pageId() const noexcept;

private:
    /// 最近一次选中的参数 id。空表示还没有选中参数。
    std::string parameterId_;

    /// 最近一次选中的页面 id。空表示还没有选中页面。它的变化不清除 `parameterId_`。
    std::string pageId_;
};

inline void EditorSelection::selectParameter(std::string_view parameterId) {
    if (parameterId.empty()) {
        return;
    }
    parameterId_.assign(parameterId);
}

inline void EditorSelection::selectPage(std::string_view pageId) {
    if (pageId.empty()) {
        return;
    }
    pageId_.assign(pageId);
}

inline const std::string& EditorSelection::parameterId() const noexcept {
    return parameterId_;
}

inline const std::string& EditorSelection::pageId() const noexcept {
    return pageId_;
}

} // namespace ernc::presentation
