// Button legends on keyboard and mouse: a menu prompt's icon goes and its text leads with the key, "(ENTER) confirm".
// Texts naming the pad's buttons name the keys. A change of device sets the texts on screen again.
#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "common/mouse_keyboard.h"
#include "game/flash.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr uint32_t kResetTexts = 0xECA90;  // every text on screen is made again from its string
constexpr uint32_t kPlayerReady = 0x2ED408;
// string ids
constexpr uint32_t kConfirm = 0xE0CA7C5F, kBack = 0x64199B75, kBackspace = 0x32D594BD;
constexpr uint32_t kPressStart[] = {0x2FCCE936, 0x905E4EE0, 0x661A3A37};
constexpr uint32_t kButtonA = 0x53773143, kButtonB = 0xCA7E60F9;  // hints in play
constexpr float kIconWidth = 28.0f;  // a prompt's text starts this far right of its icon's left edge

void Replace(std::string& text, std::string_view from, std::string_view to) {
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
        text.replace(at, from.size(), to);
}

// a string as the keyboard words it, empty when it stays
std::string Wording(uint32_t id, const std::string& pad) {
    std::string text = pad;
    if (id == kConfirm) text = "(ENTER) " + text;
    else if (id == kBack || id == kBackspace) text = "(ESC) " + text;
    else if (std::find(std::begin(kPressStart), std::end(kPressStart), id) != std::end(kPressStart)) {
        Replace(text, "start", "enter");
        Replace(text, "START", "ENTER");
    } else if (id != kButtonA && id != kButtonB) {
        // the font's A and B button glyphs
        Replace(text, "\xC2\xB2", "ENTER");
        Replace(text, "\xC2\xB9", "ESC");
    }
    return text == pad ? std::string() : text;
}

std::mutex g_lock;
std::unordered_map<uint32_t, uint32_t> g_wordings;  // string id to its keyboard wording in guest memory, 0 for none

// a prompt is a clip of a "Button" icon and a "Text": on the keyboard the icon goes and the text takes its place
void Prompts(uint32_t clip, bool keyboard, int depth) {
    int left = 4096;
    for (uint32_t child = flash::FirstChild(clip); child && left; child = R32(child + 0x50), left--) {
        uint32_t icon = 0, text = 0;
        for (uint32_t part = flash::FirstChild(child); part && left; part = R32(part + 0x50), left--) {
            if (!flash::IsClip(part)) continue;
            if (_stricmp(flash::Name(part), "Button") == 0) icon = part;
            if (_stricmp(flash::Name(part), "Text") == 0) text = part;
        }
        if (!icon || !text) {
            if (depth < 8) Prompts(child, keyboard, depth + 1);
            continue;
        }
        bool shown = R32(icon + 0x58) & 0x100000;
        if (shown != keyboard) continue;
        W32(icon + 0x58, R32(icon + 0x58) ^ 0x100000);
        G<float>(text + 0x1C) += keyboard ? -kIconWidth : kIconWidth;
        W32(text + 0x58, R32(text + 0x58) | 0x10000);
    }
}

}  // namespace

// thiscall(id): the string of that id, UTF-8
X86_EXTERN_FUNC(__imp__sub_001F3A80);
X86_FUNC(sub_001F3A80) {
    uint32_t id = R32(ctx.esp + 4);
    __imp__sub_001F3A80(ctx, base);
    if (!ctx.eax || !input::KeyboardActive()) return;
    std::lock_guard lock(g_lock);
    auto [it, fresh] = g_wordings.try_emplace(id, 0);
    if (fresh) {
        std::string text = Wording(id, ReadCString(ctx.eax, 1024));
        if (!text.empty() && (it->second = mem::AllocSystem(uint32_t(text.size() + 1))))
            std::memcpy(HostPtr(it->second), text.c_str(), text.size() + 1);
    }
    if (it->second) ctx.eax = it->second;
}

// the front end's tick, before the Flash player runs
void MenuPrompts(X86Context& ctx, uint8_t*) {
    static int was = -1;
    bool keyboard = input::KeyboardActive();
    if (int(keyboard) != was) {
        if (was >= 0 && R32(kPlayerReady) == 1 && R32(flash::kPlayer)) {
            X86Context saved = ctx;
            CallGuest(ctx, kResetTexts, {});
            ctx = saved;
        }
        was = keyboard;
    }
    Prompts(flash::Level(4), keyboard, 0);
}
