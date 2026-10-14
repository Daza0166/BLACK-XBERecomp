// Recomp settings in the game's own options menus: RECOMP SETTINGS and MOUSE AND KEYBOARD buttons under the stock ones
// load the page again with the F10 panel's rows in their place. A row is two copies of a stock button clip, its name
// and its value; the name's copy stands in for the script's button object, so the script moves the focus and the
// cursor works. The pause menu's options have room for one button, its rows start with the panel's PAGE row.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/translations.h"
#include "core/log.h"
#include "game/flash.h"
#include "xdk/hle.h"

using namespace kernel;
using app::rows::Row;
using flash::Button;
using flash::Find;
using flash::FindField;
using flash::Focus;
using flash::Link;
using flash::Member;
using flash::Members;
using flash::Method;
using flash::Set;
using flash::String;
using flash::Text;

namespace {

constexpr uint32_t kPlayerReady = 0x2ED408, kHashTable = 0x2AC908;
const char* const kCaptions[] = {"RECOMP SETTINGS", "MOUSE AND KEYBOARD"};
constexpr int kEntries = int(std::size(kCaptions)), kSlots = 8, kDepth = 100;
constexpr int kEscape = 0x1B;
const Row kPageRow{app::rows::Kind::Value, -1, "PAGE"};

// the selection bar, Main.ButtonSelector: its parts by depth; once shown it stops on frame 10, data +0x18 9
constexpr int kBarRest = 9;
constexpr int kBarBox = 1;
constexpr int kBarLine = 5;
constexpr int kBarTick = 7;
constexpr int kBarScan = 11;
constexpr float kBarLeft = -5.0f;  // the box's left end and the left tick
constexpr float kTickGap = 10.0f;  // past the end of the widest value

// the rows' font, as the name entry's caret measures it
constexpr uint32_t kFonts = 0x2D45F0;
constexpr uint32_t kFindFont = 0x13CD0;   // stdcall (fonts, name): a font
constexpr uint32_t kFontName = 0x28E63C;  // "Eurostile LT Std"
constexpr float kTextHeight = 15.0f;      // the stock buttons' text fields
constexpr float kTextLeft = 2.0f;         // the text field in its button's label clip

// the stock bar on frame 10: its box's x scale, the x of its right tick, right line and Scan
struct Bar {
    float box;
    float tick;
    float line;
    float scan;
};

// an options page of the game
struct Host {
    const char* page;
    const char* holder;        // the clip in Main its buttons are in, null for Main
    const char* button;        // a stock button, copied for ours
    const char* under;         // the clip our buttons go under, the first this far down
    float drop;
    const char* first, *last;  // the first and last button objects of its ring
    int entries;               // our buttons; one opens both pages
    float pitch, name_x, value_x, scale;  // the rows, from the stock button
    const char* hint;          // the string of a prompt our button takes the place of
    const char* stock[14];     // what the rows take the place of
    Bar bar;
};
constexpr Host kHosts[] = {
    {"OptionsMenuXbox", "TextAnim", "_level4.Main.TextAnim.ControllerSetup_Text", "Playerprofile_Text", 25.0f,
     "controllerButton", "Playerprofile", 2, 25.0f, -37.0f, 185.0f, 0.9f, nullptr,
     {"ControllerSetup_Text", "SoundSetup_Text", "PictureSetup_Text", "Playerprofile_Text"},
     {1.0f, 245.0f, 445.0f, -5.0f}},
    {"PauseMenuOptionXbox", nullptr, "_level4.Main.ToggleCrouch", "ToggleCrouch", 222.0f,
     "toggleCrouchButton", "musicVolumeButton", 1, 27.0f, 0.0f, 245.0f, 1.0f, "$FE_SELECT",
     {"ToggleCrouch", "ToggleCrouchOn_Text", "ToggleCrouchOff_Text", "InvertLook", "InvertLookOn_Text",
      "InvertLookOff_Text", "Vibration", "VibrationOn_Text", "VibrationOff_Text", "SFXVolume", "Sound_Volume",
      "MusicVolume", "Music_Volume"},
     {1.4f, 343.55f, 443.55f, 168.35f}},
};

// a text the page shows from here: its string id, the text, the data of the field showing it
struct Field {
    uint32_t id, text, data;
};
enum { kTitle, kEntry, kName = kEntry + kEntries, kValue = kName + kSlots, kFields = kValue + kSlots };
constexpr uint32_t kFieldSize = 64;
Field g_fields[kFields];

struct Page {
    const Host* host = nullptr;
    int mode = -1;  // 0 the stock buttons and ours, from 1 the rows of kCaptions[mode - 1]
    bool failed = false;
    uint32_t first = 0, lead = 0;  // our first clip, the stock clip ours keep their places from
    uint32_t entry[kEntries] = {}, name[kSlots] = {}, value[kSlots] = {};
    size_t slots = 0, top = 0;
    std::vector<const Row*> rows;  // null restores the defaults
    int capture = 0;  // a key binding: 1 the keys come up, 2 the next key binds or Escape leaves, 3 they come up again
    size_t bind = 0;  // its row
};
Page g_page;     // game thread
int g_next = 0;  // the mode of the page being loaded
int g_last = 0;  // the mode before it, whose button takes the focus back

// the id of a string by its name
uint32_t Hash(const char* text) {
    uint32_t h = ~0u;
    for (; *text; text++) h = uint32_t(int32_t(h) >> 8) ^ R32(kHashTable + ((h ^ uint8_t(*text)) & 0xFF) * 4);
    return h;
}

// that field shows field n from now on
bool Show(X86Context& ctx, int n, uint32_t clip, const char* shows = nullptr, const char* text = nullptr) {
    Field& f = g_fields[n];
    char name[16];
    std::snprintf(name, sizeof(name), "$RC_%d", n);
    if (!f.text) {
        f.id = Hash(name + 1);
        f.text = mem::AllocSystem(kFieldSize);
        G<char>(f.text) = 0;
    }
    uint32_t field = FindField(clip, shows);
    if (!field) return false;
    f.data = R32(field + 0x48);
    if (text) std::snprintf(static_cast<char*>(HostPtr(f.text)), kFieldSize, "%s", text);
    return Set(ctx, field, "variable", String(ctx, name));
}

void Write(Field& f, const std::string& text) {
    char* shown = static_cast<char*>(HostPtr(f.text));
    if (!f.data || std::strncmp(text.c_str(), shown, kFieldSize - 1) == 0) return;
    std::snprintf(shown, kFieldSize, "%s", text.c_str());
    // made again at the next draw
    W32(f.data + 0x6C, (R32(f.data + 0x6C) & ~1u) | 0x204);
}

// a copy of the stock button beside it, stopped: data +0x1C bit 25 playing
uint32_t Copy(X86Context& ctx, uint32_t holder, const char* name, int depth) {
    char number[12];
    std::snprintf(number, sizeof(number), "%d", depth);
    ctx.eax = Text(g_page.host->button);
    CallGuest(ctx, flash::kCall, {Text("duplicateMovieClip"), 0, 2, Text(name), Text(number)});
    uint32_t clip = Member(ctx, holder, name);
    if (flash::Type(clip) != 0xD) return 0;
    uint32_t data = R32(clip + 0x48);
    W32(data + 0x1C, R32(data + 0x1C) & ~0x2000000u);
    return clip;
}

// +0x58 bit 20 shown
void Hide(uint32_t clip) {
    if (flash::IsClip(clip)) W32(clip + 0x58, R32(clip + 0x58) & ~0x100000u);
}

// ours keep their places from a stock clip, which the page slides and fades in: +0x1C x, y, then its colours
void Follow(const Page& p) {
    const Host& h = *p.host;
    auto follow = [&p](uint32_t clip, float x, float y) {
        if (!clip) return;
        G<float>(clip + 0x1C) = G<float>(p.lead + 0x1C) + x;
        G<float>(clip + 0x20) = G<float>(p.lead + 0x20) + y;
        std::memcpy(HostPtr(clip + 0x24), HostPtr(p.lead + 0x24), 0x20);
    };
    for (int i = 0; i < kEntries; i++) follow(p.entry[i], 0.0f, h.drop + h.pitch * float(i));
    for (int i = 0; i < kSlots; i++) {
        follow(p.name[i], h.name_x, h.pitch * float(i));
        follow(p.value[i], h.value_x, h.pitch * float(i));
    }
}

void Sound(X86Context& ctx) {
    ctx.eax = Text("_level4.MyMenu");
    CallGuest(ctx, flash::kCall, {Text("PlayChangeSound"), 0, 0});
}

// our buttons under the stock ones, in their ring
bool Entries(X86Context& ctx, uint32_t root, uint32_t holder) {
    Page& p = g_page;
    const Host& h = *p.host;
    uint32_t top = Member(ctx, root, h.first), above = Member(ctx, root, h.last);
    p.lead = Member(ctx, holder, h.under);
    if (!top || !above || !flash::IsClip(p.lead)) return false;
    for (int i = 0; i < h.entries; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_E%d", i);
        if (!(p.entry[i] = Copy(ctx, holder, name, kDepth + i))) return false;
        if (!Show(ctx, kEntry + i, p.entry[i], nullptr, app::Ui(kCaptions[i]).c_str())) return false;
    }
    p.first = p.entry[0];
    Follow(p);
    CallGuest(ctx, flash::kRunActions, {R32(flash::kPlayer)});
    for (int i = 0; i < h.entries; i++) {
        if (!Button(ctx, p.entry[i], top) || !Set(ctx, p.entry[i], "actionSelectPage", String(ctx, h.page))) return false;
        Link(ctx, above, p.entry[i]);
        above = p.entry[i];
    }
    Link(ctx, above, top);
    if (h.hint)
        for (uint32_t clip = FindField(holder, h.hint); clip; clip = R32(clip + 0x44))
            if (R32(clip + 0x44) == holder) Hide(clip);
    return true;
}

// the rows of the page's mode, behind the PAGE row where one button opens both
void Fill(Page& p) {
    p.rows.clear();
    if (p.host->entries == 1) p.rows.push_back(&kPageRow);
    size_t count = 0;
    const Row* list = app::rows::Page(p.mode - 1, &count);
    for (size_t i = 0; i < count; i++)
        if (list[i].kind != app::rows::Kind::Heading) p.rows.push_back(&list[i]);
    if (p.mode > 1) p.rows.push_back(nullptr);
    p.slots = std::min(p.slots, p.rows.size());
    p.top = 0;
}

// the panel's rows in place of the stock buttons
bool Rows(X86Context& ctx, uint32_t root, uint32_t holder, uint32_t menu) {
    Page& p = g_page;
    const Host& h = *p.host;
    uint32_t like = Member(ctx, root, h.first), nothing = Method(ctx, like, "Select");
    p.lead = Member(ctx, holder, std::strrchr(h.button, '.') + 1);
    if (!nothing || !flash::IsClip(p.lead)) return false;
    p.slots = kSlots;
    Fill(p);
    for (size_t i = 0; i < p.slots; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_N%d", int(i));
        p.name[i] = Copy(ctx, holder, name, kDepth + int(i));
        name[3] = 'V';
        p.value[i] = Copy(ctx, holder, name, kDepth + kSlots + int(i));
        if (!p.name[i] || !p.value[i]) return false;
        for (uint32_t clip : {p.name[i], p.value[i]}) G<float>(clip + 0xC) = G<float>(clip + 0x18) = h.scale;
        if (!Show(ctx, kName + int(i), p.name[i]) || !Show(ctx, kValue + int(i), p.value[i])) return false;
    }
    p.first = p.name[0];
    Follow(p);
    CallGuest(ctx, flash::kRunActions, {R32(flash::kPlayer)});
    for (size_t i = 0; i < p.slots; i++) {
        // a confirm on a button with no action is a back
        if (!Button(ctx, p.name[i], like) || !Set(ctx, p.name[i], "actionSelectFunction", nothing)) return false;
        if (i) Link(ctx, p.name[i - 1], p.name[i]);
    }
    for (const char* stock : h.stock)
        if (stock) Hide(Member(ctx, holder, stock));
    Show(ctx, kTitle, holder, "$FE_OPTIONS");
    // back is the options page, not its own back
    Set(ctx, menu, "mfuncFunctionBack", 0);
    Set(ctx, menu, "mstrPageBack", String(ctx, h.page));
    Focus(ctx, p.name[0]);
    return true;
}

// what row i of the page shows as its value
std::string ValueText(const Page& p, size_t i) {
    const Row* row = p.rows[i];
    if ((p.capture == 1 || p.capture == 2) && p.bind == i) return app::Ui("PRESS A KEY");
    if (row == &kPageRow) return app::Ui(kCaptions[p.mode - 1]);
    if (!row) return " ";
    std::string value = app::rows::Value(*row);
    // key names as they are
    if (row->kind == app::rows::Kind::Bind && value != "UNBOUND") return value;
    return app::Ui(value);
}

// font +0x1C its last glyph, +0x20 its glyphs by code & 0x7F, each 0x28 bytes on with +0x1C the code
uint32_t Glyph(uint32_t font, uint16_t code) {
    uint32_t glyph = R32(font + 0x20 + (code & 0x7F) * 4);
    uint32_t last = R32(font + 0x1C);
    if (!glyph) return last;
    for (int left = 256; G<uint16_t>(glyph + 0x1C) != code && glyph != last && left; left--) glyph += 0x28;
    return glyph;
}

// UTF-8 text's characters, as the font codes its glyphs
std::vector<uint16_t> Codes(const std::string& text) {
    std::vector<uint16_t> codes;
    for (size_t i = 0; i < text.size(); i++) {
        uint8_t c = uint8_t(text[i]);
        if (c >= 0xE0 && i + 2 < text.size()) {
            codes.push_back(uint16_t((c & 0x0F) << 12 | (text[i + 1] & 0x3F) << 6 | (text[i + 2] & 0x3F)));
            i += 2;
        } else if (c >= 0xC0 && i + 1 < text.size()) {
            codes.push_back(uint16_t((c & 0x1F) << 6 | (text[i + 1] & 0x3F)));
            i++;
        } else {
            codes.push_back(c);
        }
    }
    return codes;
}

// a text's width at a height of 1, as 0xC3AF0: font +0x08 its size, +0x10 a spacing; glyph +0x08 width, +0x10
// left, +0x18 advance, +0x20 spaced
float Measure(uint32_t font, const std::string& text) {
    std::vector<uint16_t> codes = Codes(text);
    if (!font || codes.empty()) return 0.0f;
    uint32_t glyph = Glyph(font, codes[0]);
    float x = -G<float>(glyph + 0x10);
    for (size_t i = 1; i < codes.size(); i++) {
        x += G<float>(glyph + 0x18);
        glyph = Glyph(font, codes[i]);
    }
    float spacing = R32(glyph + 0x20) ? G<float>(font + 0x10) : 1.0f;
    return G<float>(font + 0x08) * (x + G<float>(glyph + 0x10) + G<float>(glyph + 0x08) * spacing);
}

// how far right of the focused name the widest value of the page ends, the label clip at +0xC scaled as it lights
float Reach(X86Context& ctx, const Page& p, uint32_t focus) {
    const Host& h = *p.host;
    uint32_t font = CallGuest(ctx, kFindFont, {R32(kFonts), kFontName});
    uint32_t label = flash::FirstChild(focus);
    float scale = h.scale * (label ? G<float>(label + 0xC) : 1.0f);
    // capture text left out so the bar keeps its width while a key is bound
    Page shown = p;
    shown.capture = 0;
    float widest = 0.0f;
    for (size_t i = 0; i < p.rows.size(); i++) widest = std::max(widest, Measure(font, ValueText(shown, i)));
    return h.value_x - h.name_x + (kTextLeft + widest * kTextHeight) * scale + kTickGap;
}

// the bar on the focused row reaches past the values, its ends and line as stock: +0xC x scale, +0x1C x
void StretchBar(X86Context& ctx, const Page& p, uint32_t root, uint32_t focus) {
    const Bar& stock = p.host->bar;
    uint32_t bar = Member(ctx, Member(ctx, root, "Main"), "ButtonSelector");
    if (flash::Type(bar) != 0xD) return;
    uint32_t data = R32(bar + 0x48);
    if (!data || data == 0xBAADF00D || R32(data + 0x18) != kBarRest) return;
    float right = std::max(stock.tick, Reach(ctx, p, focus));
    float stretch = (right - kBarLeft) / (stock.tick - kBarLeft);
    int left = 256;
    for (uint32_t part = flash::FirstChild(bar); part && left; part = R32(part + 0x50), left--) {
        int depth = int32_t(R32(part + 0x54) << 15) >> 15;
        if (depth == kBarBox) G<float>(part + 0xC) = stock.box * stretch;
        if (depth == kBarLine) G<float>(part + 0x1C) = stock.line + right - stock.tick;
        if (depth == kBarTick) G<float>(part + 0x1C) = right;
        if (depth == kBarScan) {
            G<float>(part + 0xC) = stretch;
            G<float>(part + 0x1C) = kBarLeft + (stock.scan - kBarLeft) * stretch;
        }
    }
}

// Delete on a binding row
void Clear(X86Context& ctx, const Row* row) {
    if (!row || row->kind != app::rows::Kind::Bind) return;
    std::string was = app::rows::Value(*row);
    input::Unbind(size_t(row->id));
    if (app::rows::Value(*row) != was) Sound(ctx);
}

// front end: +0x44C the menu input of this tick, 1 to 6 up down left right confirm back, +0x454 input on, +0x455 held
// off, +0x381C 2 once the page is ready, +0x3840 its name, +0x3884 another page asked for, +0x388C page leaving, 1 as
// it goes
void Tick(X86Context& ctx, uint32_t ui) {
    // read every tick, so a Delete that answers a capture is not a fresh press once it ends
    bool clear = input::TakeDeletePress();
    Page& p = g_page;
    const char* name = static_cast<const char*>(HostPtr(ui + 0x3840));
    const Host* host = std::find_if(std::begin(kHosts), std::end(kHosts), [name](const Host& h) { return std::strcmp(name, h.page) == 0; });
    if (host == std::end(kHosts) || R32(kPlayerReady) != 1 || !R32(flash::kPlayer) || R32(ui + 0x381C) != 2) {
        if (*name && host == std::end(kHosts)) g_next = g_last = 0;
        p = {};
        return;
    }
    uint32_t going = R32(ui + 0x388C);
    bool leaving = going || R32(ui + 0x3884) == 1;
    uint32_t root = flash::Level(4), holder = Member(ctx, root, "Main"), menu = Member(ctx, root, "MyMenu");
    if (host->holder) holder = Member(ctx, holder, host->holder);
    if (!holder || !menu) return;
    if (p.failed) {
        if (going == 1) p = {};
        return;
    }
    if (p.mode < 0 || Member(ctx, holder, p.mode ? "RC_N0" : "RC_E0") != p.first) {
        if (leaving) return;
        p = {};
        for (Field& f : g_fields) f.data = 0;
        p.host = host;
        p.mode = g_next;
        g_next = 0;
        p.failed = !(p.mode ? Rows(ctx, root, holder, menu) : Entries(ctx, root, holder));
        LOGI("options menu: %s, %s%s", host->page, p.mode ? kCaptions[p.mode - 1] : "buttons", p.failed ? ", failed" : "");
        if (!p.failed && !p.mode && g_last) Focus(ctx, p.entry[std::min(g_last, host->entries) - 1]);
        g_last = p.mode;
        return;
    }
    Follow(p);
    if (going == 1) p = {};
    if (leaving) return;
    bool taken = G<uint8_t>(ui + 0x390F) && G<uint8_t>(ui + 0x454) && !G<uint8_t>(ui + 0x455);
    uint32_t id = taken ? R32(ui + 0x44C) : 0, focus = Member(ctx, menu, "mFocusButton");
    if (!p.mode) {
        const uint32_t* at = std::find(std::begin(p.entry), std::end(p.entry), focus);
        if (id == 5 && at != std::end(p.entry)) g_next = int(at - p.entry) + 1;
        return;
    }
    size_t slot = size_t(std::find(p.name, p.name + p.slots, focus) - p.name), hidden = p.rows.size() - p.slots;
    if (p.capture) {
        int vk = input::PressedKey();
        if (p.capture == 2 && vk) {
            if (vk != kEscape) input::Bind(size_t(p.rows[p.bind]->id), vk);
            p.capture = 3;
        } else if (p.capture != 2 && !vk) {
            p.capture = p.capture == 1 ? 2 : 0;
        } else if (id == 6) {
            p.capture = 0;  // a controller's back
        }
        W32(ui + 0x44C, 0);
    } else if (slot < p.slots) {
        const Row* row = p.rows[p.top + slot];
        if (clear) Clear(ctx, row);
        // past either end the rows scroll, then go round
        bool edge = true, value = row && row->kind == app::rows::Kind::Value;
        bool locked = row && !app::rows::Enabled(*row);  // greyed in the panel
        if (id == 1 && slot == 0) {
            if (p.top) {
                p.top--;
            } else {
                p.top = hidden;
                Focus(ctx, p.name[p.slots - 1]);
            }
        } else if (id == 2 && slot == p.slots - 1) {
            if (p.top < hidden) {
                p.top++;
            } else {
                p.top = 0;
                Focus(ctx, p.name[0]);
            }
        } else {
            edge = false;
            if (row == &kPageRow && id >= 3 && id <= 5) {
                p.mode = 3 - p.mode;
                Fill(p);
            } else if (value && id >= 3 && id <= 5) {
                if (!locked) app::rows::Value(*row, id == 3 ? -1 : 1);
            } else if (id == 5 && row) {
                p.capture = 1;
                p.bind = p.top + slot;
            } else if (id == 5) {
                app::rows::Defaults(p.mode - 1);
            }
        }
        if (edge) W32(ui + 0x44C, 0);
        if (edge || (value && !locked && (id == 3 || id == 4))) Sound(ctx);
    }
    Write(g_fields[kTitle], app::Ui(kCaptions[p.mode - 1]));
    for (size_t i = 0; i < p.slots; i++) {
        const Row* row = p.rows[p.top + i];
        Write(g_fields[kName + i], app::Ui(row ? row->label : "RESTORE DEFAULTS"));
        Write(g_fields[kValue + i], ValueText(p, p.top + i));
        // a value lights up with its name: +0xC a clip's matrix, then its colours
        uint32_t from = flash::FirstChild(p.name[i]), to = flash::FirstChild(p.value[i]);
        if (from && to) std::memcpy(HostPtr(to + 0xC), HostPtr(from + 0xC), 0x38);
    }
    if (slot < p.slots) StretchBar(ctx, p, root, focus);
}

}  // namespace

// ebp the front end, after the cursor's input and before the Flash player runs the page's script
void OptionsMenu(X86Context& ctx, uint8_t*) {
    X86Context saved = ctx;
    Tick(ctx, ctx.ebp);
    ctx = saved;
}

namespace game {

// mouse.cpp: a key binding takes the next click
bool OptionsCapturing() { return g_page.capture != 0; }

// prompts.cpp's string lookup: a text of the page by its string id, 0 for none
uint32_t OptionsText(uint32_t id) {
    for (const Field& f : g_fields)
        if (f.text && f.id == id) return f.text;
    return 0;
}

}  // namespace game
