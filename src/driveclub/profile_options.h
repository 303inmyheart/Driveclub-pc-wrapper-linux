// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>

namespace DriveclubPC {
// The emulated user-service name buffer is 16 bytes plus the terminator.
inline bool IsValidLocalUserName(std::string_view name) {
    if (name.empty() || name.size() > 16) return false;
    bool non_space = false;
    for (size_t i = 0; i < name.size();) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c == 0x7f) return false;
        non_space |= c != ' ';
        if (c < 0x80) { ++i; continue; }
        unsigned length = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 :
                          c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!length || i + length > name.size()) return false;
        for (unsigned j = 1; j < length; ++j)
            if ((static_cast<unsigned char>(name[i + j]) & 0xc0) != 0x80) return false;
        const auto second = static_cast<unsigned char>(name[i + 1]);
        if (c == 0xc2 && second <= 0x9f) return false; // Unicode C1 controls
        if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
            (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90)) return false;
        i += length;
    }
    return non_space;
}
} // namespace DriveclubPC
