#pragma once

#include "ernc/domain/catalog.hpp"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::presentation {

/// 项目树的固定页面。顺序是设计第 3.2 节的八个入口，最后加上 `other`。
/// `ALL` 不是目录分类。这些文本是稳定 id，不是另一份参数表。
inline constexpr std::string_view kNavigatorPageIds[] = {
    "ALL",
    "ERNC_Flag",
    "ERNC_IN_OUT",
    "Check",
    "ENC_Param",
    "RNC_Param",
    "Threshold",
    "ERNC_Switch_State",
    "other",
};

/// 一个项目树页面，以及目录里归到这个页面的参数。
/// 参数指针借用 `ParameterCatalog` 内部记录，目录被移动或销毁后失效。
struct NavigatorPage {
    /// 页面稳定 id，指向 `kNavigatorPageIds` 里的字面量。
    std::string_view id;

    /// 该页面下的参数，顺序与目录文件一致。`ALL` 不放参数。
    std::vector<const ernc::domain::ParameterDefinition*> parameters;
};

/// 目录到项目树的投影。失败时 `pages` 为空，不能当成九个空入口。
struct NavigatorProjection {
    /// 为 true 时 `pages` 含全部固定页面。为 false 时调用方只能显示 `error`。
    bool ok = false;

    /// 失败原因。成功时为空。它说明分类无法放入固定入口，不是设备状态。
    std::string error;

    /// 成功时按 `kNavigatorPageIds` 排列。失败时为空。
    std::vector<NavigatorPage> pages;
};

/// @brief 把一份已加载目录投影成项目树页面。
/// @param catalog 参数目录，只在本次调用期间借用。返回的参数指针在该目录对象存活且不再移动期间有效。
/// @return 成功时包含九个页面。参数没有分类，或分类不是这些页面之一时失败，并且不返回部分页面。
/// @note 不读取 Excel，不访问设备。`ALL` 下面不重复挂参数。
NavigatorProjection projectNavigatorPages(const ernc::domain::ParameterCatalog& catalog);

} // namespace ernc::presentation
