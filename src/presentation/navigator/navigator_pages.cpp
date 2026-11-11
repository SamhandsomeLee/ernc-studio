#include "navigator/navigator_pages.hpp"

namespace ernc::presentation {
namespace {

static_assert(kNavigatorPageIds[std::size(kNavigatorPageIds) - 1] == ernc::domain::kOtherPageCategory);

/// @brief 写入失败结果并清空页面。
/// @param error 失败原因。调用方拥有原件，本函数复制它。
/// @return `ok` 为 false 且没有页面的投影。
NavigatorProjection failedProjection(std::string error) {
    NavigatorProjection projection;
    projection.ok = false;
    projection.error = std::move(error);
    return projection;
}

/// @brief 按页面 id 查找投影中的页面。
/// @param pages 正在组装的页面，只在本次调用期间借用。
/// @param id 页面 id，只在本次调用期间借用。
/// @return 找到时指向 `pages` 里的元素。没有这个页面时为空指针。
NavigatorPage* findPage(std::vector<NavigatorPage>& pages, std::string_view id) {
    for (NavigatorPage& page : pages) {
        if (page.id == id) {
            return &page;
        }
    }
    return nullptr;
}

} // namespace

NavigatorProjection projectNavigatorPages(const ernc::domain::ParameterCatalog& catalog) {
    NavigatorProjection projection;
    projection.ok = true;
    projection.pages.reserve(std::size(kNavigatorPageIds));
    for (const std::string_view pageId : kNavigatorPageIds) {
        projection.pages.push_back(NavigatorPage{pageId, {}});
    }

    for (const ernc::domain::ParameterDefinition& parameter : catalog.entries()) {
        const std::string& parameterId = parameter.id.text();
        if (!parameter.category.has_value() || parameter.category->empty()) {
            return failedProjection("参数“" + parameterId + "”没有页面分类。");
        }
        const std::string& category = *parameter.category;
        NavigatorPage* page = findPage(projection.pages, category);
        if (page == nullptr || page->id == "ALL") {
            return failedProjection("参数“" + parameterId + "”的分类“" + category + "”不是项目树入口。");
        }
        page->parameters.push_back(&parameter);
    }
    return projection;
}

} // namespace ernc::presentation
