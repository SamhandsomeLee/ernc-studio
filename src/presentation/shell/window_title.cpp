#include "shell/window_title.hpp"

namespace ernc::presentation {

QString applicationBaseTitle() {
#ifdef ERNC_GIT_COMMIT_SHORT
    return QStringLiteral("ERNC Studio ") + QStringLiteral(ERNC_GIT_COMMIT_SHORT);
#else
    return QStringLiteral("ERNC Studio");
#endif
}

QString applicationWindowTitle(const QString& suffix) {
    if (suffix.isEmpty()) {
        return applicationBaseTitle();
    }
    return applicationBaseTitle() + QStringLiteral(" — ") + suffix;
}

}  // namespace ernc::presentation
