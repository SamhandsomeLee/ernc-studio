#include "ernc/application/project/project_session.hpp"

namespace ernc::application {

using ernc::domain::ErrorCode;
using ernc::domain::Result;

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

Result<void> ProjectSession::close() {
    return store_.close();
}

}  // namespace ernc::application
