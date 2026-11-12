#pragma once

#include "ernc/infrastructure/persistence/migrator.hpp"

#include <span>

namespace ernc::infrastructure {

/// @brief 应用内置的版本化迁移列表。
/// @return 按版本递增排列的迁移，生命周期与程序相同。调用方不拥有它。
std::span<const Migration> builtinMigrations() noexcept;

}  // namespace ernc::infrastructure
