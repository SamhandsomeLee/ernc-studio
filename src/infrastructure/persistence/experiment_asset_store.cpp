#include "ernc/infrastructure/persistence/experiment_asset_store.hpp"

#include <QCryptographicHash>

#include <fstream>
#include <string>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;

}  // namespace

std::string ExperimentAssetStore::sha256Hex(std::span<const std::uint8_t> bytes) {
    const QByteArray digest =
        QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(bytes.data()),
                                            static_cast<int>(bytes.size())),
                                 QCryptographicHash::Sha256);
    return digest.toHex().toLower().toStdString();
}

bool ExperimentAssetStore::fileNameIsSafe(std::string_view fileName) {
    if (fileName.empty() || fileName.size() > 200) {
        return false;
    }
    if (fileName.find('/') != std::string_view::npos || fileName.find('\\') != std::string_view::npos
        || fileName.find("..") != std::string_view::npos) {
        return false;
    }
    return true;
}

Result<std::string> ExperimentAssetStore::relativeBinPath(std::int64_t experimentId, std::string_view fileName) {
    if (experimentId <= 0 || !fileNameIsSafe(fileName)) {
        return Result<std::string>::failure(ErrorCode::InvalidParameter);
    }
    std::string relative = "data/experiments/";
    relative += std::to_string(experimentId);
    relative += '/';
    relative.append(fileName.data(), fileName.size());
    return Result<std::string>::success(std::move(relative));
}

Result<std::filesystem::path> ExperimentAssetStore::resolveUnderRoot(const std::filesystem::path& projectRoot,
                                                                     std::string_view relativePath) {
    if (relativePath.empty() || relativePath.find("..") != std::string_view::npos) {
        return Result<std::filesystem::path>::failure(ErrorCode::InvalidParameter);
    }
    const std::filesystem::path candidate = projectRoot / relativePath;
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(projectRoot, ec);
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(ErrorCode::FileIoFailed);
    }
    const auto rootStr = root.native();
    const auto resolvedStr = resolved.native();
    if (resolvedStr.size() < rootStr.size()) {
        return Result<std::filesystem::path>::failure(ErrorCode::InvalidParameter);
    }
    if (resolvedStr.compare(0, rootStr.size(), rootStr) != 0) {
        return Result<std::filesystem::path>::failure(ErrorCode::InvalidParameter);
    }
    return Result<std::filesystem::path>::success(resolved);
}

Result<void> ExperimentAssetStore::writeBinAtomic(const std::filesystem::path& projectRoot,
                                                  std::string_view relativePath,
                                                  std::span<const std::uint8_t> bytes) {
    const Result<std::filesystem::path> destination = resolveUnderRoot(projectRoot, relativePath);
    if (!destination.ok()) {
        return Result<void>::failure(destination.error());
    }
    if (std::filesystem::exists(destination.value())) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.value().parent_path(), ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    const std::filesystem::path staging = destination.value().string() + ".writing";
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        output.close();
        if (!output) {
            std::filesystem::remove(staging, ec);
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
    }
    std::filesystem::rename(staging, destination.value(), ec);
    if (ec) {
        std::filesystem::remove(staging, ec);
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    return Result<void>::success();
}

Result<std::vector<std::uint8_t>> ExperimentAssetStore::readBin(const std::filesystem::path& projectRoot,
                                                                std::string_view relativePath) {
    const Result<std::filesystem::path> destination = resolveUnderRoot(projectRoot, relativePath);
    if (!destination.ok()) {
        return Result<std::vector<std::uint8_t>>::failure(destination.error());
    }
    std::ifstream input(destination.value(), std::ios::binary);
    if (!input) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), length);
    }
    if (!input) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<void> ExperimentAssetStore::verifySha256(const std::filesystem::path& projectRoot,
                                                std::string_view relativePath, std::string_view expectedSha256,
                                                std::int64_t expectedByteSize) {
    const Result<std::vector<std::uint8_t>> bytes = readBin(projectRoot, relativePath);
    if (!bytes.ok()) {
        return Result<void>::failure(bytes.error());
    }
    if (static_cast<std::int64_t>(bytes.value().size()) != expectedByteSize) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::string digest = sha256Hex(bytes.value());
    if (digest != expectedSha256) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    return Result<void>::success();
}

}  // namespace ernc::infrastructure
