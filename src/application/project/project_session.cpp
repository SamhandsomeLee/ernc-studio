#include "ernc/application/project/project_session.hpp"

#include <QJsonDocument>

namespace ernc::application {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::infrastructure::LegacyParamJsonDocument;
using ernc::infrastructure::LegacyParamJsonLoad;

namespace {

/// @brief 将 v1 参数序列化成可嵌入 manifest 的对象。失败时不修改工程。
/// @param parameters 本次调用借用的参数文档，数值保留原单位。
Result<QJsonObject> encodeParameters(const LegacyParamJsonDocument& parameters) {
    const auto encoded = ernc::infrastructure::serializeLegacyParamJson(parameters);
    if (!encoded.ok()) {
        return Result<QJsonObject>::failure(encoded.error());
    }
    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(encoded.value()));
    return document.isObject() ? Result<QJsonObject>::success(document.object())
                               : Result<QJsonObject>::failure(ErrorCode::InvalidParameter);
}

/// @brief 解析一个 v1 文档，兼容旧工程中的缺失字段。
/// @param value manifest 值，仅在本次调用借用。非对象且非 Undefined 时解析失败。
LegacyParamJsonLoad decodeParameters(const QJsonValue& value) {
    if (value.isUndefined()) {
        LegacyParamJsonLoad empty;
        empty.success = true;
        empty.document.version = ernc::infrastructure::kLegacyParamJsonVersion;
        return empty;
    }
    if (!value.isObject()) {
        return {};
    }
    return ernc::infrastructure::parseLegacyParamJson(
        QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString());
}

}  // namespace

Result<OpenProject> ProjectSession::create(
    const std::filesystem::path& root, std::string_view name, std::string_view catalogVersion) {
    if (store_.isOpen()) {
        return Result<OpenProject>::failure(ErrorCode::InvalidParameter);
    }
    return store_.createEmpty(root, name, catalogVersion);
}

Result<OpenProject> ProjectSession::open(const std::filesystem::path& root) {
    if (store_.isOpen()) {
        return Result<OpenProject>::failure(ErrorCode::InvalidParameter);
    }
    return store_.open(root);
}

Result<void> ProjectSession::save() {
    if (!store_.isOpen()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    return store_.save();
}

Result<void> ProjectSession::saveParameters(const LegacyParamJsonDocument& read, const LegacyParamJsonDocument& staged) {
    const auto encodedRead = encodeParameters(read);
    if (!encodedRead.ok()) {
        return Result<void>::failure(encodedRead.error());
    }
    const auto encodedStaged = encodeParameters(staged);
    if (!encodedStaged.ok()) {
        return Result<void>::failure(encodedStaged.error());
    }
    return store_.save({encodedRead.value(), encodedStaged.value()});
}

Result<LoadedProjectParameters> ProjectSession::loadParameters() const {
    const auto documents = store_.readParameters();
    if (!documents.ok()) {
        return Result<LoadedProjectParameters>::failure(documents.error());
    }
    auto read = decodeParameters(documents.value().read);
    if (!read.ok()) {
        return Result<LoadedProjectParameters>::failure(read.error);
    }
    auto staged = decodeParameters(documents.value().staged);
    if (!staged.ok()) {
        return Result<LoadedProjectParameters>::failure(staged.error);
    }
    return Result<LoadedProjectParameters>::success({std::move(read), std::move(staged)});
}

Result<void> ProjectSession::close() {
    return store_.close();
}

}  // namespace ernc::application
