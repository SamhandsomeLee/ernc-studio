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

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 包一份 16 字节、4 个 slot 的最小 manifest。
/// @param fields fields 数组内部的 JSON，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的完整 JSON 文本。对齐固定为 4，因此 total_size 等于 total_slots * 4。
std::string manifest(std::string_view fields) {
    std::string text = R"({"alignment":4,"base":0,"total_size":16,"total_slots":4,"fields":[)";
    text.append(fields);
    text.append("]}");
    return text;
}

/// @brief 确认解析失败且不带几何。
/// @param layout 已载入的布局。
/// @param path 固件路径，只在本次调用期间借用。
/// @param expected 期望的 LayoutError。
/// @param name 用例名。
/// @return 错误类别匹配且 tryField() 为空时为 true。
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

/// @brief 确认文档加载失败且不带布局对象。
/// @param jsonText manifest 文本，只在本次调用期间借用。
/// @param expected 期望的 LayoutError。
/// @param name 用例名。
/// @return 错误类别匹配且 tryLayout() 为空时为 true。
bool expectLoad(std::string_view jsonText, LayoutError expected, const char* name) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(jsonText);
    if (loaded.tryLayout() != nullptr || loaded.tryError() == nullptr
        || *loaded.tryError() != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 子路径也被编进索引。顶层 fields 只有 69 项，这个数字来自 manifest 文件本身。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 展平后的字段数大于 69 时为 true。
bool indexesChildren(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok() || loaded.layout().fieldCount() <= 69) {
        return fail("indexes_children");
    }
    return true;
}

/// @brief ANC_OFF 在正式 manifest 中存在，但状态是 pack4-sequential，不能当成已核验。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 解析结果为 Unverified 时为 true。
bool ancUnverified(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("anc_unverified");
    }
    return expectResolve(loaded.layout(), "ANC_OFF", LayoutError::Unverified, "anc_unverified");
}

/// @brief children 里的完整路径能被找到，并且同样因为未核验而失败。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return RNC_param.alpha1 为 Unverified 时为 true。
bool childPathUnverified(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("child_path_unverified");
    }
    return expectResolve(
        loaded.layout(), "RNC_param.alpha1", LayoutError::Unverified, "child_path_unverified");
}

/// @brief OrderFour_Flag 只出现在 unresolved 名单，不在 fields 树里。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 解析结果为 NotFound 时为 true。
bool orderFourNotFound(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("order_four_not_found");
    }
    return expectResolve(
        loaded.layout(), "OrderFour_Flag", LayoutError::NotFound, "order_four_not_found");
}

/// @brief Refer_Num 是 Excel 名称，不是 manifest 路径。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 解析结果为 NotFound 时为 true。
bool referNumNotFound(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("refer_num_not_found");
    }
    return expectResolve(loaded.layout(), "Refer_Num", LayoutError::NotFound, "refer_num_not_found");
}

/// @brief 不把子字段的短名当成完整路径。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return alpha1 为 NotFound 时为 true。
bool shortNameNotFound(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("short_name_not_found");
    }
    return expectResolve(loaded.layout(), "alpha1", LayoutError::NotFound, "short_name_not_found");
}

/// @brief 空白路径没有对应字段。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 空白文本为 NotFound 时为 true。
bool blankPathNotFound(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("blank_path_not_found");
    }
    return expectResolve(loaded.layout(), "   ", LayoutError::NotFound, "blank_path_not_found");
}

/// @brief 已核验的 float 字段返回 slot、字节长度和标量类型，writable 保持 false。
/// @return 几何和类型都匹配样本时为 true。样本是测试构造的，不代表正式 manifest 已核验。
bool verifiedFloat() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("verified_float");
    }
    const LayoutResolve resolved = loaded.layout().resolve("ANC_OFF");
    if (!resolved.ok()) {
        return fail("verified_float");
    }
    const FieldLayout& field = resolved.field();
    if (field.slot != 0 || field.slotCount != 1 || field.size != 4 || field.offset != 0
        || field.typeName != "float" || !field.scalarType || *field.scalarType != DataType::Float32
        || field.writable) {
        return fail("verified_float");
    }
    return true;
}

/// @brief int32_t 映射为 Int32，不转换成 float。
/// @return 标量类型为 Int32 时为 true。
bool verifiedInt32() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"RNC_refresh","offset":0,"size":4,"slot":0,"slot_count":1,"type":"int32_t","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("verified_int32");
    }
    const LayoutResolve resolved = loaded.layout().resolve("RNC_refresh");
    if (!resolved.ok() || !resolved.field().scalarType
        || *resolved.field().scalarType != DataType::Int32 || resolved.field().typeName != "int32_t") {
        return fail("verified_int32");
    }
    return true;
}

/// @brief uint32_t 映射为 UInt32。
/// @return 标量类型为 UInt32 时为 true。
bool verifiedUint32() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"SpeakerCheckValue","offset":0,"size":4,"slot":0,"slot_count":1,"type":"uint32_t","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("verified_uint32");
    }
    const LayoutResolve resolved = loaded.layout().resolve("SpeakerCheckValue");
    if (!resolved.ok() || !resolved.field().scalarType
        || *resolved.field().scalarType != DataType::UInt32) {
        return fail("verified_uint32");
    }
    return true;
}

/// @brief 结构体类型保留原文，不猜测成三种标量之一。
/// @return scalarType 为空且类型名仍是 RNC_params 时为 true。
bool verifiedStruct() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"RNC_param","offset":0,"size":8,"slot":0,"slot_count":2,"type":"RNC_params","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("verified_struct");
    }
    const LayoutResolve resolved = loaded.layout().resolve("RNC_param");
    if (!resolved.ok() || resolved.field().scalarType || resolved.field().typeName != "RNC_params"
        || resolved.field().slotCount != 2 || resolved.field().size != 8) {
        return fail("verified_struct");
    }
    return true;
}

/// @brief 子字段使用完整路径，并保留它自己的 writable。
/// @return RNC_param.alpha1 成功、slot 为 3 且 writable 为 true 时为 true。
bool verifiedChild() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"RNC_param","offset":12,"size":4,"slot":3,"slot_count":1,"type":"RNC_params","shape":[],"status":"verified","writable":false,"children":[{"path":"RNC_param.alpha1","offset":12,"size":4,"slot":3,"slot_count":1,"type":"float","shape":[1],"status":"verified","writable":true}]})"));
    if (!loaded.ok()) {
        return fail("verified_child");
    }
    const LayoutResolve resolved = loaded.layout().resolve("RNC_param.alpha1");
    if (!resolved.ok() || !resolved.field().writable || resolved.field().slot != 3
        || !resolved.field().scalarType || *resolved.field().scalarType != DataType::Float32) {
        return fail("verified_child");
    }
    return true;
}

/// @brief 已核验样本里的短名仍然找不到。
/// @return alpha1 为 NotFound 时为 true。
bool childShortNameNotFound() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"RNC_param","offset":0,"size":4,"slot":0,"slot_count":1,"type":"RNC_params","shape":[],"status":"verified","writable":false,"children":[{"path":"RNC_param.alpha1","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false}]})"));
    if (!loaded.ok()) {
        return fail("child_short_name_not_found");
    }
    return expectResolve(
        loaded.layout(), "alpha1", LayoutError::NotFound, "child_short_name_not_found");
}

/// @brief 几何正确但状态仍是 pack4-sequential 时，先返回未核验。
/// @return 解析结果为 Unverified 时为 true。
bool pack4Unverified() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"pack4-sequential","writable":false})"));
    if (!loaded.ok()) {
        return fail("pack4_unverified");
    }
    return expectResolve(loaded.layout(), "ANC_OFF", LayoutError::Unverified, "pack4_unverified");
}

/// @brief 已核验字段的字节长度必须等于 slot 数乘 4。
/// @return size 为 3 时结果为 OutOfRange。
bool sizeOutOfRange() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":3,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("size_out_of_range");
    }
    return expectResolve(loaded.layout(), "ANC_OFF", LayoutError::OutOfRange, "size_out_of_range");
}

/// @brief 字节仍在镜像内，但 slot 起点超出总 slot 数。
/// @return slot 为 4、总 slot 为 4 时结果为 OutOfRange。
bool slotOutOfRange() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":4,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("slot_out_of_range");
    }
    return expectResolve(loaded.layout(), "ANC_OFF", LayoutError::OutOfRange, "slot_out_of_range");
}

/// @brief shape 各维乘积必须等于 slot 数。
/// @return shape 为 [2] 而 slot_count 为 1 时结果为 OutOfRange。
bool shapeOutOfRange() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[2],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("shape_out_of_range");
    }
    return expectResolve(loaded.layout(), "ANC_OFF", LayoutError::OutOfRange, "shape_out_of_range");
}

/// @brief 同一路径出现两次时，整份文档被拒绝。
/// @return 加载结果为 InvalidDocument 时为 true。
bool duplicatePath() {
    return expectLoad(
        manifest(
            R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false},{"path":"ANC_OFF","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})"),
        LayoutError::InvalidDocument,
        "duplicate_path");
}

/// @brief 本切片只接受 4 字节对齐。alignment 为 8 时文档无效。
/// @return 加载结果为 InvalidDocument 时为 true。
bool badAlignment() {
    return expectLoad(
        R"({"alignment":8,"base":0,"total_size":16,"total_slots":2,"fields":[]})",
        LayoutError::InvalidDocument,
        "bad_alignment");
}

/// @brief 残缺 JSON 不产生布局。
/// @return 加载结果为 InvalidDocument 时为 true。
bool malformedDocument() {
    return expectLoad("{", LayoutError::InvalidDocument, "malformed_document");
}

/// @brief 空文本不产生布局。
/// @return 加载结果为 InvalidDocument 时为 true。
bool emptyDocument() {
    return expectLoad("", LayoutError::InvalidDocument, "empty_document");
}

/// @brief 打不开的文件按无效文档失败，不返回空布局冒充成功。
/// @return 加载结果为 InvalidDocument 时为 true。
bool missingFile() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile("firmware/does-not-exist.json");
    if (loaded.ok() || loaded.tryError() == nullptr
        || *loaded.tryError() != LayoutError::InvalidDocument) {
        return fail("missing_file");
    }
    return true;
}

/// @brief 正式 manifest 里的 ANC_OFF 可以被 locate，同时 resolve 仍拒绝它。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return locate 得到 slot 0、1 个 slot、float，并且证据是 pack4-sequential 时为 true。
bool locatePack4Anc(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("locate_pack4_anc");
    }
    if (!expectResolve(loaded.layout(), "ANC_OFF", LayoutError::Unverified, "locate_pack4_anc")) {
        return false;
    }
    const LayoutResolve located = loaded.layout().locate("ANC_OFF");
    if (!located.ok()) {
        return fail("locate_pack4_anc");
    }
    const FieldLayout& field = located.field();
    if (field.slot != 0 || field.slotCount != 1 || field.size != 4 || field.typeName != "float"
        || !field.scalarType || *field.scalarType != DataType::Float32
        || field.evidence != ernc::domain::LayoutEvidence::Pack4Sequential) {
        return fail("locate_pack4_anc");
    }
    return true;
}

/// @brief Can_Msg 的 locate 返回整段 72 个 float slot。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return slot 为 14869、slot 数为 72、长度为 288 时为 true。
bool locatePack4CanMsg(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("locate_pack4_can_msg");
    }
    const LayoutResolve located = loaded.layout().locate("Can_Msg");
    if (!located.ok()) {
        return fail("locate_pack4_can_msg");
    }
    const FieldLayout& field = located.field();
    if (field.slot != 14869 || field.slotCount != 72 || field.size != 288 || field.typeName != "float"
        || !field.scalarType || *field.scalarType != DataType::Float32
        || field.evidence != ernc::domain::LayoutEvidence::Pack4Sequential) {
        return fail("locate_pack4_can_msg");
    }
    return true;
}

/// @brief 不在字段表里的名字 locate 不到几何。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return OrderFour_Flag 为 NotFound 时为 true。
bool locateMissing(const char* manifestPath) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail("locate_missing");
    }
    const LayoutResolve located = loaded.layout().locate("OrderFour_Flag");
    if (located.ok() || located.tryError() == nullptr || *located.tryError() != LayoutError::NotFound
        || located.tryField() != nullptr) {
        return fail("locate_missing");
    }
    return true;
}

/// @brief pack4-sequential 几何越界时 locate 返回越界，不把坏几何交出去。
/// @return locate 为 OutOfRange，并且 resolve 仍是 Unverified 时为 true。
bool locatePack4OutOfRange() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":3,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"pack4-sequential","writable":false})"));
    if (!loaded.ok()) {
        return fail("locate_pack4_out_of_range");
    }
    if (!expectResolve(loaded.layout(), "ANC_OFF", LayoutError::Unverified, "locate_pack4_out_of_range")) {
        return false;
    }
    const LayoutResolve located = loaded.layout().locate("ANC_OFF");
    if (located.ok() || located.tryError() == nullptr || *located.tryError() != LayoutError::OutOfRange) {
        return fail("locate_pack4_out_of_range");
    }
    return true;
}

/// @brief 已核验字段的 locate 与 resolve 给出同一几何，证据为 verified。
/// @return slot、类型和证据都匹配时为 true。
bool locateVerifiedKeepsGeometry() {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail("locate_verified_keeps_geometry");
    }
    const LayoutResolve located = loaded.layout().locate("ANC_OFF");
    const LayoutResolve resolved = loaded.layout().resolve("ANC_OFF");
    if (!located.ok() || !resolved.ok()) {
        return fail("locate_verified_keeps_geometry");
    }
    const FieldLayout& field = located.field();
    if (field.slot != resolved.field().slot || field.slotCount != resolved.field().slotCount || field.size != resolved.field().size
        || field.evidence != ernc::domain::LayoutEvidence::Verified
        || resolved.field().evidence != ernc::domain::LayoutEvidence::Verified) {
        return fail("locate_verified_keeps_geometry");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param manifestPath 正式 manifest 路径。只被读取正式文件的用例使用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath, bool& recognized) {
    recognized = true;
    if (name == "indexes_children") {
        return indexesChildren(manifestPath);
    }
    if (name == "anc_unverified") {
        return ancUnverified(manifestPath);
    }
    if (name == "child_path_unverified") {
        return childPathUnverified(manifestPath);
    }
    if (name == "order_four_not_found") {
        return orderFourNotFound(manifestPath);
    }
    if (name == "refer_num_not_found") {
        return referNumNotFound(manifestPath);
    }
    if (name == "short_name_not_found") {
        return shortNameNotFound(manifestPath);
    }
    if (name == "blank_path_not_found") {
        return blankPathNotFound(manifestPath);
    }
    if (name == "verified_float") {
        return verifiedFloat();
    }
    if (name == "verified_int32") {
        return verifiedInt32();
    }
    if (name == "verified_uint32") {
        return verifiedUint32();
    }
    if (name == "verified_struct") {
        return verifiedStruct();
    }
    if (name == "verified_child") {
        return verifiedChild();
    }
    if (name == "child_short_name_not_found") {
        return childShortNameNotFound();
    }
    if (name == "pack4_unverified") {
        return pack4Unverified();
    }
    if (name == "size_out_of_range") {
        return sizeOutOfRange();
    }
    if (name == "slot_out_of_range") {
        return slotOutOfRange();
    }
    if (name == "shape_out_of_range") {
        return shapeOutOfRange();
    }
    if (name == "duplicate_path") {
        return duplicatePath();
    }
    if (name == "bad_alignment") {
        return badAlignment();
    }
    if (name == "malformed_document") {
        return malformedDocument();
    }
    if (name == "empty_document") {
        return emptyDocument();
    }
    if (name == "missing_file") {
        return missingFile();
    }
    if (name == "locate_pack4_anc") {
        return locatePack4Anc(manifestPath);
    }
    if (name == "locate_pack4_can_msg") {
        return locatePack4CanMsg(manifestPath);
    }
    if (name == "locate_missing") {
        return locateMissing(manifestPath);
    }
    if (name == "locate_pack4_out_of_range") {
        return locatePack4OutOfRange();
    }
    if (name == "locate_verified_keeps_geometry") {
        return locateVerifiedKeepsGeometry();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个固件布局单元测试。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是用例名，argv[2] 是正式 layout_manifest.json 的路径。两段文本都只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_firmware_layout_tests <case> <layout_manifest.json>\n";
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
