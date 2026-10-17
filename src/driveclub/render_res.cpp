// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/logging/log.h"
#include "driveclub/render_res.h"

namespace DriveclubPC::RenderRes {

namespace {

enum class Kind : u8 {
    IntW,
    IntH,
    FloatW,
    FloatH,
    IntPair,      // 8 bytes: u32 width, u32 height
    FloatPair,    // 8 bytes: f32 width, f32 height
    InvFloatPair, // 8 bytes: 1 / width, 1 / height
};

struct Site {
    u64 addr; // instruction address in the eboot image
    Kind kind;
};

// v1.28 sites found by scripts/find_imm_pairs.py (1920 / 1080 close together).
constexpr std::array kSites = {
    Site{0x25c605, Kind::FloatW},
    Site{0x25c60f, Kind::FloatH}, // viewport-like floats
    Site{0x25c745, Kind::FloatW},
    Site{0x25c74f, Kind::FloatH},
    Site{0x26450b, Kind::IntW},
    Site{0x264512, Kind::IntH}, // struct width/height
    Site{0x264956, Kind::IntW},
    Site{0x26495d, Kind::IntH},
    Site{0x268d61, Kind::IntW},
    Site{0x268d66, Kind::IntH}, // call arguments
    Site{0x281402, Kind::FloatW},
    Site{0x28140e, Kind::FloatH},
    Site{0x5b7c56, Kind::IntW},
    Site{0x5b7c5b, Kind::IntH},
    Site{0x754c19, Kind::IntW},
    Site{0x754c1f, Kind::IntH},
    // movabs immediates holding both values
    Site{0x268df2, Kind::IntPair},
    Site{0x281338, Kind::FloatPair},
    Site{0x281796, Kind::FloatPair},
};

// Read-only data holding both values (DCPC_RENDER_RES_DATA=1, separate experiment).
constexpr std::array kDataSites = {
    Site{0x1243a10, Kind::IntPair},      Site{0x12afe74, Kind::FloatPair},
    Site{0x12c5608, Kind::FloatPair},    Site{0x12c5618, Kind::FloatPair},
    Site{0x12cf348, Kind::FloatPair},    Site{0x12cf304, Kind::InvFloatPair},
    Site{0x12cf320, Kind::InvFloatPair},
};

u64 Pair(Kind kind, u32 w, u32 h) {
    u32 a = w, b = h;
    if (kind == Kind::FloatPair) {
        a = std::bit_cast<u32>(static_cast<float>(w));
        b = std::bit_cast<u32>(static_cast<float>(h));
    } else if (kind == Kind::InvFloatPair) {
        a = std::bit_cast<u32>(1.0f / static_cast<float>(w));
        b = std::bit_cast<u32>(1.0f / static_cast<float>(h));
    }
    return u64{a} | (u64{b} << 32);
}

bool PatchPair(VAddr base, const Site& site, u32 w, u32 h, bool exact) {
    const u64 from = Pair(site.kind, 1920, 1080);
    const u64 to = Pair(site.kind, w, h);
    u8* p = reinterpret_cast<u8*>(base + site.addr);
    // Data sites are exact; a movabs carries the value after its two opcode bytes.
    for (u32 off = 0; off <= (exact ? 0u : 4u); ++off) {
        u64 value;
        std::memcpy(&value, p + off, 8);
        if (value == from) {
            std::memcpy(p + off, &to, 8);
            return true;
        }
    }
    return false;
}

u32 Bits(Kind kind, u32 w, u32 h) {
    switch (kind) {
    case Kind::IntW:
        return w;
    case Kind::IntH:
        return h;
    case Kind::FloatW:
        return std::bit_cast<u32>(static_cast<float>(w));
    case Kind::FloatH:
        return std::bit_cast<u32>(static_cast<float>(h));
    default:
        break;
    }
    return 0;
}

} // namespace

// The VRAM_RENDERING heap's base size (512 MiB) in the game's memory setup (0x149a80):
// mov qword ptr [rbp - 0x98], 0x20000000. Larger render targets (the game's own -increasedrender
// / -resolutionscale switches) do not fit in it; DCPC_RENDER_HEAP_MB raises it. The world heap
// takes the remainder of VideoPrivate, so General.extra_dmem_in_mbytes should grow with it.
void ApplyHeap(VAddr base, u64 size) {
    const char* env = std::getenv("DCPC_RENDER_HEAP_MB");
    const u32 mb = env ? static_cast<u32>(std::strtoul(env, nullptr, 10)) : 0;
    if (mb == 0) {
        return;
    }
    if (mb < 512 || mb > 2047) {
        LOG_WARNING(Loader, "driveclub-pc: DCPC_RENDER_HEAP_MB={} out of range 512..2047", mb);
        return;
    }
    // Two memory setups carry the same table: 0x149a80 ([rbp - 0x98]) and 0x5b5310 ([rbp - 0x90]).
    struct HeapSite {
        u64 addr;
        u8 disp;
    };
    constexpr HeapSite kHeapSites[] = {{0x149e50, 0x68}, {0x5b5576, 0x70}};
    const u32 bytes = mb << 20;
    for (const auto& site : kHeapSites) {
        const u8 expected[] = {0x48, 0xc7, 0x85, site.disp, 0xff, 0xff,
                               0xff, 0x00, 0x00, 0x00,      0x20};
        u8* insn = reinterpret_cast<u8*>(base + site.addr);
        if (site.addr + sizeof(expected) > size ||
            std::memcmp(insn, expected, sizeof(expected)) != 0) {
            LOG_WARNING(Loader, "driveclub-pc: render heap site {:#x} does not match, skipped",
                        site.addr);
            continue;
        }
        std::memcpy(insn + 7, &bytes, 4);
        LOG_INFO(Loader, "driveclub-pc: VRAM_RENDERING base size {} MiB at {:#x}", mb, site.addr);
    }
}

void Apply(VAddr base, u64 size) {
    ApplyHeap(base, size);
    const char* env = std::getenv("DCPC_RENDER_RES");
    u32 w = 0, h = 0;
    if (!env || std::sscanf(env, "%ux%u", &w, &h) != 2 || w < 640 || h < 360 || w > 7680 ||
        h > 4320) {
        return;
    }
    u32 patched = 0;
    for (const auto& site : kSites) {
        if (site.kind >= Kind::IntPair) {
            if (site.addr + 16 <= size && PatchPair(base, site, w, h, false)) {
                ++patched;
            } else {
                LOG_WARNING(Loader, "driveclub-pc: render size site {:#x} does not match, skipped",
                            site.addr);
            }
            continue;
        }
        const u32 from = Bits(site.kind, 1920, 1080);
        const u32 to = Bits(site.kind, w, h);
        if (site.addr + 16 > size) {
            continue;
        }
        // The immediate is the last four bytes of the instruction; search its first 15 bytes.
        u8* insn = reinterpret_cast<u8*>(base + site.addr);
        bool done = false;
        for (u32 off = 1; off + 4 <= 15 && !done; ++off) {
            u32 value;
            std::memcpy(&value, insn + off, 4);
            if (value == from) {
                std::memcpy(insn + off, &to, 4);
                done = true;
            }
        }
        if (done) {
            ++patched;
        } else {
            LOG_WARNING(Loader, "driveclub-pc: render size site {:#x} does not match, skipped",
                        site.addr);
        }
    }
    LOG_INFO(Loader, "driveclub-pc: render size {}x{}: {} of {} sites patched", w, h, patched,
             kSites.size());
    if (const char* data = std::getenv("DCPC_RENDER_RES_DATA"); data && data[0] == '1') {
        u32 data_patched = 0;
        for (const auto& site : kDataSites) {
            if (site.addr + 8 <= size && PatchPair(base, site, w, h, true)) {
                ++data_patched;
            }
        }
        LOG_INFO(Loader, "driveclub-pc: render size data: {} of {} sites patched", data_patched,
                 kDataSites.size());
    }
}

} // namespace DriveclubPC::RenderRes
