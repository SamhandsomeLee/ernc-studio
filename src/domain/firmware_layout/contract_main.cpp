#include "ernc/domain/firmware_layout.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::domain::DataType;
using ernc::domain::FieldLayout;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutError;
using ernc::domain::LayoutLoad;
using ernc::domain::LayoutResolve;

/// @brief 打印失败检查名。
/// @param name 检查名，只在本次调用期间借用。
/// @return 恒为 false，便于串起后续检查。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 包一份最小 manifest。总长度固定为 16 字节、4 个 slot。
/// @param fields fields 数组内部的 JSON 片段，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的完整 JSON 文本。
std::string manifest(std::string_view fields) {
    std::string text = R"({"alignment":4,"base":0,"total_size":16,"total_slots":4,"fields":[)";
    text.append(fields);
    text.append("]}");
    return text;
}

/// @brief 确认一次解析以指定错误失败，并且没有几何。
/// @param layout 已经载入的布局。
/// @param path 要解析的固件路径，只在本次调用期间借用。
/// @param expected 期望的错误类别。
/// @param name 检查名。
/// @return 错误匹配且没有 FieldLayout 时为 true。
bool expectResolve(
    const FirmwareLayout& layout,
    std::string_view path,
    LayoutError expected,
    const char* name) {
    const LayoutResolve resolved = layout.resolve(path);
    if (resolved.tryField() != nullptr || resolved.tryError() == nullptr
        || *resolved.tryError() != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 确认加载以指定错误失败。
/// @param jsonText manifest 文本，只在本次调用期间借用。
/// @param expected 期望的文档错误。
/// @param name 检查名。
/// @return 错误匹配且没有布局对象时为 true。
bool expectLoad(std::string_view jsonText, LayoutError expected, const char* name) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(jsonText);
    if (loaded.tryLayout() != nullptr || loaded.tryError() == nullptr
        || *loaded.tryError() != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 确认正式 manifest 能建索引，但未核验和缺失路径都失败。
/// @param path 正式 layout_manifest.json 的路径，只在本次调用期间借用。
/// @return 子路径被索引到、未核验字段不返回几何时为 true。
bool checkRealManifest(const char* path) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(path);
    if (!loaded.ok() || loaded.tryLayout() == nullptr) {
        return fail("real-manifest");
    }
    const FirmwareLayout& layout = *loaded.tryLayout();
    if (layout.fieldCount() <= 69) {
        return fail("child-index");
    }
    return expectResolve(layout, "ANC_OFF", LayoutError::Unverified, "anc-unverified")
        && expectResolve(layout, "RNC_param.alpha1", LayoutError::Unverified, "child-unverified")
        && expectResolve(layout, "OrderFour_Flag", LayoutError::NotFound, "order-four")
        && expectResolve(layout, "Refer_Num", LayoutError::NotFound, "refer-num")
        && expectResolve(layout, "alpha1", LayoutError::NotFound, "short-name")
        && expectResolve(layout, "   ", LayoutError::NotFound, "blank-path");
}

/// @brief 用一份内存中的已核验文档证明成功、越界和类型映射。
/// @return 几何、标量类型和 writable 都符合样本时为 true。
bool checkVerifiedSamples() {
    const std::string verified = manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false},{"path":"RNC_refresh","offset":4,"size":4,"slot":1,"slot_count":1,"type":"int32_t","shape":[],"status":"verified","writable":false},{"path":"SpeakerCheckValue","offset":8,"size":4,"slot":2,"slot_count":1,"type":"uint32_t","shape":[],"status":"verified","writable":false},{"path":"RNC_param","offset":12,"size":4,"slot":3,"slot_count":1,"type":"RNC_params","shape":[],"status":"verified","writable":false,"children":[{"path":"RNC_param.alpha1","offset":12,"size":4,"slot":3,"slot_count":1,"type":"float","shape":[1],"status":"verified","writable":true}]})");
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(verified);
    if (!loaded.ok()) {
        return fail("verified-load");
    }
    const FirmwareLayout& layout = *loaded.tryLayout();
    const LayoutResolve anc = layout.resolve("ANC_OFF");
    if (!anc.ok()) {
        return fail("verified-anc");
    }
    const FieldLayout& ancField = anc.field();
    if (ancField.slot != 0 || ancField.slotCount != 1 || ancField.size != 4 || ancField.offset != 0
        || ancField.typeName != "float" || !ancField.scalarType
        || *ancField.scalarType != DataType::Float32 || ancField.writable) {
        return fail("verified-anc-geometry");
    }
    const LayoutResolve refresh = layout.resolve("RNC_refresh");
    const LayoutResolve speaker = layout.resolve("SpeakerCheckValue");
    const LayoutResolve parent = layout.resolve("RNC_param");
    const LayoutResolve child = layout.resolve("RNC_param.alpha1");
    if (!refresh.ok() || !refresh.field().scalarType || *refresh.field().scalarType != DataType::Int32
        || !speaker.ok() || !speaker.field().scalarType
        || *speaker.field().scalarType != DataType::UInt32 || !parent.ok() || parent.field().scalarType
        || parent.field().typeName != "RNC_params" || !child.ok() || !child.field().writable
        || child.field().slot != 3) {
        return fail("verified-types");
    }
    if (!expectResolve(layout, "alpha1", LayoutError::NotFound, "child-short-name")) {
        return false;
    }

    const std::string sequential = manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"pack4-sequential","writable":false})");
    const LayoutLoad sequentialLoad = FirmwareLayoutLoader::loadText(sequential);
    if (!sequentialLoad.ok()
        || !expectResolve(
            *sequentialLoad.tryLayout(), "ANC_OFF", LayoutError::Unverified, "pack4-status")) {
        return false;
    }

    const std::string outOfRange = manifest(
        R"({"path":"ANC_OFF","offset":0,"size":3,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})");
    const LayoutLoad rangeLoad = FirmwareLayoutLoader::loadText(outOfRange);
    if (!rangeLoad.ok()
        || !expectResolve(*rangeLoad.tryLayout(), "ANC_OFF", LayoutError::OutOfRange, "bad-size")) {
        return false;
    }

    const std::string pastEnd = manifest(
        R"({"path":"ANC_OFF","offset":16,"size":4,"slot":4,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})");
    const LayoutLoad pastLoad = FirmwareLayoutLoader::loadText(pastEnd);
    if (!pastLoad.ok()
        || !expectResolve(*pastLoad.tryLayout(), "ANC_OFF", LayoutError::OutOfRange, "past-end")) {
        return false;
    }
    return true;
}

/// @brief 确认坏文档不会生成布局对象。
/// @return 重复路径、错误对齐和非法 JSON 都是 InvalidDocument 时为 true。
bool checkInvalidDocuments() {
    const std::string duplicate = manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false},{"path":"ANC_OFF","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})");
    const char* badAlignment =
        R"({"alignment":8,"base":0,"total_size":16,"total_slots":2,"fields":[]})";
    return expectLoad(duplicate, LayoutError::InvalidDocument, "duplicate-path")
        && expectLoad(badAlignment, LayoutError::InvalidDocument, "alignment")
        && expectLoad("{", LayoutError::InvalidDocument, "malformed")
        && expectLoad("", LayoutError::InvalidDocument, "empty");
}

}  // namespace

/// @brief 运行固件布局契约检查。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv 参数文本。argv[1] 是 layout_manifest.json 的路径，只在本次进程内借用。
/// @return 全部检查通过时为 0。用法不对时为 2，契约不符合时为 1。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc-layout-contract <layout_manifest.json>\n";
        return 2;
    }
    if (!checkRealManifest(argv[1]) || !checkVerifiedSamples() || !checkInvalidDocuments()) {
        return 1;
    }
    const LayoutLoad missing = FirmwareLayoutLoader::loadFile("firmware/does-not-exist.json");
    if (missing.ok() || missing.tryError() == nullptr
        || *missing.tryError() != LayoutError::InvalidDocument) {
        std::cerr << "failed: missing-file\n";
        return 1;
    }
    std::cout << "layout contract passed\n";
    return 0;
}
