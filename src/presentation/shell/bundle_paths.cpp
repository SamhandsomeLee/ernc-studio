#include "shell/bundle_paths.hpp"

#include <QCoreApplication>
#include <QString>

#include <system_error>

namespace ernc::presentation {

std::filesystem::path resolveBundledResourcePath(const std::string_view relativeToExe,
                                                   const std::string_view compileTimeAbsolute) {
    if (QCoreApplication::instance() != nullptr) {
        const QString exeDir = QCoreApplication::applicationDirPath();
        if (!exeDir.isEmpty()) {
            QString relative = QString::fromUtf8(relativeToExe.data(), static_cast<int>(relativeToExe.size()));
            relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const std::filesystem::path candidate =
                std::filesystem::path(exeDir.toStdWString()) / std::filesystem::path(relative.toStdWString());
            std::error_code error;
            if (std::filesystem::exists(candidate, error)) {
                return candidate;
            }
        }
    }
    return std::filesystem::path(compileTimeAbsolute);
}

}  // namespace ernc::presentation
