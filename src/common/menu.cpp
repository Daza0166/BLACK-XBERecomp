// the options panel on F10
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <string>
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
#include "common/options.h"

namespace app {

namespace {

std::atomic<bool> g_open{false};

constexpr ImVec4 kPanelBg{0.05f, 0.03f, 0.03f, 0.94f};
constexpr ImVec4 kBorder{0.88f, 0.84f, 0.76f, 0.80f};
constexpr ImVec4 kText{0.93f, 0.91f, 0.87f, 1.0f};
constexpr ImVec4 kHighlight{0.55f, 0.12f, 0.12f, 1.0f};
constexpr ImVec4 kHeading{0.82f, 0.28f, 0.22f, 1.0f};
constexpr ImVec4 kDim{0.62f, 0.59f, 0.55f, 1.0f};

constexpr uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"GAME DEFAULT", "2x", "4x", "8x", "16x"};
constexpr uint32_t kFpsCaps[] = {0, 30, 60, 75, 90, 120, 144, 165, 240, 360};

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
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
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

// applies and saves
void Changed(bool display) {
    if (display) ApplyDisplay();
    Save();
}

enum RowId { kDisplayMode, kResolution, kRenderResolution, kMaxFps, kVsync, kFpsCounter, kAniso, kSkipIntros };

struct Row {
    int id;  // -1 is a heading
    const char* label;
};

constexpr Row kRows[] = {
    {-1, "DISPLAY"},
    {kDisplayMode, "DISPLAY MODE"},
    {kResolution, "RESOLUTION"},
    {kRenderResolution, "RENDER RESOLUTION"},
    {kMaxFps, "MAX FRAME RATE"},
    {kVsync, "VSYNC"},
    {kFpsCounter, "FPS COUNTER"},
    {-1, "GRAPHICS"},
    {kAniso, "ANISOTROPIC FILTERING"},
    {-1, "GAME"},
    {kSkipIntros, "SKIP INTROS"},
};

// steps the value first when step is not zero
std::string RowText(int id, int step) {
    Settings& s = Get();
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
            int i = 0;
            for (size_t k = 0; k < modes.size() && !i; k++)
                if (modes[k].width == s.width && modes[k].height == s.height) i = int(k) + 1;
            if (step) {
                i = StepIndex(i, step, int(modes.size()) + 1);
                s.width = i ? modes[size_t(i - 1)].width : 0;
                s.height = i ? modes[size_t(i - 1)].height : 0;
            }
            display = true;
            Mode m = i ? modes[size_t(i - 1)] : Desktop();
            std::snprintf(text, sizeof(text), i ? "%ux%u" : "DESKTOP (%ux%u)", m.width, m.height);
            break;
        }
        case kRenderResolution: {
            if (step) s.render_percent = uint32_t(std::clamp(int(s.render_percent) + step * 5, 20, 200));
            Mode out = Output();
            std::snprintf(text, sizeof(text), "%u%% (%ux%u)", s.render_percent, out.width * s.render_percent / 100,
                          out.height * s.render_percent / 100);
            break;
        }
        case kMaxFps: {
            uint32_t cap = kFpsCaps[StepTable(kFpsCaps, s.fps_cap, step)];
            if (cap) std::snprintf(text, sizeof(text), "%u", cap);
            else std::snprintf(text, sizeof(text), "UNLIMITED");
            break;
        }
        case kVsync:
            if (step) s.vsync = !s.vsync;
            std::snprintf(text, sizeof(text), "%s", OnOff(s.vsync));
            break;
        case kFpsCounter:
            if (step) game::Opt().fps_counter = !game::Opt().fps_counter;
            std::snprintf(text, sizeof(text), "%s", OnOff(game::Opt().fps_counter));
            break;
        case kAniso:
            std::snprintf(text, sizeof(text), "%s", kAnisoNames[StepTable(kAnisoValues, s.aniso, step)]);
            break;
        case kSkipIntros:
            if (step) game::Opt().skip_intros = !game::Opt().skip_intros;
            std::snprintf(text, sizeof(text), "%s", OnOff(game::Opt().skip_intros));
            break;
    }
    if (step) Changed(display);
    return text;
}

void DrawPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * 50, em * 32), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanelBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_Header, kHighlight);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kHighlight);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kHighlight);
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
        ImGui::Separator();
        float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + em;
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        for (const Row& r : kRows) {
            if (r.id < 0) Section(r.label);
            else if (int step = OptionRow(r.label, RowText(r.id, 0).c_str())) RowText(r.id, step);
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextUnformatted("Up and Down select, Left and Right change, Escape closes");
        ImGui::PopStyleColor();
        float bw = em * 8;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - bw * 3 - ImGui::GetStyle().ItemSpacing.x * 2, 0.0f));
        if (ImGui::Button("Back", ImVec2(bw, 0))) ToggleMenu();
        ImGui::SameLine();
        if (ImGui::Button("Defaults", ImVec2(bw, 0))) {
            Get() = Settings{};
            game::Opt() = game::Options{};
            Changed(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply", ImVec2(bw, 0))) Changed(true);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(6);
}

void DrawFps(double fps) {
    char text[32];
    std::snprintf(text, sizeof(text), "FPS: %.1f", fps);
    float em = ImGui::GetFontSize();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImVec2 at(em * 0.8f, em * 0.5f);
    list->AddText(ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 200), text);
    list->AddText(at, IM_COL32(180, 255, 120, 255), text);
}

}  // namespace

bool MenuOpen() { return g_open.load(std::memory_order_relaxed); }

void ToggleMenu() { g_open.store(!MenuOpen(), std::memory_order_relaxed); }

void DrawUi(double fps) {
    ImGuiIO& io = ImGui::GetIO();
    if (MenuOpen()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // scale the font with the output height
    io.FontGlobalScale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    if (game::Opt().fps_counter) DrawFps(fps);
    if (MenuOpen()) DrawPanel();
}

}  // namespace app
