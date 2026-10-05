// 2D is drawn 640x480 and shown 16:9. The game narrows its Flash menus itself; what its own 2D layer draws gets
// x' = x * 3/4 + anchor * width / 4: HUD parts by their component's anchor, the rest 4:3 in the middle.
#include <mutex>

#include "app/app.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr uint32_t kVertices = 0x563120, kStride = 0x18, kCount = 0x2F3710;
constexpr uint32_t kDisplay = 0x2D67F4;  // +0 width
constexpr uint32_t kAnchors = 0x2AD968;  // x, y share of the safe area per anchor id
constexpr uint32_t kTaller = 0x2AD9BC;   // frame height the game gives the middle anchors on a widescreen
constexpr uint32_t kFromHud = 0x13C02F, kFromFlash = 0x121E82, kFromHost = 0xFFFFFFF0;  // return addresses
constexpr float kOff = -1, kCentre = 0.5f;

float g_anchor = kOff, g_width = 640;
uint32_t g_mark = 0;  // vertices before it are done
uint32_t g_hud = 0;   // the player's HUD while its list draws

// vertices since the mark. Centred, one on a screen side stays there (bars, fades)
void Squeeze() {
    uint32_t count = R32(kCount);
    for (uint32_t i = g_mark; g_anchor >= 0 && i < count; i++) {
        float& x = G<float>(kVertices + i * kStride);
        if (g_anchor == kCentre && (x <= 0 || x >= g_width)) continue;
        x = x * 0.75f + g_anchor * g_width * 0.25f;
    }
    g_mark = count;
}

}  // namespace

// Begin. Left alone: the Flash player's, the host's, a smaller target's
X86_EXTERN_FUNC(__imp__sub_000D99A0);
X86_FUNC(sub_000D99A0) {
    uint32_t from = R32(ctx.esp), display = R32(kDisplay);
    __imp__sub_000D99A0(ctx, base);
    std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
    uint32_t width = d3d::State().viewport.w;
    bool screen = display && width == R32(display) && from != kFromFlash && from != kFromHost;
    g_anchor = screen && app::Widescreen() ? kCentre : kOff;
    g_width = float(width);
    g_mark = 0;
}

// End
X86_EXTERN_FUNC(__imp__sub_000D3740);
X86_FUNC(sub_000D3740) {
    Squeeze();
    g_anchor = kOff;
    __imp__sub_000D3740(ctx, base);
}

// Flush
X86_EXTERN_FUNC(__imp__sub_000D4C50);
X86_FUNC(sub_000D4C50) {
    Squeeze();
    g_mark = 0;
    __imp__sub_000D4C50(ctx, base);
}

// List draw, ecx the list: a player's HUD holds its own at +0x40
X86_EXTERN_FUNC(__imp__sub_000DA4B0);
X86_FUNC(sub_000DA4B0) {
    g_hud = R32(ctx.esp) == kFromHud ? ctx.ecx - 0x40 : 0;
    __imp__sub_000DA4B0(ctx, base);
    g_hud = 0;
}

// A list's item before it draws, ebx: +0x40 its frame. HUD +0xA0 components: +4 frame, +8 next, +0x10 anchor id
void HudItem(X86Context& ctx, uint8_t*) {
    Squeeze();
    uint32_t frame = R32(ctx.ebx + 0x40);
    float anchor = kCentre;
    for (uint32_t c = g_hud ? R32(g_hud + 0xA0) : 0; c; c = R32(c + 8)) {
        if (R32(c + 4) != frame) continue;
        anchor = G<float>(kAnchors + R32(c + 0x10) * 8);
        if (float& height = G<float>(frame + 0xC); height == G<float>(kTaller)) height = 1;
    }
    if (g_anchor != kOff) g_anchor = anchor;
}
