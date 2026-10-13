// the keyboard and mouse as the controller on port 0
#include "common/mouse_keyboard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
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
#include "common/aspect.h"
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
std::atomic<uint64_t> g_tap_until{0}, g_confirm_until{0};
std::atomic<bool> g_menu{false}, g_keyboard{true};
std::atomic<int> g_wheel{0};
constexpr uint64_t kPlayMs = 250, kTapMs = 80;

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()) + 1;
}

// what an action presses: 0-7 analog A B X Y black white LT RT, 8-15 the buttons bits, 16-19 the move keys,
// 20-23 the look keys
struct Target {
    Action action;
    int button;
};
constexpr int kHostKey = -2, kWalk = -3, kStart = 12;
// the game's default controller layout
constexpr Target kActions[] = {
    {{"move_forward", "MOVE FORWARD", "W"}, 16},
    {{"move_back", "MOVE BACK", "S"}, 17},
    {{"strafe_left", "STRAFE LEFT", "A"}, 18},
    {{"strafe_right", "STRAFE RIGHT", "D"}, 19},
    {{"walk", "WALK", "Alt"}, kWalk},
    {{"crouch", "CROUCH", "C"}, 14},
    {{"fire", "FIRE", "LMB"}, 7},
    {{"aim", "AIM", "RMB"}, 15},
    {{"melee", "MELEE", "V"}, 1},
    {{"reload", "RELOAD", "R"}, 0},
    {{"interact", "INTERACT", "F"}, 2},
    {{"grenade", "THROW GRENADE", "G"}, 6},
    {{"weapon_up", "CHANGE WEAPON UP", "E"}, 11},
    {{"weapon_down", "CHANGE WEAPON DOWN", "Q"}, 10},
    {{"suppressor", "TOGGLE SUPPRESSOR", "T"}, 3},
    {{"firing_mode", "FIRING MODE / SCOPE ZOOM", "X"}, 8},
    {{"health_pack", "HEALTH PACK", "H"}, 9},
    {{"next_track", "NEXT MUSIC TRACK", "N"}, 5},
    {{"back", "OBJECTIVES", "Tab"}, 13},
    {{"pause", "PAUSE MENU", "Escape"}, 12},
    {{"settings_panel", "SETTINGS PANEL", "F10"}, kHostKey},
    {{"look_up", "LOOK UP", "Up"}, 20},
    {{"look_down", "LOOK DOWN", "Down"}, 21},
    {{"look_left", "LOOK LEFT", "Left"}, 22},
    {{"look_right", "LOOK RIGHT", "Right"}, 23},
};
constexpr size_t kCount = std::size(kActions);
constexpr size_t kPanelAction = [] {
    size_t i = 0;
    while (kActions[i].button != kHostKey) i++;
    return i;
}();
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

// not bindable: in the game's menus the arrows are the dpad, Enter accepts, Backspace backs out
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
// Delete clears a binding on the options pages: bound to a move key it would move the selection off the row first
bool GameDown(int vk) { return Down(vk) && !(g_menu && vk == VK_DELETE); }

void Clip() {
    RECT r, cur;
    GetClientRect(g_hwnd, &r);
    MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    if (!GetClipCursor(&cur) || !EqualRect(&r, &cur)) ClipCursor(&r);
}

// Windows asks for the cursor only when the mouse moves
void RefreshCursor() {
    if (g_hwnd) PostMessageW(g_hwnd, WM_SETCURSOR, WPARAM(g_hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

void SetKeyboard(bool on) {
    if (g_keyboard.exchange(on) != on) RefreshCursor();
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
    const RAWMOUSE& m = ri.data.mouse;
    SetKeyboard(true);
    // the cursor is free in the game's menus
    if (!Play()) {
        ClipCursor(nullptr);
        if (m.usButtonFlags & RI_MOUSE_WHEEL) g_wheel += SHORT(m.usButtonData) > 0 ? 1 : -1;
        return;
    }
    Clip();
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
    bool play = Play(), walk = false, used = std::any_of(std::begin(kFixed), std::end(kFixed), Down);
    int stick[8] = {};  // move forward back left right, look up down left right
    static bool pause_held, pause_backs;
    bool pause = false;
    for (size_t i = 0; i < kCount; i++) {
        int b = kActions[i].button;
        if (b == kHostKey || !std::any_of(binds[i].begin(), binds[i].end(), GameDown)) continue;
        used = true;
        if (b == kWalk) walk = true;
        else if (b >= 20 && !play) continue;
        else if (b >= 16) stick[b - 16] = 1;
        else if (b < 8) p.analog[b] = 0xFF;
        else if (b == kStart) {
            // pressed in a menu it backs out of it, and does nothing more once the menu has gone
            if (!pause_held) pause_backs = g_menu;
            pause = true;
            if (!pause_backs) p.buttons |= uint16_t(1u << (b - 8));
            else if (g_menu) p.analog[1] = 0xFF;
        } else p.buttons |= uint16_t(1u << (b - 8));
    }
    pause_held = pause;
    // Enter, Space and Backspace act in menus only; in play they do what they are bound to
    bool confirm = !play && (Down(VK_RETURN) || Down(VK_SPACE));
    if (confirm || NowMs() < g_confirm_until) p.analog[0] = 0xFF;
    if ((!play && Down(VK_BACK)) || (g_menu && Down(VK_RBUTTON))) p.analog[1] = 0xFF;
    if (!play)
        for (int i = 0; i < 4; i++) p.buttons |= uint16_t(Down(kFixed[i]) << i);
    float speed = walk ? float(game::Opt().walk_speed) / 100.0f : 1.0f;
    if (int x = stick[3] - stick[2]) p.lx = int16_t(float(x) * speed * 32767.0f);
    if (int y = stick[0] - stick[1]) p.ly = int16_t(float(y) * speed * 32767.0f);
    if (int x = stick[7] - stick[6]) p.rx = int16_t(x * 32767);
    if (int y = stick[4] - stick[5]) p.ry = int16_t(y * 32767);
    if (NowMs() < g_tap_until) p.buttons |= g_tap;
    if (used) SetKeyboard(true);
}

void SeenPad(const Pad& p) {
    auto pushed = [](int16_t v) { return v > 16000 || v < -16000; };
    if (p.buttons || std::any_of(std::begin(p.analog), std::end(p.analog), [](uint8_t a) { return a > 0x20; }) ||
        pushed(p.lx) || pushed(p.ly) || pushed(p.rx) || pushed(p.ry))
        SetKeyboard(false);
}

void SetMenu(bool menu) {
    if (g_menu.exchange(menu) != menu) RefreshCursor();
}

bool KeyboardActive() { return g_keyboard; }
bool CursorShown() { return g_menu && g_keyboard; }

bool Cursor(float& x, float& y, bool& down) {
    POINT p;
    RECT r;
    if (!Active() || !CursorShown() || !GetCursorPos(&p) || !ScreenToClient(g_hwnd, &p) || !GetClientRect(g_hwnd, &r) ||
        r.right <= 0 || r.bottom <= 0)
        return false;
    // the frame is shown in the middle of the window
    float aspect = game::Aspect(), w = float(r.right), h = float(r.bottom);
    float vw = std::min(w, h * aspect), vh = vw / aspect;
    x = (float(p.x) - (w - vw) / 2) / vw;
    y = (float(p.y) - (h - vh) / 2) / vh;
    down = Down(VK_LBUTTON);
    return true;
}

int TakeWheel() { return g_wheel.exchange(0); }

bool LeftButton() { return Active() && !g_settle && Down(VK_LBUTTON); }

void TapConfirm() { g_confirm_until = NowMs() + kTapMs; }

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

void Bind(size_t action, int vk, int slot) {
    if (action >= kCount || slot < 0 || slot > 1 || (vk && !NameOf(vk))) return;
    // the settings panel acts on the window's key messages, which mouse buttons do not send
    if (kActions[action].button == kHostKey && Mouse(vk)) return;
    {
        std::lock_guard lock(g_bind_lock);
        std::vector<int>& keys = g_binds[action];
        int slots[2] = {0, 0};
        for (size_t i = 0; i < keys.size() && i < 2; i++) slots[i] = keys[i];
        if (vk && slots[1 - slot] == vk) slots[1 - slot] = 0;
        slots[slot] = vk;
        // the settings panel with no key would leave a keyboard player no way back into it
        if (!slots[0] && !slots[1] && action == kPanelAction) return;
        keys.clear();
        for (int k : slots) {
            if (k) keys.push_back(k);
        }
        game::Opt().keys[kActions[action].action.id] = Join(keys);
    }
    app::Save();
}

void Unbind(size_t action) {
    if (action >= kCount || action == kPanelAction) return;
    {
        std::lock_guard lock(g_bind_lock);
        g_binds[action].clear();
        game::Opt().keys[kActions[action].action.id].clear();
    }
    app::Save();
}

bool TakeDeletePress() {
    static bool held = false;
    bool down = Down(VK_DELETE);
    bool fresh = down && !held;
    held = down;
    return fresh && Active();
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

std::string ButtonKey(int button) {
    std::lock_guard lock(g_bind_lock);
    for (size_t i = 0; i < kCount; i++) {
        if (kActions[i].button != button || g_binds[i].empty()) continue;
        std::string name = g_binds[i][0] == VK_ESCAPE ? "Esc" : NameOf(g_binds[i][0]);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return name;
    }
    return {};
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
