#include "ernc/domain/catalog.hpp"
#include "navigator/navigator_pages.hpp"

#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterDefinition;
using ernc::presentation::NavigatorPage;
using ernc::presentation::NavigatorProjection;
using ernc::presentation::kNavigatorPageIds;
using ernc::presentation::projectNavigatorPages;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 包一份最小目录。
/// @param parameters parameters 数组内部的 JSON，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的目录 JSON。
std::string catalogText(std::string_view parameters) {
    std::string text = R"({"schema_version":1,"catalog_version":"0.2.0","parameters":[)";
    text.append(parameters);
    text.append("]}");
    return text;
}

/// @brief 按页面 id 查找投影页面。
/// @param projection 成功的投影，只在本次调用期间借用。
/// @param id 页面 id，只在本次调用期间借用。
/// @return 找到时指向投影内部页面。没有这个页面时为空指针。
const NavigatorPage* findPage(const NavigatorProjection& projection, std::string_view id) {
    for (const NavigatorPage& page : projection.pages) {
        if (page.id == id) {
            return &page;
        }
    }
    return nullptr;
}

/// @brief 页面参数里是否有这个 id。
/// @param page 一个页面，只在本次调用期间借用。
/// @param id 参数 id，只在本次调用期间借用。
/// @return 出现至少一次时为 true。
bool pageContains(const NavigatorPage& page, std::string_view id) {
    for (const ParameterDefinition* parameter : page.parameters) {
        if (parameter != nullptr && parameter->id.text() == id) {
            return true;
        }
    }
    return false;
}

/// @brief 载入随仓库发布的目录。
/// @param path 目录文件路径，只在本次调用期间借用。
/// @param catalog 成功时写入目录副本。
/// @return 文件能通过加载器时为 true。
bool loadShipped(const char* path, ParameterCatalog& catalog) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(path);
    if (!loaded.ok()) {
        std::cerr << "cannot load catalog " << path << '\n';
        return false;
    }
    catalog = loaded.catalog();
    return true;
}

/// @brief 九个页面的 id 和顺序与设计入口一致。
/// @param catalogPath 发布目录路径，只在本次调用期间借用。
/// @return 投影成功且顺序一致时为 true。
bool pageOrder(const char* catalogPath) {
    ParameterCatalog catalog = ParameterCatalogLoader::loadText(catalogText("")).catalog();
    if (!loadShipped(catalogPath, catalog)) {
        return fail("page_order");
    }
    const NavigatorProjection projection = projectNavigatorPages(catalog);
    if (!projection.ok || projection.pages.size() != std::size(kNavigatorPageIds)) {
        return fail("page_order");
    }
    for (std::size_t i = 0; i < std::size(kNavigatorPageIds); ++i) {
        if (projection.pages[i].id != kNavigatorPageIds[i]) {
            return fail("page_order");
        }
    }
    return true;
}

/// @brief `check_data` 在 `other`，`check_date` 不在任何页面。
/// @param catalogPath 发布目录路径，只在本次调用期间借用。
/// @return 两条约束都成立时为 true。
bool checkDataNotDate(const char* catalogPath) {
    ParameterCatalog catalog = ParameterCatalogLoader::loadText(catalogText("")).catalog();
    if (!loadShipped(catalogPath, catalog)) {
        return fail("check_data_not_date");
    }
    if (catalog.find("check_date") != nullptr || catalog.find("check_data") == nullptr) {
        return fail("check_data_not_date");
    }
    const NavigatorProjection projection = projectNavigatorPages(catalog);
    const NavigatorPage* other = projection.ok ? findPage(projection, "other") : nullptr;
    const NavigatorPage* check = projection.ok ? findPage(projection, "Check") : nullptr;
    if (other == nullptr || check == nullptr || !pageContains(*other, "check_data")
        || pageContains(*check, "check_data") || pageContains(*other, "check_date")) {
        return fail("check_data_not_date");
    }
    return true;
}

/// @brief 状态入口保持为空，同名参数留在 `other`。
/// @param catalogPath 发布目录路径，只在本次调用期间借用。
/// @return 页面没有子项且参数在 `other` 时为 true。
bool switchStateSplit(const char* catalogPath) {
    ParameterCatalog catalog = ParameterCatalogLoader::loadText(catalogText("")).catalog();
    if (!loadShipped(catalogPath, catalog)) {
        return fail("switch_state_split");
    }
    const NavigatorProjection projection = projectNavigatorPages(catalog);
    const NavigatorPage* page = projection.ok ? findPage(projection, "ERNC_Switch_State") : nullptr;
    const NavigatorPage* other = projection.ok ? findPage(projection, "other") : nullptr;
    if (page == nullptr || other == nullptr || !page->parameters.empty()
        || !pageContains(*other, "ERNC_Switch_State")) {
        return fail("switch_state_split");
    }
    return true;
}

/// @brief 每条目录参数恰好出现在一个非 ALL 页面下，顺序与文件一致。
/// @param catalogPath 发布目录路径，只在本次调用期间借用。
/// @return 没有遗漏、重复或点分层时为 true。
bool parametersOnceInFileOrder(const char* catalogPath) {
    ParameterCatalog catalog = ParameterCatalogLoader::loadText(catalogText("")).catalog();
    if (!loadShipped(catalogPath, catalog)) {
        return fail("parameters_once_in_file_order");
    }
    const NavigatorProjection projection = projectNavigatorPages(catalog);
    if (!projection.ok) {
        return fail("parameters_once_in_file_order");
    }
    const NavigatorPage* all = findPage(projection, "ALL");
    if (all == nullptr || !all->parameters.empty()) {
        return fail("parameters_once_in_file_order");
    }

    std::vector<std::string> seen;
    for (const NavigatorPage& page : projection.pages) {
        if (page.id == "ALL") {
            continue;
        }
        std::size_t cursor = 0;
        for (const ParameterDefinition* parameter : page.parameters) {
            if (parameter == nullptr) {
                return fail("parameters_once_in_file_order");
            }
            const std::string& id = parameter->id.text();
            bool duplicate = false;
            for (const std::string& previous : seen) {
                if (previous == id) {
                    duplicate = true;
                }
            }
            if (duplicate) {
                return fail("parameters_once_in_file_order");
            }
            seen.push_back(id);
            bool foundLater = false;
            for (std::size_t i = cursor; i < catalog.entries().size(); ++i) {
                if (&catalog.entries()[i] == parameter) {
                    cursor = i + 1;
                    foundLater = true;
                    break;
                }
            }
            if (!foundLater) {
                return fail("parameters_once_in_file_order");
            }
        }
    }
    if (seen.size() != catalog.size()) {
        return fail("parameters_once_in_file_order");
    }
    const NavigatorPage* other = findPage(projection, "other");
    if (other == nullptr || !pageContains(*other, "check_data.OutputCheck")) {
        return fail("parameters_once_in_file_order");
    }
    return true;
}

/// @brief 固定入口以外的分类拒绝整份投影。
/// @param catalogPath 未使用。本用例不读发布目录。
/// @return 页面被清空且说明里保留分类名时为 true。
bool unexpectedCategoryRejected(const char* catalogPath) {
    (void)catalogPath;
    const CatalogLoad loaded = ParameterCatalogLoader::loadText(
        catalogText(R"({"id":"Weird","firmware_path":"Weird","category":"Custom"})"));
    if (!loaded.ok()) {
        return fail("unexpected_category_rejected");
    }
    const NavigatorProjection projection = projectNavigatorPages(loaded.catalog());
    if (projection.ok || !projection.pages.empty() || projection.error.find("Custom") == std::string::npos
        || projection.error.find("Weird") == std::string::npos) {
        return fail("unexpected_category_rejected");
    }
    return true;
}

/// @brief 没有分类的参数拒绝整份投影。
/// @param catalogPath 未使用。本用例不读发布目录。
/// @return 说明里保留参数 id 时为 true。
bool missingCategoryRejected(const char* catalogPath) {
    (void)catalogPath;
    const CatalogLoad loaded = ParameterCatalogLoader::loadText(
        catalogText(R"({"id":"Orphan","firmware_path":"Orphan"})"));
    if (!loaded.ok()) {
        return fail("missing_category_rejected");
    }
    const NavigatorProjection projection = projectNavigatorPages(loaded.catalog());
    if (projection.ok || !projection.pages.empty() || projection.error.find("Orphan") == std::string::npos) {
        return fail("missing_category_rejected");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name 用例名，只在本次调用期间借用。
/// @param catalogPath 发布目录路径，只在本次调用期间借用。
/// @param recognized 识别到用例名时设为 true。调用方拥有。
/// @return 用例通过时为 true。未识别的名字也返回 false，并由 `recognized` 区分。
bool runCase(const char* name, const char* catalogPath, bool& recognized) {
    recognized = true;
    const std::string_view caseName(name);
    if (caseName == "page_order") {
        return pageOrder(catalogPath);
    }
    if (caseName == "check_data_not_date") {
        return checkDataNotDate(catalogPath);
    }
    if (caseName == "switch_state_split") {
        return switchStateSplit(catalogPath);
    }
    if (caseName == "parameters_once_in_file_order") {
        return parametersOnceInFileOrder(catalogPath);
    }
    if (caseName == "unexpected_category_rejected") {
        return unexpectedCategoryRejected(catalogPath);
    }
    if (caseName == "missing_category_rejected") {
        return missingCategoryRejected(catalogPath);
    }
    recognized = false;
    return false;
}

} // namespace

/// @brief 运行一个项目树页面用例。
/// @param argc 参数个数。必须是 3。
/// @param argv `argv[1]` 是用例名，`argv[2]` 是发布目录路径。只在本次调用期间借用。
/// @return 通过返回 0，断言失败返回 1，用法错误返回 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_navigator_pages_tests <case> <catalog-path>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], argv[2], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
