// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "common/logging/log.h"
#include "driveclub/render_res.h"
#include "driveclub/patch_plan.h"

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

// Heap sizes come from the game's memory budget, looked up by name. The engine's own
// higher-resolution switches (-increasedrender, -resolutionscale) need a bigger VRAM_RENDERING
// heap, and VideoPrivate (direct memory, contains VRAM_RENDERING) must grow by the same amount.
// DCPC_VRAM_EXTRA_MB=<n> adds n MiB to both by diverting two lookups to stubs in the zero padding
// after the code segment (call lookup; add rax, n MiB; original register move; jmp back):
// - eboot+0x15ad25 in the base-heap setup: VideoPrivate size (lookup 0x568a30, mov r12, rax);
// - eboot+0x15a697 in the heap creator: every heap's size (lookup 0x568a20, mov r14, rax),
//   adjusted only when the name (r15) is the "VRAM_RENDERING" literal.
// The emulator must provide the extra direct memory (General.extra_dmem_in_mbytes >= n).
bool ApplyHeap(VAddr base, u64 size) {
    const char* env = std::getenv("DCPC_VRAM_EXTRA_MB");
    const u32 mb = env ? static_cast<u32>(std::strtoul(env, nullptr, 10)) : 0;
    if (mb == 0) {
        return true;
    }
    if (mb > 2047) {
        LOG_WARNING(Loader, "driveclub-pc: DCPC_VRAM_EXTRA_MB={} above 2047, ignored", mb);
        return false;
    }
    struct Site {
        u64 addr; // call lookup (5 bytes) followed by a 3-byte mov reg, rax
        u64 lookup;
        u8 mov[3];
        u64 name; // only when r15 holds this literal (0: always)
        const char* literal;
    };
    constexpr Site kSites[] = {
        {0x15ad25, 0x568a30, {0x49, 0x89, 0xc4}, 0, nullptr},
        {0x15a697, 0x568a20, {0x49, 0x89, 0xc6}, 0x12ae992, "VRAM_RENDERING"},
    };
    constexpr u64 kCave = 0x1552000; // inside the mapped tail of the code segment
    constexpr u32 kCaveBytes = 96;
    if (kCave + kCaveBytes > size) {
        return false;
    }
    for (const auto& site : kSites) {
        if (site.addr + 8 > size ||
            (site.literal && site.name + std::strlen(site.literal) + 1 > size)) return false;
        const u8* p = reinterpret_cast<const u8*>(base + site.addr);
        s32 disp;
        std::memcpy(&disp, p + 1, 4);
        if (p[0] != 0xe8 || site.addr + 5 + disp != site.lookup ||
            std::memcmp(p + 5, site.mov, 3) != 0 ||
            (site.literal &&
             std::memcmp(reinterpret_cast<const char*>(base + site.name), site.literal,
                         std::strlen(site.literal) + 1) != 0)) {
            LOG_WARNING(Loader, "driveclub-pc: heap size site {:#x} does not match, skipped",
                        site.addr);
            return false;
        }
    }
    u8* c = reinterpret_cast<u8*>(base + kCave);
    for (u32 i = 0; i < kCaveBytes; ++i) {
        if (c[i] != 0) {
            LOG_WARNING(Loader, "driveclub-pc: heap stub area is not free, skipped");
            return false;
        }
    }
    const s32 delta = static_cast<s32>(mb << 20);
    u32 n = 0;
    const auto emit = [&](std::initializer_list<u8> bytes) {
        for (u8 v : bytes) {
            c[n++] = v;
        }
    };
    const auto emit_rel = [&](u64 target) {
        const s32 v = static_cast<s32>(target - (kCave + n + 4));
        std::memcpy(c + n, &v, 4);
        n += 4;
    };
    for (const auto& site : kSites) {
        const u64 stub = kCave + n;
        emit({0xe8}); // call lookup
        emit_rel(site.lookup);
        if (site.literal) {
            emit({0x48, 0xb9}); // mov rcx, imm64 (caller-saved, dead after the call)
            const u64 ptr = base + site.name;
            std::memcpy(c + n, &ptr, 8);
            n += 8;
            emit({0x4c, 0x39, 0xf9}); // cmp rcx, r15
            emit({0x75, 0x06});       // jne over the add
        }
        emit({0x48, 0x05}); // add rax, imm32
        std::memcpy(c + n, &delta, 4);
        n += 4;
        emit({site.mov[0], site.mov[1], site.mov[2]});
        emit({0xe9}); // jmp back
        emit_rel(site.addr + 8);
        u8* p = reinterpret_cast<u8*>(base + site.addr);
        p[0] = 0xe9; // jmp stub
        const s32 to_stub = static_cast<s32>(stub - (site.addr + 5));
        std::memcpy(p + 1, &to_stub, 4);
        std::memset(p + 5, 0x90, 3);
    }
    LOG_INFO(Loader, "driveclub-pc: VideoPrivate and VRAM_RENDERING +{} MiB ({} stub bytes)", mb,
             n);
    return true;
}

void Apply(VAddr base, u64 size) {
    const char* env = std::getenv("DCPC_RENDER_RES");
    u32 w = 0, h = 0;
    if (!env) {
        ApplyHeap(base, size);
        return;
    }
    if (std::sscanf(env, "%ux%u", &w, &h) != 2 || w < 640 || h < 360 || w > 7680 || h > 4320)
        return;
    // DCPC_RENDER_RES_SITES=video: only the video-out size (eboot+0x754c19), as in the
    // widescreen patch this is based on (widescreen.h); default all sites.
    const char* sites_env = std::getenv("DCPC_RENDER_RES_SITES");
    const bool video_only = sites_env && std::strcmp(sites_env, "video") == 0;
    PatchPlan plan{std::span<u8>{reinterpret_cast<u8*>(base), static_cast<size_t>(size)}};
    u32 considered = 0;
    for (const auto& site : kSites) {
        if (video_only && site.addr != 0x754c19 && site.addr != 0x754c1f) continue;
        ++considered;
        if (site.addr + 16 > size) {
            LOG_WARNING(Loader, "Render-size patch group outside this executable, skipped");
            return;
        }
        const bool pair = site.kind >= Kind::IntPair;
        const u64 from = pair ? Pair(site.kind, 1920, 1080) : Bits(site.kind, 1920, 1080);
        const u64 to = pair ? Pair(site.kind, w, h) : Bits(site.kind, w, h);
        const size_t length = pair ? 8 : 4;
        const auto* expected = reinterpret_cast<const u8*>(&from);
        const auto* replacement = reinterpret_cast<const u8*>(&to);
        const auto* instruction = reinterpret_cast<const u8*>(base + site.addr);
        bool found = false;
        for (size_t off = pair ? 0 : 1; off <= (pair ? 4 : 15 - length); ++off) {
            if (std::memcmp(instruction + off, expected, length) == 0) {
                found = plan.Add(site.addr + off, {expected, length}, {replacement, length});
                break;
            }
        }
        if (!found) {
            LOG_WARNING(Loader, "Render-size site {:#x} does not match: whole resolution patch "
                                "group skipped (no partial writes)", site.addr);
            return;
        }
    }
    if (!ApplyHeap(base, size)) {
        LOG_WARNING(Loader, "Required render-memory patch did not match; resolution patch skipped");
        return;
    }
    if (!plan.Commit()) {
        LOG_WARNING(Loader, "Render-size patch validation changed; patch skipped");
        return;
    }
    LOG_INFO(Loader, "driveclub-pc: render size {}x{}: {} of {} sites patched", w, h, considered,
             considered);
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
