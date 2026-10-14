// Quit on the main menu, as the SCDA recomp has: a QUIT button under the title menu's own opens the game's
// ConfirmWarning page with its unused "are you sure you want to quit?" text. Yes closes the window, No goes back.
// Back on the title menu, which did nothing on the console, opens the same page.
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "core/log.h"
#include "game/flash.h"
#include "xdk/hle.h"

using namespace kernel;
using flash::Member;

namespace {

// front end: +0x44C the menu input of this tick, +0x454 input on, +0x455 held off, +0x381C 2 once the page is ready,
// +0x3820 the page asked for, +0x3840 the page's name, +0x3884 1 once a page is asked for, +0x388C page leaving,
// +0x390F ready
constexpr uint32_t kInputId = 0x44C;
constexpr uint32_t kInputOn = 0x454;
constexpr uint32_t kInputHeldOff = 0x455;
constexpr uint32_t kPageReady = 0x381C;
constexpr uint32_t kTargetPage = 0x3820;
constexpr uint32_t kPageName = 0x3840;
constexpr uint32_t kPageAsked = 0x3884;
constexpr uint32_t kPageLeaving = 0x388C;
constexpr uint32_t kUiReady = 0x390F;
constexpr uint32_t kPageRunning = 2;
constexpr uint32_t kConfirmInput = 5;
constexpr uint32_t kPlayerReady = 0x2ED408;

// game object: +0x21070 its state, the front end's at +0x20220
constexpr uint32_t kGame = 0x2D45F0;
constexpr uint32_t kGameState = 0x21070;
constexpr uint32_t kFrontEnd = 0x20220;

// ConfirmWarning's title and message, text bindings as SetupConfirmWarning (0x11A660) fills them
constexpr uint32_t kConfirmTitle = 0x2EC600;
constexpr uint32_t kConfirmMessage = 0x2EC8F8;
constexpr uint32_t kSetText = 0x11A4A0;  // eax binding, ebx UTF-8 text
constexpr uint32_t kStrings = 0x2D45D8;
constexpr uint32_t kLookup = 0x1F3A80;   // thiscall strings (string id): UTF-8 text, 0 before they load
constexpr uint32_t kQuitId = 0x4A8272D7;         // FE_QUIT
constexpr uint32_t kQuitQuestionId = 0x50756353;  // "are you sure you want to quit?"

// clip: +0x1C x, +0x20 y, +0x24 its colours, +0x48 its data; data +0x1C flags
constexpr uint32_t kClipX = 0x1C;
constexpr uint32_t kClipY = 0x20;
constexpr uint32_t kClipColours = 0x24;
constexpr uint32_t kColoursSize = 0x20;
constexpr uint32_t kClipData = 0x48;
constexpr uint32_t kDataFlags = 0x1C;
constexpr uint32_t kPlaying = 0x2000000;
constexpr uint32_t kSprite = 0xD;

constexpr char kConfirmPage[] = "ConfirmWarning";
constexpr char kQuitText[] = "$FE_QUIT";
constexpr char kButtonName[] = "QuitButton";
constexpr char kButtonDepth[] = "100";
constexpr float kPitch = 25.0f;  // between the stock buttons

struct TitleMenu {
    const char* page;
    const char* last_clip;  // in Main.TextAnim, ours goes under it
    const char* first;      // the ring's first and last button objects
    const char* last;
};
// longest name first, they are compared by prefix
constexpr TitleMenu kTitleMenus[] = {
    {"BlackTitleMenu2", "options_text", "startButton", "optionsButton"},
    {"BlackTitleMenu", "operatorprofile_text", "continueButton", "profileButton"},
};

const TitleMenu* g_return = &kTitleMenus[1];  // the title menu No goes back to
bool g_confirm_ready = false;  // the page's No and back lead there
bool g_quitting = false;

bool HasPrefix(const char* text, const char* prefix) { return std::strncmp(text, prefix, std::strlen(prefix)) == 0; }

const TitleMenu* FindTitleMenu(const char* page) {
    for (const TitleMenu& menu : kTitleMenus)
        if (HasPrefix(page, menu.page)) return &menu;
    return nullptr;
}

bool InFrontEnd() {
    uint32_t game = R32(kGame);
    return game && R32(game + kGameState) == game + kFrontEnd;
}

bool InputTaken(uint32_t ui) {
    return G<uint8_t>(ui + kUiReady) && G<uint8_t>(ui + kInputOn) && !G<uint8_t>(ui + kInputHeldOff);
}

void SetConfirmText(X86Context& ctx, uint32_t binding, uint32_t id) {
    ctx.ecx = R32(kStrings);
    uint32_t text = CallGuest(ctx, kLookup, {id});
    ctx.eax = binding;
    ctx.ebx = text;
    CallGuest(ctx, kSetText, {});
}

// a stopped copy of the last stock button
uint32_t CopyButton(X86Context& ctx, const TitleMenu& title, uint32_t holder) {
    std::string path = std::string("_level4.Main.TextAnim.") + title.last_clip;
    uint32_t function = flash::Text("duplicateMovieClip");
    ctx.eax = flash::Text(path.c_str());
    CallGuest(ctx, flash::kCall, {function, 0, 2, flash::Text(kButtonName), flash::Text(kButtonDepth)});
    uint32_t clip = Member(ctx, holder, kButtonName);
    if (flash::Type(clip) != kSprite) return 0;
    uint32_t data = R32(clip + kClipData);
    W32(data + kDataFlags, R32(data + kDataFlags) & ~kPlaying);
    return clip;
}

// it keeps its place under the last stock button, which the page fades in
void Follow(uint32_t button, uint32_t lead) {
    G<float>(button + kClipX) = G<float>(lead + kClipX);
    G<float>(button + kClipY) = G<float>(lead + kClipY) + kPitch;
    std::memcpy(HostPtr(button + kClipColours), HostPtr(lead + kClipColours), kColoursSize);
}

// the button joins the ring and asks the question; back asks it too
bool JoinRing(X86Context& ctx, const TitleMenu& title, uint32_t root, uint32_t button) {
    uint32_t first = Member(ctx, root, title.first);
    uint32_t last = Member(ctx, root, title.last);
    uint32_t menu = Member(ctx, root, "MyMenu");
    if (!first || !last || !menu || !flash::Button(ctx, button, first)) return false;
    if (!flash::Set(ctx, button, "actionSelectPage", flash::String(ctx, kConfirmPage))) return false;
    flash::Link(ctx, last, button);
    flash::Link(ctx, button, first);
    return flash::Set(ctx, menu, "mstrPageBack", flash::String(ctx, kConfirmPage));
}

bool AddButton(X86Context& ctx, const TitleMenu& title, uint32_t root, uint32_t holder, uint32_t lead) {
    uint32_t button = CopyButton(ctx, title, holder);
    uint32_t field = button ? flash::FindField(button, nullptr) : 0;
    if (!field || !flash::Set(ctx, field, "variable", flash::String(ctx, kQuitText))) return false;
    Follow(button, lead);
    CallGuest(ctx, flash::kRunActions, {R32(flash::kPlayer)});
    return JoinRing(ctx, title, root, button);
}

void UpdateButton(X86Context& ctx, uint32_t ui, const TitleMenu& title, uint32_t root) {
    uint32_t holder = Member(ctx, Member(ctx, root, "Main"), "TextAnim");
    uint32_t lead = Member(ctx, holder, title.last_clip);
    if (!flash::IsClip(lead)) return;
    if (uint32_t button = Member(ctx, holder, kButtonName)) {
        Follow(button, lead);
    } else if (!R32(ui + kPageLeaving) && R32(ui + kPageAsked) != 1) {
        bool added = AddButton(ctx, title, root, holder, lead);
        LOGI("quit: %s %s", title.page, added ? "button added" : "button failed");
    }
}

// as the question is asked, before the page loads and reads its text
void ApplyQuestion(X86Context& ctx, uint32_t ui, const TitleMenu& title) {
    const char* target = static_cast<const char*>(HostPtr(ui + kTargetPage));
    if (R32(ui + kPageAsked) != 1 || !HasPrefix(target, kConfirmPage)) return;
    SetConfirmText(ctx, kConfirmTitle, kQuitId);
    SetConfirmText(ctx, kConfirmMessage, kQuitQuestionId);
    g_return = &title;
}

// No and back lead to the title menu; the page took them from what the last pause menu left, or none
bool PointBack(X86Context& ctx, uint32_t root) {
    uint32_t menu = Member(ctx, root, "MyMenu");
    uint32_t no = Member(ctx, root, "noButton");
    if (!menu || !no) return false;
    return flash::Set(ctx, no, "actionSelectPage", flash::String(ctx, g_return->page)) &&
           flash::Set(ctx, menu, "mstrPageBack", flash::String(ctx, g_return->page));
}

// Yes is answered here and its input taken, so the game code a pause menu left on it never runs
void UpdateConfirm(X86Context& ctx, uint32_t ui, uint32_t root) {
    if (!g_confirm_ready) g_confirm_ready = PointBack(ctx, root);
    if (R32(ui + kPageLeaving) || R32(ui + kPageAsked) == 1 || !InputTaken(ui)) return;
    if (R32(ui + kInputId) != kConfirmInput) return;
    uint32_t yes = Member(ctx, root, "yesButton");
    uint32_t focus = Member(ctx, Member(ctx, root, "MyMenu"), "mFocusButton");
    if (!yes || focus != yes) return;
    W32(ui + kInputId, 0);
    if (g_quitting) return;
    g_quitting = true;
    LOGI("quit: closing the window");
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
}

void Tick(X86Context& ctx, uint32_t ui) {
    const char* page = static_cast<const char*>(HostPtr(ui + kPageName));
    bool confirm = HasPrefix(page, kConfirmPage) && InFrontEnd();
    if (!confirm) g_confirm_ready = false;
    if (R32(kPlayerReady) != 1 || !R32(flash::kPlayer) || R32(ui + kPageReady) != kPageRunning) return;
    uint32_t root = flash::Level(4);
    if (!root) return;
    if (const TitleMenu* title = FindTitleMenu(page)) {
        UpdateButton(ctx, ui, *title, root);
        ApplyQuestion(ctx, ui, *title);
    }
    if (confirm) UpdateConfirm(ctx, ui, root);
}

}  // namespace

// ebp the front end, after the cursor's input and before the Flash player runs the page's script
void ExitMenu(X86Context& ctx, uint8_t*) {
    X86Context saved = ctx;
    Tick(ctx, ctx.ebp);
    ctx = saved;
}
