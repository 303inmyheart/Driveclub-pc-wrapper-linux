// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace Common {

inline std::uint64_t ParseCpuListLinux(std::string_view list) {
    std::uint64_t mask = 0;
    while (!list.empty()) {
        while (!list.empty() && (list.front() == ' ' || list.front() == '\t' ||
                                list.front() == '\n' || list.front() == '\r'))
            list.remove_prefix(1);
        if (list.empty()) break;
        unsigned first = 0, last = 0;
        const auto begin = std::from_chars(list.data(), list.data() + list.size(), first);
        if (begin.ec != std::errc{} || first >= 64) return 0;
        list.remove_prefix(begin.ptr - list.data());
        last = first;
        if (!list.empty() && list.front() == '-') {
            list.remove_prefix(1);
            const auto end = std::from_chars(list.data(), list.data() + list.size(), last);
            if (end.ec != std::errc{} || last >= 64 || last < first) return 0;
            list.remove_prefix(end.ptr - list.data());
        }
        for (unsigned cpu = first; cpu <= last; ++cpu) mask |= 1ull << cpu;
        while (!list.empty() && (list.front() == ' ' || list.front() == '\t' ||
                                list.front() == '\n' || list.front() == '\r'))
            list.remove_prefix(1);
        if (list.empty()) break;
        if (list.front() != ',') return 0;
        list.remove_prefix(1);
        if (list.empty()) return 0;
    }
    return mask;
}

// Intel hybrid PMU CPU lists remain available on kernels without topology/core_type.
// Unknown, incomplete or inconsistent topology never invents a performance class.
inline std::uint64_t ReadLinuxPerformanceMask(const std::filesystem::path& pmu_root,
                                              std::uint64_t allowed) {
    const auto read = [&](const char* kind) {
        std::ifstream file(pmu_root / kind / "cpus");
        std::string list;
        return std::getline(file, list) ? ParseCpuListLinux(list) : std::uint64_t{0};
    };
    const auto core = read("cpu_core"), atom = read("cpu_atom");
    return core && atom && !(core & atom) ? core & allowed : 0;
}

} // namespace Common
