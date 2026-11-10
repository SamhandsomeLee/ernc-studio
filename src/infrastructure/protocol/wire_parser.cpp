#include "ernc/protocol/wire_parser.hpp"

#include "ernc/protocol/crc16.hpp"

#include <cstdint>
#include <span>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_protocol must not include Qt"
#endif

namespace ernc::protocol {
namespace {

/// 旧版主机缓冲把小于 5 的长度字节视为非法并丢掉这 1 字节。
constexpr std::uint8_t kMinimumModbusBytes = 5;

}  // namespace

void WireParser::append(std::span<const std::uint8_t> bytes) {
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
}

void WireParser::clear() noexcept {
    pending_.clear();
}

ParseStep WireParser::pull() {
    if (pending_.empty()) {
        return ParseStep{ParseStep::Kind::NeedMore, {}};
    }
    const auto length = static_cast<std::uint8_t>(pending_.front());
    if (length < kMinimumModbusBytes) {
        pending_.erase(pending_.begin());
        return ParseStep{ParseStep::Kind::LengthDropped, {}};
    }
    const std::size_t need = static_cast<std::size_t>(length) + 1U;
    if (pending_.size() < need) {
        return ParseStep{ParseStep::Kind::NeedMore, {}};
    }
    const std::span<const std::uint8_t> inner{pending_.data() + 1, length};
    const ernc::domain::Result<void> crc = verifyCrc16(inner);
    std::vector<std::uint8_t> wire{pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(need)};
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(need));
    if (!crc.ok()) {
        return ParseStep{ParseStep::Kind::CrcRejected, {}};
    }
    return ParseStep{ParseStep::Kind::Frame, std::move(wire)};
}

}  // namespace ernc::protocol
