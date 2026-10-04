#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

// the game sees no input while the panel is open
void Pad(int, input::Pad& pad) {
    if (app::MenuOpen()) pad = input::Pad{.connected = pad.connected};
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
