// The mouse on the game side.
// Play: raw counts turn the view in the player's look update.
// Menus: Flash pages whose script keeps the focused button, each button's neighbours and takes one input a tick.
// The cursor steps the focus to the button under it, a click confirms, the wheel steps up and down.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "xdk/hle.h"

using namespace kernel;

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
// player: +0x18 its movies; display: +0x0C widescreen, +0xD428 width, +0xD42C height
constexpr uint32_t kPlayer = 0x2EC988, kDisplay = 0x2D67F4;
constexpr uint32_t kBounds = 0xE9310;  // eax = float box[4] out, ecx = clip: its box in its parent
constexpr uint32_t kDeletedName = 0x2AC7EC;
constexpr float kWideOffset = 106.6667f;  // the 640 stage sits this far in on a widescreen
constexpr size_t kMaxButtons = 64;
enum Input : uint32_t { None, Up, Down, Left, Right, Confirm };

// script value: +0 vtable (+0xC its members), +4 flags: 0x10 defined, type from bit 25
uint32_t Type(uint32_t value) { return value && (R32(value + 4) & 0x10) ? R32(value + 4) >> 25 : 0; }
// clip: +0x0C matrix a b c d x y, +0x44 parent, +0x50 next, +0x54 depth in 17 bits, +0x58 bit 0x100000 visible
bool IsClip(uint32_t value) { return Type(value) >= 0xC && Type(value) <= 0x12; }

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

// the page's button list while it takes input: the script's MyMenu on the movie at level 4
uint32_t MenuList(X86Context& ctx, uint32_t ui) {
    if (!G<uint8_t>(ui + 0x390F) || R32(ui + 0x388C) || !G<uint8_t>(ui + 0x454) || G<uint8_t>(ui + 0x455)) return 0;
    uint32_t player = R32(kPlayer), movies = player ? R32(player + 0x18) : 0;
    for (uint32_t clip = movies ? R32(movies) : 0; clip; clip = R32(clip + 0x50))
        if (int32_t(R32(clip + 0x54) << 15) >> 15 == 4) return Type(clip) == 0x12 ? Member(ctx, clip, "MyMenu") : 0;
    return 0;
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

// a button the focus can reach and the first input on the way there
struct Button {
    uint32_t object, members;
    uint32_t step;
};

size_t Reach(X86Context& ctx, uint32_t focus, Button* list) {
    static const char* const kLinks[] = {"buttonUp", "buttonDown", "buttonLeft", "buttonRight"};
    size_t count = 0;
    list[count++] = {focus, Members(ctx, focus), None};
    for (size_t i = 0; i < count; i++)
        for (uint32_t d = 0; d < 4; d++) {
            uint32_t next = Find(list[i].members, kLinks[d]);
            if (!next || count == kMaxButtons ||
                std::any_of(list, list + count, [next](const Button& b) { return b.object == next; }))
                continue;
            list[count++] = {next, Members(ctx, next), i ? list[i].step : Up + d};
        }
    return count;
}

void Menu(X86Context& ctx, uint32_t ui) {
    static const char* const kFilms[] = {"FMVPlayer", "BootSequence", "IntroCredits", "BlackIntroSequence"};
    static uint32_t target, page;
    static bool confirm, held;
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
    uint32_t list = pointer ? MenuList(ctx, ui) : 0, focus = list ? Member(ctx, list, "mFocusButton") : 0;
    uint32_t display = R32(kDisplay);
    float sx = G<float>(ui + 0x3710), sy = G<float>(ui + 0x3714);
    if (!focus || !display || !(sx > 0) || !(sy > 0)) {
        target = 0;
        return;
    }
    x = x * float(int32_t(R32(display + 0xD428))) / sx - (G<uint8_t>(display + 0xC) ? kWideOffset : 0.0f);
    y = y * float(int32_t(R32(display + 0xD42C))) / sy;
    bool moved = x != seen_x || y != seen_y;
    seen_x = x;
    seen_y = y;
    // the pad or the keys act this tick
    if (R32(ui + 0x44C)) {
        target = 0;
        return;
    }
    uint32_t input = None;
    if (moved || click || target) {
        Button buttons[kMaxButtons];
        size_t count = Reach(ctx, focus, buttons);
        if (list != page) LOGI("menu cursor: page %s, %zu buttons", name, count);
        page = list;
        if (moved || click) {
            // a page of one button with nothing to point at takes a click anywhere
            bool clips = false;
            target = 0;
            confirm = click;
            for (size_t i = 0; i < count && !target; i++) {
                uint32_t clip = Find(buttons[i].members, "movieClip");
                float box[4];
                if (!IsClip(clip)) continue;
                clips = true;
                if (Box(ctx, clip, box) && x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3])
                    target = buttons[i].object;
            }
            if (click && !clips) input = Confirm;
        }
        const Button* at = std::find_if(buttons, buttons + count, [](const Button& b) { return b.object == target; });
        if (!target || at == buttons + count) target = 0;
        else if (target != focus) input = at->step;
        else {
            // a row of options has no action of its own, confirm would back out of the page
            if (confirm && !Find(buttons[0].members, "mChoiceList")) input = Confirm;
            target = 0;
        }
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
