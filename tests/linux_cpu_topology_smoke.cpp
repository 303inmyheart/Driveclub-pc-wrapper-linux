// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include "common/linux_cpu_topology.h"
#include "common/core_reservation.h"

int main() {
    using namespace Common;
    assert(!CanReservePhysicalCore(0));
    assert(!CanReservePhysicalCore(2));
    assert(!CanReservePhysicalCore(4)); // Steam Deck: SMT does not add physical cores.
    assert(CanReservePhysicalCore(6));
    assert(CanReservePhysicalCore(16));
    assert(ParseCpuListLinux("0-3,8-11\n") == 0xf0f);
    // Regression: a periodic walk runs on another thread. The GPU owner may have
    // widened from its reserved P-core to all P-cores and must not lose that core.
    constexpr unsigned gpu = 21, walker = 42, game = 77;
    constexpr std::uint64_t pcores = 0xfff, reserved = 0xc00;
    assert(ReservationWalkMask(pcores, reserved, gpu, walker, gpu) == pcores);
    assert(ReservationWalkMask(pcores, reserved, game, walker, gpu) == 0x3ff);
    auto mask = pcores;
    for (int pass = 0; pass < 20; ++pass)
        mask = ReservationWalkMask(mask, reserved, gpu, walker, gpu);
    assert(mask == pcores);
    assert(ReservationWalkMask(0x30, reserved, game, walker, gpu) == 0x30);
    assert(ReservationWalkMask(reserved, reserved, game, walker, gpu) == reserved);
    for (const auto* invalid : {"0-64", "64", "8-2", "-1", "99999999999999999999", "0x1", "0,"})
        assert(ParseCpuListLinux(invalid) == 0);
    char pattern[] = "/tmp/driveclub-topology-XXXXXX";
    const auto dir = mkdtemp(pattern);
    assert(dir);
    const std::filesystem::path root{dir};
    std::filesystem::create_directories(root / "cpu_core");
    std::filesystem::create_directories(root / "cpu_atom");
    // Regression: Core Ultra 7 155H, with no topology/core_type file. The former fallback
    // treated all 22 CPUs as equally fast and reserved LP-E CPU 21 for the graphics thread.
    std::ofstream(root / "cpu_core/cpus") << "0-11\n";
    std::ofstream(root / "cpu_atom/cpus") << "12-21\n";
    const auto fast = ReadLinuxPerformanceMask(root, 0x3fffff);
    assert(fast == 0xfff);
    assert(!(fast & (1ull << 21)));
    assert(ReadLinuxPerformanceMask(root, 0x30) == 0x30);
    assert(ReadLinuxPerformanceMask(root, 1ull << 21) == 0);
    std::ofstream(root / "cpu_atom/cpus") << "0-21\n";
    assert(ReadLinuxPerformanceMask(root, 0x3fffff) == 0);
    std::filesystem::remove(root / "cpu_atom/cpus");
    assert(ReadLinuxPerformanceMask(root, 0x3fffff) == 0);
    std::filesystem::remove_all(root);
    std::cout << "Linux hybrid topology and affinity restrictions: OK\n";
}
