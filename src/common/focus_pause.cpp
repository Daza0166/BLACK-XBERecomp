// pause on focus loss: the game thread waits while the window is in the background or minimized
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "audio/output.h"
#include "common/options.h"
#include "core/log.h"

namespace game {

namespace {

constexpr DWORD kPollMs = 20;

// a hidden window, behind the splash or for a handover, never pauses: it opens from the game thread
bool Unfocused() {
    HWND hwnd = static_cast<HWND>(app::Window());
    if (!hwnd || !IsWindowVisible(hwnd)) return false;
    return GetForegroundWindow() != hwnd || IsIconic(hwnd);
}

}  // namespace

void WaitWhileUnfocused() {
    if (!Opt().pause_on_focus_loss || !Unfocused()) return;
    LOGI("focus lost: paused");
    g_focus_paused = true;
    audio::SetPaused(true);
    while (Opt().pause_on_focus_loss && Unfocused()) Sleep(kPollMs);
    audio::SetPaused(false);
    g_focus_pauses++;
    g_focus_paused = false;
    LOGI("focus back: resumed");
}

}  // namespace game
