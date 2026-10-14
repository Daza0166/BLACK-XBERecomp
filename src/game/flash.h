#pragma once
// The game's Flash player: the script's values and the clips on screen
#include <string.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

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

// the script: calls into it and its members set from here
constexpr uint32_t kInterpreter = 0x562C08;
constexpr uint32_t kSetMember = 0x100DB0;   // eax value or 0 to delete, edx name, ecx 0; (interpreter, object, 1, 0, 1)
constexpr uint32_t kNewString = 0xF5600;    // cdecl (text): a string value
constexpr uint32_t kCall = 0x101420;        // eax object path; cdecl (function, result, count, texts...)
constexpr uint32_t kCallValues = 0x101260;  // the same, its arguments values
constexpr uint32_t kRunActions = 0x10F180;  // (player): the frame actions waiting, a new clip's stop() among them

// a name the player may keep: +0 its handle, +4 uses, length, room and hash, +12 the text
inline uint32_t KeptName(const char* text) {
    static std::map<std::string, uint32_t> names;
    uint32_t& at = names[text];
    if (!at) {
        uint32_t length = uint32_t(std::strlen(text));
        at = mem::AllocSystem(length + 13);
        kernel::W32(at, at + 4);
        kernel::G<uint16_t>(at + 4) = 0x4000;
        kernel::G<uint16_t>(at + 6) = kernel::G<uint16_t>(at + 8) = uint16_t(length);
        kernel::G<uint16_t>(at + 10) = 0;
        std::memcpy(kernel::HostPtr(at + 12), text, length + 1);
    }
    return at;
}

inline uint32_t Text(const char* text) { return KeptName(text) + 12; }

inline uint32_t String(X86Context& ctx, const char* text) { return kernel::CallGuest(ctx, kNewString, {Text(text)}); }

inline bool Set(X86Context& ctx, uint32_t object, const char* name, uint32_t value) {
    ctx.eax = value;
    ctx.edx = KeptName(name);
    ctx.ecx = 0;
    return kernel::CallGuest(ctx, kSetMember, {kInterpreter, object, 1, 0, 1}) & 0xFF;
}

// a text field in a clip, the one showing that string or the first: data +0x1C its string's name
inline uint32_t FindField(uint32_t clip, const char* shows, int depth = 0) {
    int left = 256;
    for (uint32_t child = FirstChild(clip); child && left; child = kernel::R32(child + 0x50), left--) {
        uint32_t data = Type(child) == 0xF ? kernel::R32(child + 0x48) : 0;
        const char* name = data ? static_cast<const char*>(kernel::HostPtr(kernel::R32(data + 0x1C) + 8)) : nullptr;
        if (data && (!shows || std::strcmp(name, shows) == 0)) return child;
        if (uint32_t found = depth < 4 ? FindField(child, shows, depth + 1) : 0) return found;
    }
    return 0;
}

// a method of a script object's class or of one that extends: members +8 the class
inline uint32_t Method(X86Context& ctx, uint32_t object, const char* name) {
    for (int up = 0; up < 4; up++) {
        uint32_t members = Members(ctx, object);
        if (!members) break;
        object = kernel::R32(members + 8);
        if (uint32_t found = Find(Members(ctx, object), name)) return found;
    }
    return 0;
}

// a clip the page's script takes for one of its button objects, such as like
inline bool Button(X86Context& ctx, uint32_t clip, uint32_t like) {
    uint32_t frame = Member(ctx, like, "HighlightFrame"), on = Method(ctx, like, "HighLight");
    uint32_t off = Method(ctx, like, "UnHighLight");
    return frame && on && off && Set(ctx, clip, "movieClip", clip) && Set(ctx, clip, "HighlightFrame", frame) &&
           Set(ctx, clip, "HighLight", on) && Set(ctx, clip, "UnHighLight", off);
}

inline void Link(X86Context& ctx, uint32_t above, uint32_t below) {
    Set(ctx, above, "buttonDown", below);
    Set(ctx, below, "buttonUp", above);
}

inline void Focus(X86Context& ctx, uint32_t button) {
    ctx.eax = Text("_level4.MyMenu");
    kernel::CallGuest(ctx, kCallValues, {Text("SetFocusButton"), 0, 1, button});
}

}  // namespace flash
