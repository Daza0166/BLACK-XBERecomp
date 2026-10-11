// the options panel on F10
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <imgui.h>

#include "app/app.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"

namespace app {

namespace {

std::atomic<bool> g_open{false};

// the cover art's brass on black
constexpr ImVec4 kPanelBg{0.035f, 0.03f, 0.025f, 0.94f};
constexpr ImVec4 kBorder{0.72f, 0.58f, 0.30f, 0.85f};
constexpr ImVec4 kText{0.90f, 0.88f, 0.84f, 1.0f};
constexpr ImVec4 kHighlight{0.30f, 0.23f, 0.10f, 1.0f};
constexpr ImVec4 kHeading{0.86f, 0.69f, 0.34f, 1.0f};
constexpr ImVec4 kDim{0.62f, 0.58f, 0.50f, 1.0f};
constexpr ImVec4 kTab{0.12f, 0.10f, 0.07f, 1.0f};
constexpr ImVec4 kTabHovered{0.20f, 0.16f, 0.09f, 1.0f};
constexpr ImVec4 kButton{0.17f, 0.14f, 0.08f, 1.0f};
constexpr ImVec4 kButtonHovered{0.40f, 0.31f, 0.14f, 1.0f};  // lighter than the highlight, so it shows on a selected row
constexpr ImVec4 kLine{0.86f, 0.69f, 0.34f, 0.35f};
constexpr std::pair<ImGuiCol, ImVec4> kPanelColors[] = {
    {ImGuiCol_WindowBg, kPanelBg}, {ImGuiCol_Border, kBorder}, {ImGuiCol_Text, kText},
    {ImGuiCol_Header, kHighlight}, {ImGuiCol_HeaderHovered, kHighlight}, {ImGuiCol_HeaderActive, kHighlight},
    {ImGuiCol_Button, kButton}, {ImGuiCol_ButtonHovered, kButtonHovered}, {ImGuiCol_ButtonActive, kButtonHovered},
    {ImGuiCol_FrameBg, kTab}, {ImGuiCol_FrameBgHovered, kTabHovered}, {ImGuiCol_FrameBgActive, kHighlight},
    {ImGuiCol_ScrollbarGrab, kHighlight}, {ImGuiCol_ScrollbarGrabHovered, kButtonHovered},
    {ImGuiCol_ScrollbarGrabActive, kHeading}, {ImGuiCol_Separator, kLine}, {ImGuiCol_NavHighlight, kHeading},
};

constexpr uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"GAME DEFAULT", "2x", "4x", "8x", "16x"};
constexpr uint32_t kDistances[] = {1, 2, 4, 8};
const char* const kDistanceNames[] = {"GAME DEFAULT", "2x", "4x", "8x"};
constexpr uint32_t kMsaaValues[] = {0, 2, 4, 8};
const char* const kMsaaNames[] = {"OFF", "2x", "4x", "8x"};
constexpr uint32_t kFpsCaps[] = {30, 60, 75, 90, 120, 144, 165, 240, 360, 0};

int StepIndex(int index, int step, int count) {
    if (count <= 0) return 0;
    index = (index + step) % count;
    return index < 0 ? index + count : index;
}

// index nearest value, stepped first
template <size_t N>
int StepTable(const uint32_t (&values)[N], uint32_t& value, int step) {
    int best = 0;
    uint32_t best_d = UINT32_MAX;
    for (int i = 0; i < int(N); i++) {
        uint32_t d = values[i] > value ? values[i] - value : value - values[i];
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (step) {
        best = StepIndex(best, step, int(N));
        value = values[best];
    }
    return best;
}

std::vector<Mode> Modes() {
    std::vector<Mode> modes;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++) {
        if (dm.dmBitsPerPel < 32 || dm.dmPelsWidth < 640) continue;
        Mode m{dm.dmPelsWidth, dm.dmPelsHeight};
        if (std::none_of(modes.begin(), modes.end(), [&](const Mode& o) { return o.width == m.width && o.height == m.height; }))
            modes.push_back(m);
    }
    std::sort(modes.begin(), modes.end(), [](const Mode& a, const Mode& b) { return a.width != b.width ? a.width > b.width : a.height > b.height; });
    return modes;
}

void Section(const char* title) {
    float em = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, em * 0.2f));
}

// label left, arrows and value right, the row is the only nav stop
int OptionRow(const char* label, const char* value) {
    const ImGuiStyle& style = ImGui::GetStyle();
    float em = ImGui::GetFontSize(), row_w = ImGui::GetContentRegionAvail().x, row_h = ImGui::GetFrameHeight();
    float value_w = em * 12, block_w = row_h * 2 + value_w + style.ItemSpacing.x * 2;
    float row_x = ImGui::GetCursorPosX(), row_y = ImGui::GetCursorPosY();
    ImGui::PushID(label);
    int step = 0;
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
    if (ImGui::IsItemFocused()) {
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Header));
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) step = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) step = 1;
    }
    ImGui::SetCursorPos(ImVec2(row_x + style.ItemInnerSpacing.x, row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_ButtonRepeat, true);  // held arrows keep stepping
    float block_x = row_x + row_w - block_w;
    ImGui::SetCursorPos(ImVec2(block_x, row_y));
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) step = -1;
    float value_x = block_x + row_h + style.ItemSpacing.x;
    float pad = (value_w - ImGui::CalcTextSize(value).x) * 0.5f;
    ImGui::SetCursorPos(ImVec2(value_x + std::max(pad, 0.0f), row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value);
    ImGui::SetCursorPos(ImVec2(value_x + value_w + style.ItemSpacing.x, row_y));
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) step = 1;
    ImGui::PopItemFlag();
    ImGui::PopID();
    ImGui::SetCursorPos(ImVec2(row_x, row_y));
    ImGui::Dummy(ImVec2(row_w, row_h));
    return step;
}

const char* OnOff(bool on) { return on ? "ON" : "OFF"; }

void Flip(bool& on, int step) {
    if (step) on = !on;
}

// applies and saves
void Changed(bool display) {
    if (display) ApplyDisplay();
    Save();
}

enum RowId { kDisplayMode, kResolution, kRenderResolution, kMaxFps, kVsync, kFpsCounter, kAniso, kReloadBlur, kSkipIntros, kSensitivity, kInvertMouse, kWalkSpeed, kFov, kViewmodelFov, kHoldAim, kDrawDistance, kMsaa };

using rows::Kind;
using rows::Row;

constexpr Row kSettingsRows[] = {
    {Kind::Heading, -1, "DISPLAY"},
    {Kind::Value, kDisplayMode, "DISPLAY MODE"},
    {Kind::Value, kResolution, "RESOLUTION"},
    {Kind::Value, kRenderResolution, "RENDER RESOLUTION"},
    {Kind::Value, kFov, "FIELD OF VIEW"},
    {Kind::Value, kViewmodelFov, "VIEWMODEL FIELD OF VIEW"},
    {Kind::Value, kVsync, "VERTICAL SYNC"},
    {Kind::Value, kMaxFps, "MAX FRAME RATE"},
    {Kind::Value, kFpsCounter, "FPS COUNTER"},
    {Kind::Heading, -1, "GRAPHICS"},
    {Kind::Value, kMsaa, "MSAA"},
    {Kind::Value, kAniso, "ANISOTROPIC FILTERING"},
    {Kind::Value, kDrawDistance, "DRAW DISTANCE"},
    {Kind::Value, kReloadBlur, "RELOAD BLUR"},
    {Kind::Heading, -1, "GENERAL"},
    {Kind::Value, kSkipIntros, "SKIP INTROS"},
};

const std::vector<Row>& InputRows() {
    static const std::vector<Row> list = [] {
        std::vector<Row> out = {{Kind::Heading, -1, "MOUSE"},
                                {Kind::Value, kSensitivity, "MOUSE SENSITIVITY"},
                                {Kind::Value, kInvertMouse, "INVERT LOOK"},
                                {Kind::Value, kHoldAim, "HOLD TO AIM"},
                                {Kind::Heading, -1, "KEYBOARD"}};
        size_t count = 0;
        const input::Action* actions = input::Actions(&count);
        for (size_t i = 0; i < count; i++) out.push_back({Kind::Bind, int(i), actions[i].label});
        out.push_back({Kind::Value, kWalkSpeed, "WALK SPEED"});
        return out;
    }();
    return list;
}

// steps the value first when step is not zero
std::string RowText(int id, int step, Settings& s = Get()) {
    game::Options& o = game::Opt();
    char text[64] = "";
    bool display = false;
    switch (id) {
        case kDisplayMode: {
            static const char* const kModes[] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
            if (step) s.display_mode = DisplayMode(StepIndex(int(s.display_mode), step, 3));
            display = true;
            std::snprintf(text, sizeof(text), "%s", kModes[std::min(int(s.display_mode), 2)]);
            break;
        }
        case kResolution: {
            static const std::vector<Mode> modes = Modes();
            Mode desktop = Desktop(), m = s.width && s.height ? Mode{s.width, s.height} : desktop;
            int i = 0;
            for (size_t k = 0; k < modes.size(); k++)
                if (modes[k].width == m.width && modes[k].height == m.height) i = int(k);
            if (step && !modes.empty()) {
                m = modes[size_t(StepIndex(i, step, int(modes.size())))];
                // the desktop's own mode is kept as 0, which follows the desktop
                bool native = m.width == desktop.width && m.height == desktop.height;
                s.width = native ? 0 : m.width;
                s.height = native ? 0 : m.height;
            }
            display = true;
            std::snprintf(text, sizeof(text), "%ux%u", m.width, m.height);
            break;
        }
        case kRenderResolution: {
            // 20% to 200% in steps of 5
            if (step) s.render_percent = 20 + 5 * uint32_t(StepIndex((std::clamp(int(s.render_percent), 20, 200) - 20) / 5, step, 37));
            Mode out = Output();
            std::snprintf(text, sizeof(text), "%u%% (%ux%u)", s.render_percent, out.width * s.render_percent / 100,
                          out.height * s.render_percent / 100);
            break;
        }
        case kViewmodelFov:
            if (step) o.viewmodel_fov = 45 + uint32_t(StepIndex(std::clamp(int(o.viewmodel_fov), 45, 140) - 45, step, 96));
            std::snprintf(text, sizeof(text), o.viewmodel_fov == 70 ? "%u (DEFAULT)" : "%u", o.viewmodel_fov);
            break;
        case kFov:
            if (step) o.fov = 45 + uint32_t(StepIndex(std::clamp(int(o.fov), 45, 140) - 45, step, 96));
            std::snprintf(text, sizeof(text), o.fov == 70 ? "%u (DEFAULT)" : "%u", o.fov);
            break;
        case kMaxFps: {
            uint32_t cap = kFpsCaps[StepTable(kFpsCaps, s.fps_cap, step)];
            if (cap) std::snprintf(text, sizeof(text), "%u", cap);
            else std::snprintf(text, sizeof(text), "UNLIMITED");
            break;
        }
        case kVsync:
            Flip(s.vsync, step);
            std::snprintf(text, sizeof(text), "%s", OnOff(s.vsync));
            break;
        case kFpsCounter:
            Flip(o.fps_counter, step);
            std::snprintf(text, sizeof(text), "%s", OnOff(o.fps_counter));
            break;
        case kAniso:
            std::snprintf(text, sizeof(text), "%s", kAnisoNames[StepTable(kAnisoValues, s.aniso, step)]);
            break;
        case kDrawDistance:
            std::snprintf(text, sizeof(text), "%s", kDistanceNames[StepTable(kDistances, o.draw_distance, step)]);
            break;
        case kMsaa:
            std::snprintf(text, sizeof(text), "%s", kMsaaNames[StepTable(kMsaaValues, o.msaa, step)]);
            break;
        case kReloadBlur:
            Flip(o.reload_blur, step);
            std::snprintf(text, sizeof(text), "%s", OnOff(o.reload_blur));
            break;
        case kSkipIntros:
            Flip(o.skip_intros, step);
            std::snprintf(text, sizeof(text), "%s", OnOff(o.skip_intros));
            break;
        case kSensitivity:
            // 0.1 to 5.0 in steps of 0.1
            if (step) {
                int i = std::clamp(int(std::lround(o.mouse_sensitivity * 10.0f)), 1, 50) - 1;
                o.mouse_sensitivity = float(StepIndex(i, step, 50) + 1) / 10.0f;
            }
            std::snprintf(text, sizeof(text), "%.1f", o.mouse_sensitivity);
            break;
        case kInvertMouse:
            Flip(o.invert_mouse, step);
            std::snprintf(text, sizeof(text), "%s", OnOff(o.invert_mouse));
            break;
        case kHoldAim:
            Flip(o.hold_aim, step);
            std::snprintf(text, sizeof(text), "%s", OnOff(o.hold_aim));
            break;
        case kWalkSpeed:
            // 10% to 100% in steps of 5
            if (step) o.walk_speed = 10 + 5 * uint32_t(StepIndex((std::clamp(int(o.walk_speed), 10, 100) - 10) / 5, step, 19));
            std::snprintf(text, sizeof(text), "%u%%", o.walk_speed);
            break;
    }
    if (step && &s == &Get()) Changed(display);
    return text;
}

std::string KeyText(std::string keys) {
    if (keys.empty()) return "UNBOUND";
    for (char& c : keys) c = c == ',' ? '/' : char(std::toupper(uint8_t(c)));
    return keys;
}

void RestoreDefaults(int page) {
    game::Options& o = game::Opt();
    if (page == 1) {
        o.mouse_sensitivity = game::Options{}.mouse_sensitivity;
        o.invert_mouse = false;
        o.hold_aim = true;
        o.walk_speed = game::Options{}.walk_speed;
        input::ResetBindings();
        return;
    }
    game::Options keep = o;
    Get() = Settings{};
    o = game::Options{};
    o.mouse_sensitivity = keep.mouse_sensitivity;
    o.invert_mouse = keep.invert_mouse;
    o.hold_aim = keep.hold_aim;
    o.walk_speed = keep.walk_speed;
    o.keys = keep.keys;
    Changed(true);
}

// the row being rebound, -1 when idle; its keys must be released before the next press binds
std::atomic<int> g_capture{-1};
bool g_capture_armed = false;
int g_capture_done = 0;  // frame of the last bind, the key that bound must not start another
int g_page = 0;
const char* const kPages[] = {"RECOMP SETTINGS", "MOUSE AND KEYBOARD"};
// the display mode and resolution wait for Apply, as Windows' display settings do; closing drops them
bool g_display_held = false;
Settings g_display;

bool DisplayRow(int id) { return id == kDisplayMode || id == kResolution; }

void HoldDisplay(int id, int step) {
    const Settings& s = Get();
    if (!g_display_held) {
        g_display.display_mode = s.display_mode;
        g_display.width = s.width;
        g_display.height = s.height;
    }
    RowText(id, step, g_display);
    g_display_held =
        g_display.display_mode != s.display_mode || g_display.width != s.width || g_display.height != s.height;
}

void Apply() {
    Settings& s = Get();
    s.display_mode = g_display.display_mode;
    s.width = g_display.width;
    s.height = g_display.height;
    g_display_held = false;
    Changed(true);
}

// label left, keys right; Enter or a click starts the capture
bool BindRow(const char* label, const std::string& keys, bool capturing) {
    const ImGuiStyle& style = ImGui::GetStyle();
    float em = ImGui::GetFontSize(), row_w = ImGui::GetContentRegionAvail().x, row_h = ImGui::GetFrameHeight();
    float value_w = em * 12, block_w = row_h * 2 + value_w + style.ItemSpacing.x * 2;
    float row_x = ImGui::GetCursorPosX(), row_y = ImGui::GetCursorPosY();
    ImGui::PushID(label);
    bool pressed = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
    if (ImGui::IsItemFocused() || capturing)
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Header));
    ImGui::SetCursorPos(ImVec2(row_x + style.ItemInnerSpacing.x, row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    std::string value = capturing ? std::string("PRESS A KEY") : keys;
    float value_x = row_x + row_w - block_w + row_h + style.ItemSpacing.x;
    ImGui::SetCursorPos(ImVec2(value_x + std::max((value_w - ImGui::CalcTextSize(value.c_str()).x) * 0.5f, 0.0f), row_y));
    ImGui::AlignTextToFramePadding();
    if (capturing) ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    ImGui::TextUnformatted(value.c_str());
    if (capturing) ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::SetCursorPos(ImVec2(row_x, row_y));
    ImGui::Dummy(ImVec2(row_w, row_h));
    return pressed;
}

// Escape cancels, any other bindable key or mouse button binds
void PollCapture() {
    if (g_capture < 0) return;
    int vk = input::PressedKey();
    if (!g_capture_armed) {
        g_capture_armed = !vk;
        return;
    }
    if (!vk) return;
    if (vk != VK_ESCAPE) input::Bind(size_t(g_capture.load()), vk);
    g_capture = -1;
    g_capture_done = ImGui::GetFrameCount();
}

void DrawRows(int page) {
    size_t count = 0;
    const Row* list = rows::Page(page, &count);
    for (size_t i = 0; i < count; i++) {
        const Row& r = list[i];
        if (r.kind == Kind::Heading) {
            Section(r.label);
        } else if (r.kind == Kind::Value) {
            bool held = g_display_held && DisplayRow(r.id);
            if (int step = OptionRow(r.label, RowText(r.id, 0, held ? g_display : Get()).c_str())) {
                if (DisplayRow(r.id)) HoldDisplay(r.id, step);
                else RowText(r.id, step);
            }
        } else if (BindRow(r.label, KeyText(input::Binding(size_t(r.id))), g_capture == r.id) && g_capture < 0 &&
                   ImGui::GetFrameCount() > g_capture_done + 1) {
            g_capture = r.id;
            g_capture_armed = false;
        }
    }
}

// one tab a page, as wide as they share the line; LB / RB step them
void DrawTabs() {
    int step = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) - ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false);
    // a selection made here shows from the next frame, the tab drawn this one still reads as selected; not under
    // a bind or on the button that just bound
    bool set = step && g_capture < 0 && ImGui::GetFrameCount() > g_capture_done + 1;
    int pages = int(std::size(kPages));
    if (set) g_page = StepIndex(g_page, step, pages);
    ImGui::PushStyleColor(ImGuiCol_Tab, kTab);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, kTabHovered);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, kHighlight);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, kHeading);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
    if (ImGui::BeginTabBar("##pages")) {
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x * float(pages - 1)) / float(pages);
        for (int i = 0; i < pages; i++) {
            ImGui::SetNextItemWidth(w);
            if (ImGui::BeginTabItem(kPages[i], nullptr, set && i == g_page ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (!set && i != g_page) {
                    g_page = i;
                    g_capture = -1;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

void DrawPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * 50, em * 32), ImGuiCond_Always);
    for (const auto& [col, color] : kPanelColors) ImGui::PushStyleColor(col, color);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 1.6f, em * 1.2f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##settings", nullptr, kFlags)) {
        const char* title = "SETTINGS";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) * 0.5f, 0.0f));
        ImGui::TextUnformatted(title);
        DrawTabs();
        float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + em;
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        DrawRows(g_page);
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextUnformatted(g_capture >= 0 ? "Press a key or mouse button to bind, Escape cancels" : "");
        ImGui::PopStyleColor();
        float bw = em * 8;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - bw * 3 - ImGui::GetStyle().ItemSpacing.x * 2, 0.0f));
        if (ImGui::Button("Back", ImVec2(bw, 0))) ToggleMenu();
        ImGui::SameLine();
        if (ImGui::Button("Defaults", ImVec2(bw, 0))) {
            g_capture = -1;
            if (g_page == 0) g_display_held = false;
            RestoreDefaults(g_page);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!g_display_held);
        if (ImGui::Button("Apply", ImVec2(bw, 0))) Apply();
        ImGui::EndDisabled();
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(int(std::size(kPanelColors)));
}

}  // namespace

const rows::Row* rows::Page(int page, size_t* count) {
    *count = page == 1 ? InputRows().size() : std::size(kSettingsRows);
    return page == 1 ? InputRows().data() : kSettingsRows;
}

std::string rows::Value(const Row& row, int step) {
    return row.kind == Kind::Bind ? KeyText(input::Binding(size_t(row.id))) : RowText(row.id, step);
}

void rows::Defaults(int page) { RestoreDefaults(page); }

bool MenuOpen() { return g_open.load(std::memory_order_relaxed); }

bool Capturing() { return g_capture >= 0; }

void ToggleMenu() {
    bool open = !MenuOpen();
    g_open.store(open, std::memory_order_relaxed);
    g_capture = -1;
    g_display_held = false;
    if (open) input::ReleaseCursor();
}

void DrawUi(double fps) {
    game::g_fps = float(fps);
    ImGuiIO& io = ImGui::GetIO();
    if (MenuOpen()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.KeyRepeatRate = 0.1f;  // held arrows step ten times a second, ImGui's twenty is too fast for settings
    // scale the font with the output height
    io.FontGlobalScale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    if (MenuOpen()) {
        PollCapture();
        DrawPanel();
    }
}

}  // namespace app
