#pragma once
// The game's Flash player: the script's values and the clips on screen
#include <string.h>

#include <cstdint>

#include "xdk/hle.h"

namespace flash {

constexpr uint32_t kPlayer = 0x2EC988;  // +0x18 its movies

// value: +0 vtable (+0xC its members), +4 flags: 0x10 defined, type from bit 25
inline uint32_t Type(uint32_t value) {
    return value && (kernel::R32(value + 4) & 0x10) ? kernel::R32(value + 4) >> 25 : 0;
}

// clip: +8 name (text at +8), +0x0C matrix a b c d x y, +0x44 parent, +0x48 its data, +0x50 next, +0x54 depth in
// 17 bits, +0x58 flags: 0x100000 visible, 0x10000 placed by the script
inline bool IsClip(uint32_t value) { return Type(value) >= 0xC && Type(value) <= 0x12; }

inline const char* Name(uint32_t clip) { return static_cast<const char*>(kernel::HostPtr(kernel::R32(clip + 8) + 8)); }

// a value's members: +0 slots, +4 {name, value} each, a name's text at +8
inline uint32_t Members(X86Context& ctx, uint32_t value) {
    if (!Type(value)) return 0;
    ctx.ecx = value;
    return kernel::CallGuest(ctx, kernel::R32(kernel::R32(value) + 0xC), {});
}

inline uint32_t Find(uint32_t members, const char* name) {
    uint32_t slots = members ? kernel::R32(members) : 0, table = members ? kernel::R32(members + 4) : 0;
    for (uint32_t i = 0; table && slots <= 0x1000 && i < slots; i++) {
        uint32_t key = kernel::R32(table + i * 8), value = kernel::R32(table + i * 8 + 4);
        // 0x2AC7EC the name of a deleted member
        if (key && key != 0x2AC7EC && _stricmp(static_cast<const char*>(kernel::HostPtr(key + 8)), name) == 0)
            return Type(value) ? value : 0;
    }
    return 0;
}

inline uint32_t Member(X86Context& ctx, uint32_t value, const char* name) { return Find(Members(ctx, value), name); }

// the movie at a level, menu pages are at 4
inline uint32_t Level(int level) {
    uint32_t player = kernel::R32(kPlayer), movies = player ? kernel::R32(player + 0x18) : 0;
    for (uint32_t clip = movies ? kernel::R32(movies) : 0; clip; clip = kernel::R32(clip + 0x50))
        if (int32_t(kernel::R32(clip + 0x54) << 15) >> 15 == level) return Type(clip) == 0x12 ? clip : 0;
    return 0;
}

// a sprite's first child, the rest follow at +0x50
inline uint32_t FirstChild(uint32_t clip) {
    if (Type(clip) != 0xD && Type(clip) != 0x12) return 0;
    uint32_t data = kernel::R32(clip + 0x48), list = data && data != 0xBAADF00D ? kernel::R32(data + 0x24) : 0;
    uint32_t head = list ? kernel::R32(list) : 0;
    return head ? kernel::R32(head + 0x50) : 0;
}

}  // namespace flash
