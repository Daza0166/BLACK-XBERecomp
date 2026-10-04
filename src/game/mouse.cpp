// The mouse on the game side.
// Play: raw counts turn the view in the player's look update.
// Menus: Flash pages whose script keeps the focused button, each button's neighbours and takes one input a tick.
// The cursor steps the focus to the button under it, a click confirms, the wheel steps up and down. In a row of
// options a click picks the value or arrow under it, the left button held on a slider drags it.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "game/flash.h"
#include "xdk/hle.h"

using namespace kernel;
using flash::IsClip;
using flash::Type;

// degrees a count at sensitivity 1
constexpr float kDegrees = 0.05f;

// esi the player's controller: +0x18 yaw (positive left), +0x1C pitch (positive down), degrees; xmm2 the zoom scale.
// The game adds the stick, clamps the pitch and wraps the yaw after this.
void MouseLook(X86Context& ctx, uint8_t*) {
    float dx, dy;
    if (!input::TakeMouse(dx, dy)) return;
    float scale = kDegrees * ctx.xmm[2].f[0];
    G<float>(ctx.esi + 0x18) -= std::remainder(dx * scale, 360.0f);
    G<float>(ctx.esi + 0x1C) += dy * scale;
}

namespace {

// front end: +0x44C the menu input of this tick, +0x450 set while it waits for a player's Start, +0x454 input on,
// +0x455 input held off, +0x388C page leaving, +0x390F ready, +0x3710 stage to screen scale, +0x3840 page name
// display: +0x0C widescreen, +0xD428 width, +0xD42C height
constexpr uint32_t kDisplay = 0x2D67F4;
constexpr uint32_t kBounds = 0xE9310;  // eax = float box[4] out, ecx = clip: its box in its parent
constexpr uint32_t kDeletedName = 0x2AC7EC;
constexpr float kWideOffset = 106.6667f;  // the 640 stage sits this far in on a widescreen
constexpr float kPad = 6.0f;  // around a value's or an arrow's clip, some are a few pixels across
constexpr size_t kMaxNodes = 64;
enum Input : uint32_t { None, Up, Down, Left, Right, Confirm };
constexpr const char* kButtonLinks[] = {"buttonUp", "buttonDown", "buttonLeft", "buttonRight"};
constexpr const char* kValueLinks[] = {"upPoint", "downPoint", "leftPoint", "rightPoint"};


// members: +0 slots, +4 {name, value} each, a name's text at +8
uint32_t Members(X86Context& ctx, uint32_t value) {
    if (!Type(value)) return 0;
    ctx.ecx = value;
    return CallGuest(ctx, R32(R32(value) + 0xC), {});
}

uint32_t Find(uint32_t members, const char* name) {
    uint32_t slots = members ? R32(members) : 0, table = members ? R32(members + 4) : 0;
    for (uint32_t i = 0; table && slots <= 0x1000 && i < slots; i++) {
        uint32_t key = R32(table + i * 8), value = R32(table + i * 8 + 4);
        if (key && key != kDeletedName && _stricmp(static_cast<const char*>(HostPtr(key + 8)), name) == 0)
            return Type(value) ? value : 0;
    }
    return 0;
}

uint32_t Member(X86Context& ctx, uint32_t value, const char* name) { return Find(Members(ctx, value), name); }

float Number(uint32_t value) {
    return Type(value) == 7 ? float(int32_t(R32(value + 8))) : Type(value) == 6 ? G<float>(value + 8) : 0.0f;
}

// the page while it takes input
uint32_t Page(uint32_t ui) {
    bool input = G<uint8_t>(ui + 0x390F) && !R32(ui + 0x388C) && G<uint8_t>(ui + 0x454) && !G<uint8_t>(ui + 0x455);
    return input ? flash::Level(4) : 0;
}

// a clip's box on the stage; false when hidden or empty
bool Box(X86Context& ctx, uint32_t clip, float box[4]) {
    static const uint32_t out = mem::AllocSystem(16);
    ctx.eax = out;
    ctx.ecx = clip;
    CallGuest(ctx, kBounds, {});
    float r[4];
    std::memcpy(r, HostPtr(out), sizeof(r));
    if (r[2] < r[0] || r[3] < r[1]) return false;
    float x[4] = {r[0], r[2], r[0], r[2]}, y[4] = {r[1], r[1], r[3], r[3]};
    bool shown = R32(clip + 0x58) & 0x100000;
    int depth = 0;
    for (uint32_t parent = R32(clip + 0x44); parent && depth < 32; parent = R32(parent + 0x44), depth++) {
        float m[6];
        std::memcpy(m, HostPtr(parent + 0xC), sizeof(m));
        for (int i = 0; i < 4; i++) {
            float px = x[i];
            x[i] = m[0] * px + m[2] * y[i] + m[4];
            y[i] = m[1] * px + m[3] * y[i] + m[5];
        }
        shown = shown && (R32(parent + 0x58) & 0x100000);
    }
    box[0] = *std::min_element(x, x + 4);
    box[1] = *std::min_element(y, y + 4);
    box[2] = *std::max_element(x, x + 4);
    box[3] = *std::max_element(y, y + 4);
    return shown;
}

bool Over(X86Context& ctx, uint32_t clip, float x, float y, float pad = 0.0f, float* across = nullptr) {
    float box[4];
    if (!IsClip(clip) || !Box(ctx, clip, box)) return false;
    if (across) *across = (x - box[0]) / std::max(box[2] - box[0], 1.0f);
    return x >= box[0] - pad && x <= box[2] + pad && y >= box[1] - pad && y <= box[3] + pad;
}

// a button or value that can be reached from the first and the first input on the way there
struct Node {
    uint32_t object, members;
    uint32_t step;
};

size_t Reach(X86Context& ctx, uint32_t start, const char* const* links, Node* list) {
    size_t count = 0;
    list[count++] = {start, Members(ctx, start), None};
    for (size_t i = 0; i < count; i++)
        for (uint32_t d = 0; d < 4; d++) {
            uint32_t next = Find(list[i].members, links[d]);
            if (!next || count == kMaxNodes ||
                std::any_of(list, list + count, [next](const Node& n) { return n.object == next; }))
                continue;
            list[count++] = {next, Members(ctx, next), i ? list[i].step : Up + d};
        }
    return count;
}

// the value of a row of values (on, off; the map's levels) under the cursor
uint32_t ValueAt(X86Context& ctx, uint32_t values, float x, float y) {
    uint32_t active = Member(ctx, values, "mActive");
    if (!active) return 0;
    Node nodes[kMaxNodes];
    size_t count = Reach(ctx, active, kValueLinks, nodes);
    for (size_t i = 0; i < count; i++)
        if (Over(ctx, Find(nodes[i].members, "movieClip"), x, y, kPad)) return nodes[i].object;
    return 0;
}

// what the cursor is on: the button to focus, then the value to pick in its row or the input to give it
struct Aim {
    uint32_t button = 0, values = 0, value = 0, then = None;
};

Aim Point(X86Context& ctx, const Node* buttons, size_t count, uint32_t map, float x, float y, bool click) {
    bool clips = false;
    for (size_t i = 0; i < count; i++) {
        uint32_t button = buttons[i].object, clip = Find(buttons[i].members, "movieClip");
        uint32_t options = Find(buttons[i].members, "mChoiceList"), members = Members(ctx, options);
        clips |= IsClip(clip);
        if (uint32_t value = ValueAt(ctx, options, x, y)) return {button, options, click ? value : 0};
        if (Over(ctx, Find(members, "mLeftAnimation"), x, y, kPad)) return {button, 0, 0, click ? Left : None};
        if (Over(ctx, Find(members, "mRightAnimation"), x, y, kPad)) return {button, 0, 0, click ? Right : None};
        if (Over(ctx, Find(members, "mMovieClip"), x, y)) return {button};
        // a row of options has no action of its own, confirm would back out of the page
        if (Over(ctx, clip, x, y)) return {button, 0, 0, click && !options ? Confirm : None};
    }
    if (uint32_t value = click ? ValueAt(ctx, map, x, y) : 0) return {buttons[0].object, map, value};
    // a page of one button with nothing to point at takes a click anywhere
    if (click && !clips) return {buttons[0].object, 0, 0, Confirm};
    return {};
}

// the left button held on the focused row's slider: a step toward the cursor
uint32_t Slide(X86Context& ctx, uint32_t row, float x, float y) {
    uint32_t slider = Members(ctx, Find(row, "mChoiceList")), value = Find(slider, "mValue");
    float across, step = Number(Find(slider, "mChange"));
    if (!value || !(step > 0) || !Over(ctx, Find(slider, "mMovieClip"), x, y, 0.0f, &across)) return None;
    float off = across * 100.0f - Number(value);
    return off >= step / 2 ? Right : off <= -step / 2 ? Left : None;
}

void Menu(X86Context& ctx, uint32_t ui) {
    static const char* const kFilms[] = {"FMVPlayer", "BootSequence", "IntroCredits", "BlackIntroSequence"};
    static Aim aim;
    static uint32_t page;
    static bool held;
    static float seen_x, seen_y;
    const char* name = static_cast<const char*>(HostPtr(ui + 0x3840));
    bool menu = *name && !R32(ui + 0x450) &&
                std::none_of(std::begin(kFilms), std::end(kFilms),
                             [name](const char* film) { return _strnicmp(name, film, std::strlen(film)) == 0; });
    input::SetMenu(menu);
    int wheel = input::TakeWheel();
    float x, y;
    bool down = false;
    bool pointer = menu && input::Cursor(x, y, down);
    bool click = down && !held;
    held = down;
    uint32_t root = pointer ? Page(ui) : 0, list = Member(ctx, root, "MyMenu"), focus = Member(ctx, list, "mFocusButton");
    uint32_t display = R32(kDisplay);
    float sx = G<float>(ui + 0x3710), sy = G<float>(ui + 0x3714);
    if (!focus || !display || !(sx > 0) || !(sy > 0)) {
        aim = {};
        return;
    }
    x = x * float(int32_t(R32(display + 0xD428))) / sx - (G<uint8_t>(display + 0xC) ? kWideOffset : 0.0f);
    y = y * float(int32_t(R32(display + 0xD42C))) / sy;
    bool moved = x != seen_x || y != seen_y;
    seen_x = x;
    seen_y = y;
    // the pad or the keys act this tick
    if (R32(ui + 0x44C)) {
        aim = {};
        return;
    }
    uint32_t input = None;
    if (moved || down || aim.button) {
        Node buttons[kMaxNodes];
        size_t count = Reach(ctx, focus, kButtonLinks, buttons);
        if (list != page) LOGI("menu cursor: page %s, %zu buttons", name, count);
        page = list;
        // a click's aim holds while the cursor stays on its button
        if (moved || click) {
            Aim over = Point(ctx, buttons, count, Member(ctx, root, "MyMap"), x, y, click);
            if (click || over.button != aim.button) aim = over;
        }
        const Node* at = std::find_if(buttons, buttons + count, [](const Node& n) { return n.object == aim.button; });
        if (!aim.button || at == buttons + count) aim = {};
        else if (aim.button != focus) input = at->step;
        else if (aim.value) {
            Node values[kMaxNodes];
            uint32_t active = Member(ctx, aim.values, "mActive");
            size_t n = active ? Reach(ctx, active, kValueLinks, values) : 0;
            const Node* value = std::find_if(values, values + n, [](const Node& v) { return v.object == aim.value; });
            if (value != values + n) input = value->step;
            if (!input) aim = {};
        } else {
            input = aim.then;
            aim = {};
        }
        if (!input && down) input = Slide(ctx, buttons[0].members, x, y);
    }
    if (!input && wheel) input = wheel > 0 ? Up : Down;
    if (input) W32(ui + 0x44C, input);
}

}  // namespace

// ebp the front end, after it took the pad's menu input and before the Flash player runs the page's script
void MenuCursor(X86Context& ctx, uint8_t*) {
    X86Context saved = ctx;
    Menu(ctx, ctx.ebp);
    ctx = saved;
}
