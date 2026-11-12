#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {

/// Region BIN 在工程包内的落盘与校验。仅处理相对路径，不写硬件。
class ExperimentAssetStore {
public:
    /// @brief 计算 SHA-256 小写十六进制摘要。
    static std::string sha256Hex(std::span<const std::uint8_t> bytes);

    /// @brief 生成 `data/experiments/{experimentId}/{fileName}` 相对路径。
    static ernc::domain::Result<std::string> relativeBinPath(std::int64_t experimentId, std::string_view fileName);

    /// @brief 原子写入 BIN；目标已存在时失败。
    static ernc::domain::Result<void> writeBinAtomic(const std::filesystem::path& projectRoot,
                                                      std::string_view relativePath,
                                                      std::span<const std::uint8_t> bytes);

    /// @brief 读取工程内相对路径对应的全部字节。
    static ernc::domain::Result<std::vector<std::uint8_t>> readBin(const std::filesystem::path& projectRoot,
                                                                  std::string_view relativePath);

    /// @brief 读取文件并核对 SHA-256。
    static ernc::domain::Result<void> verifySha256(const std::filesystem::path& projectRoot,
                                                   std::string_view relativePath, std::string_view expectedSha256,
                                                   std::int64_t expectedByteSize);

private:
    static bool fileNameIsSafe(std::string_view fileName);
    static ernc::domain::Result<std::filesystem::path> resolveUnderRoot(const std::filesystem::path& projectRoot,
                                                                       std::string_view relativePath);
};

inline constexpr std::string_view kRegionBinAssetKind = "region_bin";
inline constexpr std::string_view kBinAssetFormat = "bin";

}  // namespace ernc::infrastructure
