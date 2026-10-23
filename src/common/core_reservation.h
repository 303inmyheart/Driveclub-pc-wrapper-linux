// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Common {

// Four physical cores (including 4c/8t handhelds) need all cores for guest and
// driver workers. A dedicated graphics core would remove a quarter of them.
constexpr bool CanReservePhysicalCore(unsigned physical_cores) {
    return physical_cores > 4;
}

constexpr std::uint64_t ReservationWalkMask(std::uint64_t current, std::uint64_t reserved,
                                           std::uint64_t tid, std::uint64_t caller,
                                           std::uint64_t owner) {
    if (tid == caller || (owner != 0 && tid == owner)) return current;
    const auto narrowed = current & ~reserved;
    return narrowed ? narrowed : current; // Never strand a thread or widen its affinity.
}

} // namespace Common
