#include "ernc/application/experiment/experiment_service.hpp"
#include "ernc/application/project/project_session.hpp"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::application::AttachRegionBinRequest;
using ernc::application::CreateExperimentRequest;
using ernc::application::ExperimentService;
using ernc::application::ProjectSession;
using ernc::domain::ErrorCode;
using ernc::domain::Result;

constexpr const char* kCatalogVersion = "0.2.0";
constexpr const char* kProjectName = "ExperimentTest";
constexpr const char* kCondition = "bench-run-1";

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

struct TempWorkspace {
    QTemporaryDir qtDir;
    std::filesystem::path parent;

    TempWorkspace() {
        if (!qtDir.isValid()) {
            std::random_device device;
            parent = std::filesystem::temp_directory_path() / ("ernc-es086-" + std::to_string(device()));
            std::filesystem::create_directories(parent);
            ownsFallback = true;
        } else {
            parent = std::filesystem::path(qtDir.path().toStdWString());
        }
    }

    ~TempWorkspace() {
        if (ownsFallback) {
            std::error_code ignored;
            std::filesystem::remove_all(parent, ignored);
        }
    }

    std::filesystem::path bundlePath() const { return parent / "Sample.erncproj"; }

private:
    bool ownsFallback{false};
};

Result<std::int64_t> createOpenProject(TempWorkspace& workspace) {
    ProjectSession session;
    const Result created = session.create(workspace.bundlePath(), kProjectName, kCatalogVersion);
    if (!created.ok()) {
        return Result<std::int64_t>::failure(created.error());
    }
  return Result<std::int64_t>::success(created.value().projectId);
}

std::vector<std::uint8_t> sampleBytes() {
    std::vector<std::uint8_t> bytes(16);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(index + 1);
    }
    return bytes;
}

bool createAndAttach() {
    TempWorkspace workspace;
    const Result<std::int64_t> projectId = createOpenProject(workspace);
    if (!projectId.ok()) {
        return fail("create_and_attach");
    }
    ProjectSession session;
    if (!session.open(workspace.bundlePath()).ok()) {
        return fail("create_and_attach");
    }
    const Result ran = session.withDatabase([&](QSqlDatabase& database) {
        ExperimentService service(database, workspace.bundlePath());
        CreateExperimentRequest create;
        create.projectId = projectId.value();
        create.name = "Run-A";
        create.testCondition = kCondition;
        const Result<std::int64_t> experimentId = service.createExperiment(create);
        if (!experimentId.ok()) {
            return Result<void>::failure(experimentId.error());
        }
        const std::vector<std::uint8_t> bytes = sampleBytes();
        AttachRegionBinRequest attach;
        attach.experimentId = experimentId.value();
        attach.fileName = "check_data.bin";
        attach.bytes = bytes;
        attach.regionPath = "check_data";
        const Result<std::int64_t> assetId = service.attachRegionBin(attach);
        if (!assetId.ok()) {
            return Result<void>::failure(assetId.error());
        }
        const Result<void> verified = service.verifyAsset(assetId.value());
        if (!verified.ok()) {
            return Result<void>::failure(verified.error());
        }
        return Result<void>::success();
    });
    if (!ran.ok()) {
        return fail("create_and_attach");
    }
    session.close();
    std::error_code ec;
    std::filesystem::path search = workspace.bundlePath() / "data" / "experiments";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(search, ec)) {
        if (entry.is_regular_file() && entry.path().filename() == "check_data.bin") {
            return true;
        }
    }
    return fail("create_and_attach");
}

bool reopenVerifyHash() {
    TempWorkspace workspace;
    const Result<std::int64_t> projectId = createOpenProject(workspace);
    if (!projectId.ok()) {
        return fail("reopen_verify_hash");
    }
    {
        ProjectSession session;
        if (!session.open(workspace.bundlePath()).ok()) {
            return fail("reopen_verify_hash");
        }
        std::optional<std::int64_t> assetId;
        const Result ran = session.withDatabase([&](QSqlDatabase& database) {
            ExperimentService service(database, workspace.bundlePath());
            CreateExperimentRequest create;
            create.projectId = projectId.value();
            create.name = "Run-B";
            const Result<std::int64_t> experimentId = service.createExperiment(create);
            if (!experimentId.ok()) {
                return Result<void>::failure(experimentId.error());
            }
            const std::vector<std::uint8_t> bytes = sampleBytes();
            AttachRegionBinRequest attach;
            attach.experimentId = experimentId.value();
            attach.fileName = "region.bin";
            attach.bytes = bytes;
            const Result<std::int64_t> inserted = service.attachRegionBin(attach);
            if (!inserted.ok()) {
                return Result<void>::failure(inserted.error());
            }
            assetId = inserted.value();
            return Result<void>::success();
        });
        if (!ran.ok() || !assetId) {
            return fail("reopen_verify_hash");
        }
        session.close();
    }
    ProjectSession reopened;
    if (!reopened.open(workspace.bundlePath()).ok()) {
        return fail("reopen_verify_hash");
    }
    return reopened.withDatabase([&](QSqlDatabase& database) {
        ExperimentService service(database, workspace.bundlePath());
        const Result<std::vector<ernc::domain::DataAssetSummary>> assets = service.listAssets(1);
        if (!assets.ok() || assets.value().empty()) {
            return Result<void>::failure(ErrorCode::DatabaseFailed);
        }
        return service.verifyAsset(assets.value().front().assetId);
    }).ok();
}

bool hashMismatchFails() {
    TempWorkspace workspace;
    const Result<std::int64_t> projectId = createOpenProject(workspace);
    if (!projectId.ok()) {
        return fail("hash_mismatch_fails");
    }
    ProjectSession session;
    if (!session.open(workspace.bundlePath()).ok()) {
        return fail("hash_mismatch_fails");
    }
    std::optional<std::int64_t> assetId;
    const Result ran = session.withDatabase([&](QSqlDatabase& database) {
        ExperimentService service(database, workspace.bundlePath());
        CreateExperimentRequest create;
        create.projectId = projectId.value();
        create.name = "Run-C";
        const Result<std::int64_t> experimentId = service.createExperiment(create);
        if (!experimentId.ok()) {
            return Result<void>::failure(experimentId.error());
        }
        AttachRegionBinRequest attach;
        attach.experimentId = experimentId.value();
        attach.fileName = "tamper.bin";
        attach.bytes = sampleBytes();
        const Result<std::int64_t> inserted = service.attachRegionBin(attach);
        if (!inserted.ok()) {
            return Result<void>::failure(inserted.error());
        }
        assetId = inserted.value();
        return Result<void>::success();
    });
    if (!ran.ok() || !assetId) {
        return fail("hash_mismatch_fails");
    }
    const std::filesystem::path file = workspace.bundlePath() / "data" / "experiments" / "1" / "tamper.bin";
    std::fstream fileStream(file, std::ios::in | std::ios::out | std::ios::binary);
    if (!fileStream) {
        return fail("hash_mismatch_fails");
    }
    char byte{};
    fileStream.get(byte);
    fileStream.seekp(0);
    fileStream.put(static_cast<char>(byte + 1));
    fileStream.close();

    const Result verify = session.withDatabase([&](QSqlDatabase& database) {
        ExperimentService service(database, workspace.bundlePath());
        return service.verifyAsset(*assetId);
    });
    return !verify.ok() && verify.error() == ErrorCode::InvalidParameter;
}

bool runCase(std::string_view name) {
    if (name == "create_and_attach") {
        return createAndAttach();
    }
    if (name == "reopen_verify_hash") {
        return reopenVerifyHash();
    }
    if (name == "hash_mismatch_fails") {
        return hashMismatchFails();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_experiment_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QCoreApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
