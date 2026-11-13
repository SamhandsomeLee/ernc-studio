#include "ernc/infrastructure/logging/session_log_archive.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;

constexpr std::uint32_t kLocalFileHeaderSignature = 0x04034b50U;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014b50U;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50U;
constexpr std::uint16_t kZipStoreMethod = 0U;

std::uint32_t crc32Update(std::uint32_t crc, std::uint8_t byte) {
    static constexpr std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t index = 0; index < 256U; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                if ((value & 1U) != 0U) {
                    value = 0xEDB88320U ^ (value >> 1U);
                } else {
                    value >>= 1U;
                }
            }
            values[index] = value;
        }
        return values;
    }();
    return table[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
}

std::uint32_t crc32Buffer(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::uint8_t byte : bytes) {
        crc = crc32Update(crc, byte);
    }
    return crc ^ 0xFFFFFFFFU;
}

void writeU16(std::ostream& output, std::uint16_t value) {
    const char bytes[2] = {static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU)};
    output.write(bytes, 2);
}

void writeU32(std::ostream& output, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU),
                           static_cast<char>((value >> 16U) & 0xFFU), static_cast<char>((value >> 24U) & 0xFFU)};
    output.write(bytes, 4);
}

std::string posixZipPath(const std::filesystem::path& relative) {
    std::string text = relative.generic_string();
    for (char& character : text) {
        if (character == '\\') {
            character = '/';
        }
    }
    return text;
}

struct ZipEntry {
    std::string nameInZip;
    std::vector<std::uint8_t> data;
    std::uint32_t crc = 0U;
    std::uint32_t localHeaderOffset = 0U;
};

Result<std::vector<std::uint8_t>> readBinaryFile(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input) {
            return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
        }
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<void> collectLogFiles(const std::filesystem::path& logsRoot, const std::filesystem::path& relativePrefix,
                             std::vector<ZipEntry>& entries) {
    std::error_code error;
    if (!std::filesystem::exists(logsRoot, error) || !std::filesystem::is_directory(logsRoot, error)) {
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    for (const std::filesystem::directory_entry& child :
         std::filesystem::recursive_directory_iterator(
             logsRoot, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        if (!child.is_regular_file(error)) {
            continue;
        }
        const std::filesystem::path relative = relativePrefix / std::filesystem::relative(child.path(), logsRoot, error);
        if (error) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        const auto loaded = readBinaryFile(child.path());
        if (!loaded.ok()) {
            return Result<void>::failure(loaded.error());
        }
        ZipEntry entry;
        entry.nameInZip = posixZipPath(relative);
        entry.data = loaded.value();
        entry.crc = crc32Buffer(entry.data);
        entries.push_back(std::move(entry));
    }
    return Result<void>::success();
}

std::string buildManifest(const std::filesystem::path& logsRoot, std::size_t fileCount) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream stamp;
    stamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    std::ostringstream json;
    json << "{\"export_time_utc\":\"" << stamp.str() << "\",\"logs_root\":\"" << logsRoot.generic_string()
         << "\",\"file_count\":" << fileCount << ",\"format\":\"ernc-session-logs-v1\"}";
    return json.str();
}

Result<void> writeZip(const std::filesystem::path& destinationZip, const std::filesystem::path& logsRoot,
                      std::vector<ZipEntry> entries) {
    const std::string manifest = buildManifest(logsRoot, entries.size());
    ZipEntry manifestEntry;
    manifestEntry.nameInZip = "logs/manifest.json";
    manifestEntry.data.assign(manifest.begin(), manifest.end());
    manifestEntry.crc = crc32Buffer(manifestEntry.data);
    entries.insert(entries.begin(), std::move(manifestEntry));

    const std::filesystem::path temporary = destinationZip.string() + ".part";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }

        for (ZipEntry& entry : entries) {
            entry.localHeaderOffset = static_cast<std::uint32_t>(output.tellp());
            writeU32(output, kLocalFileHeaderSignature);
            writeU16(output, 20U);
            writeU16(output, 0U);
            writeU16(output, kZipStoreMethod);
            writeU16(output, 0U);
            writeU16(output, 0U);
            writeU32(output, entry.crc);
            writeU32(output, static_cast<std::uint32_t>(entry.data.size()));
            writeU32(output, static_cast<std::uint32_t>(entry.data.size()));
            writeU16(output, static_cast<std::uint16_t>(entry.nameInZip.size()));
            writeU16(output, 0U);
            output.write(entry.nameInZip.data(), static_cast<std::streamsize>(entry.nameInZip.size()));
            if (!entry.data.empty()) {
                output.write(reinterpret_cast<const char*>(entry.data.data()),
                             static_cast<std::streamsize>(entry.data.size()));
            }
            if (!output) {
                return Result<void>::failure(ErrorCode::FileIoFailed);
            }
        }

        const std::uint32_t centralDirectoryOffset = static_cast<std::uint32_t>(output.tellp());
        for (const ZipEntry& entry : entries) {
            writeU32(output, kCentralDirectorySignature);
            writeU16(output, 20U);
            writeU16(output, 20U);
            writeU16(output, 0U);
            writeU16(output, kZipStoreMethod);
            writeU16(output, 0U);
            writeU16(output, 0U);
            writeU32(output, entry.crc);
            writeU32(output, static_cast<std::uint32_t>(entry.data.size()));
            writeU32(output, static_cast<std::uint32_t>(entry.data.size()));
            writeU16(output, static_cast<std::uint16_t>(entry.nameInZip.size()));
            writeU16(output, 0U);
            writeU16(output, 0U);
            writeU16(output, 0U);
            writeU16(output, 0U);
            writeU32(output, 0U);
            writeU32(output, entry.localHeaderOffset);
            output.write(entry.nameInZip.data(), static_cast<std::streamsize>(entry.nameInZip.size()));
            if (!output) {
                return Result<void>::failure(ErrorCode::FileIoFailed);
            }
        }

        const std::uint32_t centralDirectorySize =
            static_cast<std::uint32_t>(output.tellp()) - centralDirectoryOffset;
        writeU32(output, kEndOfCentralDirectorySignature);
        writeU16(output, 0U);
        writeU16(output, 0U);
        writeU16(output, static_cast<std::uint16_t>(entries.size()));
        writeU16(output, static_cast<std::uint16_t>(entries.size()));
        writeU32(output, centralDirectorySize);
        writeU32(output, centralDirectoryOffset);
        writeU16(output, 0U);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
    }

    std::error_code error;
    std::filesystem::remove(destinationZip, error);
    std::filesystem::rename(temporary, destinationZip, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    return Result<void>::success();
}

}  // namespace

Result<void> exportSessionLogsToZip(const std::filesystem::path& logsRoot, const std::filesystem::path& destinationZip,
                                    const SessionLogArchiveScope scope) {
    if (scope != SessionLogArchiveScope::AllSessionsUnderLogsRoot) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (destinationZip.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
  std::vector<ZipEntry> entries;
    const auto collected = collectLogFiles(logsRoot, std::filesystem::path{"logs"}, entries);
    if (!collected.ok()) {
        return collected;
    }
    return writeZip(destinationZip, logsRoot, std::move(entries));
}

}  // namespace ernc::infrastructure
