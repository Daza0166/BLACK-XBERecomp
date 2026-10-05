// Recomp settings in the game's own options menu: a RECOMP SETTINGS button under the stock ones loads the page again
// with the F10 panel's rows in their place. A row is two copies of a stock button clip, its name and its value; the
// name's copy stands in for the script's button object, so the script moves the focus and the cursor works.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "common/menu.h"
#include "core/log.h"
#include "game/flash.h"
#include "xdk/hle.h"

using namespace kernel;
using flash::Find;
using flash::Member;
using flash::Members;

namespace {

constexpr uint32_t kInterpreter = 0x562C08, kPlayerReady = 0x2ED408, kHashTable = 0x2AC908;
constexpr uint32_t kSetMember = 0x100DB0;   // eax value or 0 to delete, edx name, ecx 0; (interpreter, object, 1, 0, 1)
constexpr uint32_t kNewString = 0xF5600;    // cdecl (text): a string value
constexpr uint32_t kCall = 0x101420;        // eax object path; cdecl (function, result, count, texts...)
constexpr uint32_t kCallValues = 0x101260;  // the same, its arguments values
constexpr uint32_t kRunActions = 0x10F180;  // (player): the frame actions waiting, a new clip's stop() among them
constexpr char kPage[] = "OptionsMenuXbox";
constexpr char kButton[] = "_level4.Main.TextAnim.ControllerSetup_Text";
const char* const kCaptions[] = {"RECOMP SETTINGS"};
constexpr int kEntries = int(std::size(kCaptions)), kSlots = 8, kDepth = 100;
// from the stock first button: rows 25 apart, names left of it and values right, a little smaller to fit
constexpr float kPitch = 25.0f, kNameX = -37.0f, kValueX = 173.0f, kScale = 0.9f;

// a text the page shows from here: its string id, the text, the data of the field showing it
struct Field {
    uint32_t id, text, data;
};
enum { kTitle, kEntry, kName = kEntry + kEntries, kValue = kName + kSlots, kFields = kValue + kSlots };
constexpr uint32_t kFieldSize = 64;
Field g_fields[kFields];

struct Page {
    int mode = -1;  // 0 the stock buttons and ours, from 1 the rows of kCaptions[mode - 1]
    bool failed = false;
    uint32_t first = 0, fade = 0;  // our first clip, the stock clip ours come up with
    uint32_t entry[kEntries] = {}, name[kSlots] = {}, value[kSlots] = {};
    size_t slots = 0, top = 0;
    std::vector<const app::rows::Row*> rows;
};
Page g_page;     // game thread
int g_next = 0;  // the mode of the page being loaded
int g_last = 0;  // the mode before it, whose button takes the focus back

// a name the player may keep: +0 its handle, +4 uses, length, room and hash, +12 the text
uint32_t Name(const char* text) {
    static std::map<std::string, uint32_t> names;
    uint32_t& at = names[text];
    if (!at) {
        uint32_t length = uint32_t(std::strlen(text));
        at = mem::AllocSystem(length + 13);
        W32(at, at + 4);
        G<uint16_t>(at + 4) = 0x4000;
        G<uint16_t>(at + 6) = G<uint16_t>(at + 8) = uint16_t(length);
        G<uint16_t>(at + 10) = 0;
        std::memcpy(HostPtr(at + 12), text, length + 1);
    }
    return at;
}

uint32_t Text(const char* text) { return Name(text) + 12; }

uint32_t String(X86Context& ctx, const char* text) { return CallGuest(ctx, kNewString, {Text(text)}); }

bool Set(X86Context& ctx, uint32_t object, const char* name, uint32_t value) {
    ctx.eax = value;
    ctx.edx = Name(name);
    ctx.ecx = 0;
    return CallGuest(ctx, kSetMember, {kInterpreter, object, 1, 0, 1}) & 0xFF;
}

// the id of a string by its name
uint32_t Hash(const char* text) {
    uint32_t h = ~0u;
    for (; *text; text++) h = uint32_t(int32_t(h) >> 8) ^ R32(kHashTable + ((h ^ uint8_t(*text)) & 0xFF) * 4);
    return h;
}

// a text field in a clip, the one showing that string or the first: data +0x1C its string's name
uint32_t FindField(uint32_t clip, const char* shows, int depth = 0) {
    int left = 256;
    for (uint32_t child = flash::FirstChild(clip); child && left; child = R32(child + 0x50), left--) {
        uint32_t data = flash::Type(child) == 0xF ? R32(child + 0x48) : 0;
        if (data && (!shows || std::strcmp(static_cast<const char*>(HostPtr(R32(data + 0x1C) + 8)), shows) == 0)) return child;
        if (uint32_t found = depth < 4 ? FindField(child, shows, depth + 1) : 0) return found;
    }
    return 0;
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
uint32_t Copy(X86Context& ctx, uint32_t parent, const char* name, int depth) {
    char number[12];
    std::snprintf(number, sizeof(number), "%d", depth);
    ctx.eax = Text(kButton);
    CallGuest(ctx, kCall, {Text("duplicateMovieClip"), 0, 2, Text(name), Text(number)});
    uint32_t clip = Member(ctx, parent, name);
    if (flash::Type(clip) != 0xD) return 0;
    uint32_t data = R32(clip + 0x48);
    W32(data + 0x1C, R32(data + 0x1C) & ~0x2000000u);
    return clip;
}

void Place(uint32_t clip, float x, float y) {
    G<float>(clip + 0x1C) = x;
    G<float>(clip + 0x20) = y;
}

// the methods of a script object's class
uint32_t Methods(X86Context& ctx, uint32_t object) {
    uint32_t members = Members(ctx, object);
    return members ? Members(ctx, R32(members + 8)) : 0;
}

// a clip the page's script takes for one of its button objects, such as like
bool Button(X86Context& ctx, uint32_t clip, uint32_t like) {
    uint32_t methods = Methods(ctx, like), frame = Member(ctx, like, "HighlightFrame");
    uint32_t on = Find(methods, "HighLight"), off = Find(methods, "UnHighLight");
    return frame && on && off && Set(ctx, clip, "movieClip", clip) && Set(ctx, clip, "HighlightFrame", frame) &&
           Set(ctx, clip, "HighLight", on) && Set(ctx, clip, "UnHighLight", off);
}

void Link(X86Context& ctx, uint32_t above, uint32_t below) {
    Set(ctx, above, "buttonDown", below);
    Set(ctx, below, "buttonUp", above);
}

void Focus(X86Context& ctx, uint32_t button) {
    ctx.eax = Text("_level4.MyMenu");
    CallGuest(ctx, kCallValues, {Text("SetFocusButton"), 0, 1, button});
}

void Sound(X86Context& ctx) {
    ctx.eax = Text("_level4.MyMenu");
    CallGuest(ctx, kCall, {Text("PlayChangeSound"), 0, 0});
}

// our buttons under the stock ones, in their ring
bool Entries(X86Context& ctx, uint32_t root, uint32_t anim) {
    Page& p = g_page;
    uint32_t top = Member(ctx, root, "controllerButton"), above = Member(ctx, root, "Playerprofile");
    p.fade = Member(ctx, anim, "Playerprofile_Text");
    if (!top || !above || !flash::IsClip(p.fade)) return false;
    for (int i = 0; i < kEntries; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_E%d", i);
        if (!(p.entry[i] = Copy(ctx, anim, name, kDepth + i))) return false;
        Place(p.entry[i], G<float>(p.fade + 0x1C), G<float>(p.fade + 0x20) + kPitch * float(i + 1));
        if (!Show(ctx, kEntry + i, p.entry[i], nullptr, kCaptions[i])) return false;
    }
    p.first = p.entry[0];
    CallGuest(ctx, kRunActions, {R32(flash::kPlayer)});
    for (uint32_t clip : p.entry) {
        if (!Button(ctx, clip, top) || !Set(ctx, clip, "actionSelectPage", String(ctx, kPage))) return false;
        Link(ctx, above, clip);
        above = clip;
    }
    Link(ctx, above, top);
    return true;
}

// the panel's rows in place of the stock buttons
bool Rows(X86Context& ctx, uint32_t root, uint32_t anim, uint32_t menu) {
    Page& p = g_page;
    uint32_t like = Member(ctx, root, "controllerButton"), nothing = Find(Methods(ctx, like), "Select");
    p.fade = Member(ctx, anim, "ControllerSetup_Text");
    if (!nothing || !flash::IsClip(p.fade)) return false;
    size_t count = 0;
    const app::rows::Row* list = app::rows::Page(p.mode - 1, &count);
    for (size_t i = 0; i < count; i++)
        if (list[i].kind != app::rows::Kind::Heading) p.rows.push_back(&list[i]);
    p.slots = std::min(p.rows.size(), size_t(kSlots));
    float x = G<float>(p.fade + 0x1C), y = G<float>(p.fade + 0x20);
    for (size_t i = 0; i < p.slots; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_N%d", int(i));
        p.name[i] = Copy(ctx, anim, name, kDepth + int(i));
        name[3] = 'V';
        p.value[i] = Copy(ctx, anim, name, kDepth + kSlots + int(i));
        if (!p.name[i] || !p.value[i]) return false;
        Place(p.name[i], x + kNameX, y + kPitch * float(i));
        Place(p.value[i], x + kValueX, y + kPitch * float(i));
        for (uint32_t clip : {p.name[i], p.value[i]}) G<float>(clip + 0xC) = G<float>(clip + 0x18) = kScale;
        if (!Show(ctx, kName + int(i), p.name[i]) || !Show(ctx, kValue + int(i), p.value[i])) return false;
    }
    p.first = p.name[0];
    CallGuest(ctx, kRunActions, {R32(flash::kPlayer)});
    for (size_t i = 0; i < p.slots; i++) {
        // a confirm on a button with no action is a back
        if (!Button(ctx, p.name[i], like) || !Set(ctx, p.name[i], "actionSelectFunction", nothing)) return false;
        if (i) Link(ctx, p.name[i - 1], p.name[i]);
    }
    for (const char* stock : {"ControllerSetup_Text", "SoundSetup_Text", "PictureSetup_Text", "Playerprofile_Text"})
        if (uint32_t clip = Member(ctx, anim, stock); flash::IsClip(clip)) W32(clip + 0x58, R32(clip + 0x58) & ~0x100000u);
    Show(ctx, kTitle, anim, "$FE_OPTIONS", kCaptions[p.mode - 1]);
    // back is the options page, not its save and leave
    Set(ctx, menu, "mfuncFunctionBack", 0);
    Set(ctx, menu, "mstrPageBack", String(ctx, kPage));
    Focus(ctx, p.name[0]);
    return true;
}

// front end: +0x44C the menu input of this tick, 1 to 6 up down left right confirm back, +0x454 input on, +0x455 held
// off, +0x381C 2 once the page is ready, +0x3840 its name, +0x3884 another page asked for, +0x388C page leaving
void Tick(X86Context& ctx, uint32_t ui) {
    Page& p = g_page;
    const char* name = static_cast<const char*>(HostPtr(ui + 0x3840));
    bool up = R32(kPlayerReady) == 1 && R32(flash::kPlayer) && R32(ui + 0x381C) == 2 && std::strcmp(name, kPage) == 0;
    if (!up || R32(ui + 0x3884) == 1 || R32(ui + 0x388C)) {
        if (*name && std::strcmp(name, kPage) != 0) g_next = g_last = 0;
        p.mode = -1;
        p.failed = false;
        return;
    }
    uint32_t root = flash::Level(4), anim = Member(ctx, Member(ctx, root, "Main"), "TextAnim");
    uint32_t menu = Member(ctx, root, "MyMenu");
    if (!anim || !menu || p.failed) return;
    if (p.mode < 0 || Member(ctx, anim, p.mode ? "RC_N0" : "RC_E0") != p.first) {
        p = {};
        for (Field& f : g_fields) f.data = 0;
        p.mode = g_next;
        g_next = 0;
        p.failed = !(p.mode ? Rows(ctx, root, anim, menu) : Entries(ctx, root, anim));
        LOGI("options menu: %s%s", p.mode ? kCaptions[p.mode - 1] : "buttons", p.failed ? " failed" : "");
        if (!p.failed && !p.mode && g_last) Focus(ctx, p.entry[g_last - 1]);
        g_last = p.mode;
        return;
    }
    // the page brings its buttons up by their colour
    auto fade = [&p](uint32_t clip) {
        if (clip) std::memcpy(HostPtr(clip + 0x24), HostPtr(p.fade + 0x24), 0x20);
    };
    std::for_each(std::begin(p.entry), std::end(p.entry), fade);
    std::for_each(std::begin(p.name), std::end(p.name), fade);
    std::for_each(std::begin(p.value), std::end(p.value), fade);
    bool taken = G<uint8_t>(ui + 0x390F) && G<uint8_t>(ui + 0x454) && !G<uint8_t>(ui + 0x455);
    uint32_t id = taken ? R32(ui + 0x44C) : 0, focus = Member(ctx, menu, "mFocusButton");
    if (!p.mode) {
        const uint32_t* at = std::find(std::begin(p.entry), std::end(p.entry), focus);
        if (id == 5 && at != std::end(p.entry)) g_next = int(at - p.entry) + 1;
        return;
    }
    size_t slot = size_t(std::find(p.name, p.name + p.slots, focus) - p.name), hidden = p.rows.size() - p.slots;
    if (slot < p.slots) {
        // past either end the rows scroll, then go round
        bool edge = true;
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
            if (id >= 3 && id <= 5) app::rows::Value(*p.rows[p.top + slot], id == 3 ? -1 : 1);
        }
        if (edge) W32(ui + 0x44C, 0);
        if (edge || id == 3 || id == 4) Sound(ctx);
    }
    for (size_t i = 0; i < p.slots; i++) {
        const app::rows::Row& row = *p.rows[p.top + i];
        Write(g_fields[kName + i], row.label);
        Write(g_fields[kValue + i], app::rows::Value(row));
        // a value lights up with its name: +0xC a clip's matrix, then its colours
        uint32_t from = flash::FirstChild(p.name[i]), to = flash::FirstChild(p.value[i]);
        if (from && to) std::memcpy(HostPtr(to + 0xC), HostPtr(from + 0xC), 0x38);
    }
}

}  // namespace

// ebp the front end, after the cursor's input and before the Flash player runs the page's script
void OptionsMenu(X86Context& ctx, uint8_t*) {
    X86Context saved = ctx;
    Tick(ctx, ctx.ebp);
    ctx = saved;
}

namespace game {

// prompts.cpp's string lookup: a text of the page by its string id, 0 for none
uint32_t OptionsText(uint32_t id) {
    for (const Field& f : g_fields)
        if (f.text && f.id == id) return f.text;
    return 0;
}

}  // namespace game
