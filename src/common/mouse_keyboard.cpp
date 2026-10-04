// the keyboard and mouse as the controller on port 0
#include "common/mouse_keyboard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iterator>
#include <mutex>
#include <vector>

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

namespace input {

namespace {

HWND g_hwnd = nullptr;
std::atomic<int> g_dx{0}, g_dy{0};
std::atomic<uint64_t> g_take_ms{0};     // TakeMouse, asked every tick in play
std::atomic<uint64_t> g_capture_ms{0};  // PressedKey
std::atomic<bool> g_settle{false};
std::atomic<uint16_t> g_tap{0};
std::atomic<uint64_t> g_tap_until{0};
constexpr uint64_t kPlayMs = 250, kTapMs = 80;

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()) + 1;
}

// what an action presses: 0-7 analog A B X Y black white LT RT, 8-15 the buttons bits, 16-19 the move keys
struct Target {
    Action action;
    int button;
};
constexpr int kHostKey = -2;
// the game's default controller layout
constexpr Target kActions[] = {
    {{"move_forward", "MOVE FORWARD", "W"}, 16},
    {{"move_back", "MOVE BACK", "S"}, 17},
    {{"strafe_left", "MOVE LEFT", "A"}, 18},
    {{"strafe_right", "MOVE RIGHT", "D"}, 19},
    {{"fire", "FIRE", "LMB"}, 7},
    {{"aim", "AIM", "RMB"}, 15},
    {{"reload", "RELOAD", "R"}, 0},
    {{"action", "ACTION", "E"}, 2},
    {{"melee", "MELEE", "F"}, 1},
    {{"grenade", "GRENADE", "G"}, 6},
    {{"crouch", "CROUCH", "C,Ctrl"}, 14},
    {{"suppressor", "TOGGLE SUPPRESSOR", "T"}, 3},
    {{"firing_mode", "CHANGE FIRING MODE", "X"}, 8},
    {{"health_pack", "HEALTH PACK", "H"}, 9},
    {{"weapon_up", "CHANGE WEAPON UP", "Q"}, 11},
    {{"weapon_down", "CHANGE WEAPON DOWN", "Z"}, 10},
    {{"next_track", "NEXT MUSIC TRACK", "N"}, 5},
    {{"pause", "PAUSE", "Escape"}, 12},
    {{"back", "BACK BUTTON", "Tab"}, 13},
    {{"settings_panel", "SETTINGS PANEL", "F10"}, kHostKey},
};
constexpr size_t kCount = std::size(kActions);
constexpr size_t kPanelAction = kCount - 1;
// the wheel changes weapon
constexpr uint16_t kWheelUp = 1 << 3, kWheelDown = 1 << 2;

struct Name {
    int vk;
    const char* name;
};
constexpr Name kNames[] = {
    {VK_LBUTTON, "LMB"}, {VK_RBUTTON, "RMB"}, {VK_MBUTTON, "MMB"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
    {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_ESCAPE, "Escape"}, {VK_BACK, "Backspace"}, {VK_TAB, "Tab"},
    {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_MENU, "Alt"}, {VK_CAPITAL, "CapsLock"},
    {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"},
    {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
    {'A', "A"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "E"}, {'F', "F"}, {'G', "G"}, {'H', "H"}, {'I', "I"},
    {'J', "J"}, {'K', "K"}, {'L', "L"}, {'M', "M"}, {'N', "N"}, {'O', "O"}, {'P', "P"}, {'Q', "Q"}, {'R', "R"},
    {'S', "S"}, {'T', "T"}, {'U', "U"}, {'V', "V"}, {'W', "W"}, {'X', "X"}, {'Y', "Y"}, {'Z', "Z"},
    {'0', "0"}, {'1', "1"}, {'2', "2"}, {'3', "3"}, {'4', "4"}, {'5', "5"}, {'6', "6"}, {'7', "7"}, {'8', "8"}, {'9', "9"},
    {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"},
    {VK_F8, "F8"}, {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
    {VK_NUMPAD0, "Numpad0"}, {VK_NUMPAD1, "Numpad1"}, {VK_NUMPAD2, "Numpad2"}, {VK_NUMPAD3, "Numpad3"},
    {VK_NUMPAD4, "Numpad4"}, {VK_NUMPAD5, "Numpad5"}, {VK_NUMPAD6, "Numpad6"}, {VK_NUMPAD7, "Numpad7"},
    {VK_NUMPAD8, "Numpad8"}, {VK_NUMPAD9, "Numpad9"}, {VK_ADD, "NumpadPlus"}, {VK_SUBTRACT, "NumpadMinus"},
    {VK_MULTIPLY, "NumpadStar"}, {VK_DIVIDE, "NumpadSlash"}, {VK_DECIMAL, "NumpadPeriod"},
    {VK_OEM_3, "Backtick"}, {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_4, "LBracket"},
    {VK_OEM_6, "RBracket"}, {VK_OEM_5, "Backslash"}, {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "Quote"},
    {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"},
};

// not bindable: the arrows move at full tilt, Enter accepts, Backspace backs out
constexpr int kFixed[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK};

std::mutex g_bind_lock;
std::array<std::vector<int>, kCount> g_binds;

int Vk(const std::string& name) {
    for (const Name& n : kNames)
        if (_stricmp(n.name, name.c_str()) == 0) return n.vk;
    return 0;
}

const char* NameOf(int vk) {
    for (const Name& n : kNames)
        if (n.vk == vk) return n.name;
    return nullptr;
}

std::vector<int> Parse(const std::string& list) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (int vk = Vk(list.substr(start, comma - start))) out.push_back(vk);
        start = comma + 1;
    }
    return out;
}

std::string Join(const std::vector<int>& vks) {
    std::string s;
    for (int vk : vks) {
        if (!s.empty()) s += ',';
        s += NameOf(vk);
    }
    return s;
}

bool Active() { return g_hwnd && GetForegroundWindow() == g_hwnd && !app::MenuOpen(); }
bool Down(int vk) { return GetAsyncKeyState(vk) < 0; }
bool Mouse(int vk) { return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2; }
bool Play() { return NowMs() - g_take_ms < kPlayMs; }

void Clip() {
    RECT r, cur;
    GetClientRect(g_hwnd, &r);
    MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    if (!GetClipCursor(&cur) || !EqualRect(&r, &cur)) ClipCursor(&r);
}

}  // namespace

void StartMouse(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE mouse{1, 2, 0, g_hwnd};
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
}

void OnRawInput(void* handle) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(static_cast<HRAWINPUT>(handle), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) ||
        ri.header.dwType != RIM_TYPEMOUSE || !Active())
        return;
    // the cursor is free in the game's menus
    if (!Play()) {
        ClipCursor(nullptr);
        return;
    }
    Clip();
    const RAWMOUSE& m = ri.data.mouse;
    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        g_dx += m.lLastX;
        g_dy += m.lLastY;
    }
    if (m.usButtonFlags & RI_MOUSE_WHEEL) {
        g_tap = SHORT(m.usButtonData) > 0 ? kWheelUp : kWheelDown;
        g_tap_until = NowMs() + kTapMs;
    }
}

void ReleaseCursor() {
    ClipCursor(nullptr);
    g_settle = true;
}

bool TakeMouse(float& dx, float& dy) {
    uint64_t now = NowMs();
    bool stale = now - g_take_ms.exchange(now) >= kPlayMs;
    int x = g_dx.exchange(0), y = g_dy.exchange(0);
    if ((!x && !y) || stale || !Active()) return false;
    const game::Options& o = game::Opt();
    dx = float(x) * o.mouse_sensitivity;
    dy = float(o.invert_mouse ? -y : y) * o.mouse_sensitivity;
    return true;
}

void Keyboard(Pad& p) {
    if (!Active()) {
        g_settle = true;
        return;
    }
    std::array<std::vector<int>, kCount> binds;
    {
        std::lock_guard lock(g_bind_lock);
        binds = g_binds;
    }
    if (g_settle) {
        for (auto& b : binds)
            for (int vk : b)
                if (Down(vk)) return;
        for (int vk : kFixed)
            if (Down(vk)) return;
        g_settle = false;
    }
    int move[4] = {};
    for (size_t i = 0; i < kCount; i++) {
        int b = kActions[i].button;
        if (b == kHostKey || !std::any_of(binds[i].begin(), binds[i].end(), Down)) continue;
        if (b >= 16) move[b - 16] = 1;
        else if (b < 8) p.analog[b] = 0xFF;
        else p.buttons |= uint16_t(1u << (b - 8));
    }
    if (Down(VK_RETURN)) p.analog[0] = 0xFF;
    if (Down(VK_BACK)) p.analog[1] = 0xFF;
    int x = move[3] - move[2], y = move[0] - move[1];
    if (int ax = Down(VK_RIGHT) - Down(VK_LEFT), ay = Down(VK_UP) - Down(VK_DOWN); ax || ay) {
        x = ax;
        y = ay;
    }
    if (x) p.lx = int16_t(x * 32767);
    if (y) p.ly = int16_t(y * 32767);
    if (NowMs() < g_tap_until) p.buttons |= g_tap;
}

const Action* Actions(size_t* count) {
    static const auto list = [] {
        std::array<Action, kCount> a{};
        for (size_t i = 0; i < kCount; i++) a[i] = kActions[i].action;
        return a;
    }();
    *count = kCount;
    return list.data();
}

std::string Binding(size_t action) {
    std::lock_guard lock(g_bind_lock);
    return action < kCount ? Join(g_binds[action]) : std::string();
}

void Bind(size_t action, int vk) {
    // the settings panel acts on the window's key messages, which mouse buttons do not send
    if (action >= kCount || !NameOf(vk) || (kActions[action].button == kHostKey && Mouse(vk))) return;
    {
        std::lock_guard lock(g_bind_lock);
        // a new key joins the action's keys (two at most, the older goes); one it has already comes off, unless it is
        // the last
        std::vector<int>& keys = g_binds[action];
        if (auto it = std::find(keys.begin(), keys.end(), vk); it != keys.end()) {
            if (keys.size() > 1) keys.erase(it);
        } else {
            keys.push_back(vk);
            if (keys.size() > 2) keys.erase(keys.begin());
        }
        game::Opt().keys[kActions[action].action.id] = Join(keys);
    }
    app::Save();
}

void ResetBindings() {
    game::Opt().keys.clear();
    LoadBindings();
    app::Save();
}

void LoadBindings() {
    std::lock_guard lock(g_bind_lock);
    const auto& keys = game::Opt().keys;
    for (size_t i = 0; i < kCount; i++) {
        auto it = keys.find(kActions[i].action.id);
        g_binds[i] = Parse(it != keys.end() ? it->second : kActions[i].action.defaults);
    }
}

int PressedKey() {
    g_capture_ms = NowMs();
    for (const Name& n : kNames)
        if (Down(n.vk)) return n.vk;
    return 0;
}

bool PanelKey(int vk) {
    if (NowMs() - g_capture_ms <= 250) return false;
    std::lock_guard lock(g_bind_lock);
    const auto& b = g_binds[kPanelAction];
    return std::find(b.begin(), b.end(), vk) != b.end();
}

}  // namespace input
