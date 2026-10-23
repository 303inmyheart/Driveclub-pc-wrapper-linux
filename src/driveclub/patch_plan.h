// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace DriveclubPC {

// Loader-side patch transaction: validate every range before changing any bytes.
class PatchPlan {
public:
    explicit PatchPlan(std::span<std::uint8_t> image) : image{image} {}

    bool Add(std::size_t offset, std::span<const std::uint8_t> expected,
             std::span<const std::uint8_t> replacement) {
        if (!valid || expected.empty() || expected.size() != replacement.size() ||
            offset > image.size() || expected.size() > image.size() - offset) {
            valid = false;
            return false;
        }
        for (const auto& write : writes) {
            if (offset < write.offset + write.expected.size() &&
                write.offset < offset + expected.size()) {
                valid = false;
                return false;
            }
        }
        if (!std::equal(expected.begin(), expected.end(), image.begin() + offset)) {
            valid = false;
            return false;
        }
        writes.push_back({offset, {expected.begin(), expected.end()},
                         {replacement.begin(), replacement.end()}});
        return true;
    }

    bool Commit() {
        if (!valid) return false;
        for (const auto& write : writes) {
            if (!std::equal(write.expected.begin(), write.expected.end(),
                            image.begin() + write.offset)) return false;
        }
        for (const auto& write : writes)
            std::copy(write.replacement.begin(), write.replacement.end(), image.begin() + write.offset);
        return true;
    }

private:
    struct Write {
        std::size_t offset;
        std::vector<std::uint8_t> expected, replacement;
    };
    std::span<std::uint8_t> image;
    std::vector<Write> writes;
    bool valid = true;
};

} // namespace DriveclubPC
