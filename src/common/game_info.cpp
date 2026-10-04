#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

#include "app/app.h"
#include "common/menu.h"
#include "common/options.h"
#include "core/input.h"

namespace game {

namespace {

// the panel's keys; F3 and the display mode stay with the host
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    using namespace app;
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_F10) ToggleMenu();
            if (wp == VK_ESCAPE && MenuOpen()) ToggleMenu();
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(MenuOpen() ? LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)) : nullptr);
                *result = TRUE;
                return true;
            }
            break;
        case WM_SYSKEYDOWN:
            if (wp == VK_F10) {
                ToggleMenu();
                *result = 0;
                return true;
            }
            break;
    }
    return false;
}

// keys as the default controller layout: 0-7 analog, 8-15 buttons bit, 16-19 left stick up down right left
struct Key {
    int vk, to;
};
constexpr Key kKeys[] = {
    {'W', 16}, {'S', 17}, {'D', 18}, {'A', 19},
    {VK_UP, 8}, {VK_DOWN, 9}, {VK_LEFT, 10}, {VK_RIGHT, 11},
    {'X', 8},                    // firing mode
    {'H', 9},                    // health pack
    {'Q', 11},                   // change weapon
    {VK_LBUTTON, 7},             // fire
    {VK_RBUTTON, 15},            // aim
    {'G', 6},                    // grenade
    {'C', 14},                   // crouch
    {'R', 0}, {VK_RETURN, 0},    // reload, accept
    {'F', 1}, {VK_BACK, 1},      // melee, back
    {'E', 2},                    // action
    {'T', 3},                    // silencer
    {'B', 4}, {'N', 5},          // black, next music track
    {VK_ESCAPE, 12}, {VK_TAB, 13},
};

// port 0 has the keyboard; the game sees no input while the panel is open
void Pad(int port, input::Pad& pad) {
    static std::atomic<bool> closing{false};  // the Escape that closed the panel is no pause
    if (port == 0) pad.connected = true;
    if (app::MenuOpen()) {
        pad = input::Pad{.connected = pad.connected};
        closing = true;
        return;
    }
    if (port != 0 || GetForegroundWindow() != app::Window()) return;
    if (closing && (closing = GetAsyncKeyState(VK_ESCAPE) < 0)) return;
    for (const Key& k : kKeys) {
        if (GetAsyncKeyState(k.vk) >= 0) continue;
        if (k.to < 8) pad.analog[k.to] = 0xFF;
        else if (k.to < 16) pad.buttons |= uint16_t(1 << (k.to - 8));
        else (k.to < 18 ? pad.ly : pad.lx) = int16_t(k.to & 1 ? -32768 : 32767);
    }
}

}  // namespace

Options& Opt() {
    static Options o;
    return o;
}

}  // namespace game

const app::GameInfo app::kGame = {
    .title = L"Black - XBE Recomp",
    .short_title = L"Black",
    .name = L"Black",
    .log = "black",
    .settings = "black.toml",
    .splash = L"BlackLogo",
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with GlobData.bin and the "
               L"chars, data, export, language, levels, sound and videos folders beside it.",
    .images = {{"default.xbe", "Black"}},
    .options = {{"skip_intros", &game::Opt().skip_intros, nullptr}, {"fps_counter", &game::Opt().fps_counter, nullptr}},
    .load = nullptr,
    .save = nullptr,
    .init = nullptr,
    .loaded = nullptr,
    .window = nullptr,
    .message = game::Message,
    .ui = app::DrawUi,
    .pad = game::Pad,
    .rumble = nullptr,
};
