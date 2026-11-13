#include "parameters/runtime_time_display.hpp"

#include <QDateTime>
#include <QTimeZone>

#include <chrono>

namespace ernc::presentation {
namespace {

const QTimeZone& beijingTimeZone() {
    static const QTimeZone zone = [] {
        const QTimeZone named(QByteArray("Asia/Shanghai"));
        return named.isValid() ? named : QTimeZone::fromSecondsAheadOfUtc(8 * 3600);
    }();
    return zone;
}

}  // namespace

QString formatRuntimeTimeBeijing(const ernc::domain::RuntimeTime time) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch());
    const QDateTime local =
        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds.count()), beijingTimeZone());
    return local.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

std::string formatRuntimeTimeBeijingUtf8(const ernc::domain::RuntimeTime time) {
    return formatRuntimeTimeBeijing(time).toStdString();
}

}  // namespace ernc::presentation
