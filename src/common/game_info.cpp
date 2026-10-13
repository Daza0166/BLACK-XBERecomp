#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "audio/output.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"

namespace game {

namespace {

// the panel's keys and the mouse; F3 and the display mode stay with the host
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    using namespace app;
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(lp & (1 << 30)) && input::PanelKey(int(wp))) {
                ToggleMenu();
                *result = 0;
                return true;
            }
            if (msg == WM_KEYDOWN && wp == VK_ESCAPE && MenuOpen() && !Capturing()) ToggleMenu();
            break;
        case WM_SYSCOMMAND:
            // a bound Alt must not open the window menu
            if ((wp & 0xFFF0) == SC_KEYMENU) {
                *result = 0;
                return true;
            }
            break;
        case WM_INPUT:
            input::OnRawInput(reinterpret_cast<void*>(lp));
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) input::ReleaseCursor();
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(MenuOpen() ? LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))
                          : input::CursorShown() ? static_cast<HCURSOR>(CursorImage()) : nullptr);
                *result = TRUE;
                return true;
            }
            break;
    }
    return false;
}

// port 0 has the keyboard; the game sees no input while the panel is open
void Pad(int port, input::Pad& pad) {
    input::SeenPad(pad);
    if (port == 0) pad.connected = true;
    if (app::MenuOpen()) pad = input::Pad{.connected = pad.connected};
    else if (port == 0) input::Keyboard(pad);
}

// no vibration while the keyboard and mouse are in use
void Rumble(int port, uint16_t& left, uint16_t& right) {
    if (port != 0 || !input::KeyboardActive()) return;
    left = 0;
    right = 0;
}

// what the options list cannot hold
void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    Options& o = Opt();
    o.mouse_sensitivity = float(t["mouse_sensitivity"].value_or(double(o.mouse_sensitivity)));
    if (const toml::table* keys = t["keys"].as_table())
        for (auto&& [id, value] : *keys)
            if (auto v = value.value<std::string>()) o.keys[std::string(id.str())] = *v;
}

// after the settings load, before boot
void Init() {
    input::LoadBindings();
    audio::SetOutputMode(Opt().audio_output);
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    const Options& o = Opt();
    t.insert("mouse_sensitivity", double(o.mouse_sensitivity));
    toml::table keys;
    for (const auto& [id, value] : o.keys) keys.insert(id, value);
    t.insert("keys", keys);
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
    .options = {{"skip_intros", &game::Opt().skip_intros, nullptr}, {"fps_counter", &game::Opt().fps_counter, nullptr},
                {"reload_blur", &game::Opt().reload_blur, nullptr},
                {"hdr_output", &game::Opt().hdr_output, nullptr},
                {"invert_mouse", &game::Opt().invert_mouse, nullptr}, {"hold_aim", &game::Opt().hold_aim, nullptr},
                {"walk_speed", nullptr, &game::Opt().walk_speed}, {"fov", nullptr, &game::Opt().fov},
                {"msaa", nullptr, &game::Opt().msaa},
                {"draw_distance", nullptr, &game::Opt().draw_distance},
                {"ambient_occlusion", nullptr, &game::Opt().ambient_occlusion},
                {"viewmodel_fov", nullptr, &game::Opt().viewmodel_fov},
                {"audio_output", nullptr, &game::Opt().audio_output}},
    .load = game::Load,
    .save = game::Save,
    .init = game::Init,
    .loaded = nullptr,
    .window = input::StartMouse,
    .message = game::Message,
    .ui = app::DrawUi,
    .pad = game::Pad,
    .rumble = game::Rumble,
};
