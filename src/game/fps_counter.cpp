// Frame rate (top left), drawn by the game in its small font over the finished frame.
#include <algorithm>
#include <bit>
#include <cstdio>

#include "app/app.h"
#include "common/options.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

namespace {

// the game's 2D layer: Text(font, wide text, x, y, size, rgba floats), viewport pixels from the top left
constexpr uint32_t kBegin = 0xD99A0, kText = 0xD9600, kFlush = 0xD4C50, kEnd = 0xD3740, kApplyStates = 0x29EF0;
constexpr uint32_t kQuads = 0x2F3710, kTexture = 0x56EF30;
// the small font, +8 its glyph width scale
constexpr uint32_t kFont = 0x2F433C, kWidthScale = 0x08;
constexpr float kSize = 15, kMargin = 8;  // on a frame 480 high

}  // namespace

// the back buffer is the target here, the frame is shown next
void DrawFps(X86Context& ctx, uint8_t*) {
    uint32_t font = R32(kFont), viewport = SYM(D3D_Device) + XDK_CDevice_Viewport;
    float w = float(R32(viewport + 8)), h = float(R32(viewport + 12));
    if (!game::Opt().fps_counter || !font || !(h > 0)) return;
    static const uint32_t block = mem::AllocSystem(0x60);
    uint32_t colour = block, chars = block + 0x10;
    char text[32];
    int n = std::min(std::snprintf(text, sizeof text, "FPS: %.1f", double(game::g_fps.load())), 31);
    for (int i = 0; i <= n; i++) G<uint16_t>(chars + i * 2) = i < n ? uint8_t(text[i]) : 0;
    // a wide frame is shown stretched: the glyphs are drawn narrower by as much
    float unit = h / 480, narrow = w / h / (app::Widescreen() ? 16.0f / 9 : 4.0f / 3);
    float& width = G<float>(font + kWidthScale);
    float game_width = width;
    CallGuest(ctx, kBegin, {});
    width = game_width * narrow;
    for (int shadow = 1; shadow >= 0; shadow--) {
        for (int i = 0; i < 4; i++) G<float>(colour + i * 4) = shadow && i < 3 ? 0.0f : 1.0f;
        float x = (kMargin + shadow) * unit * narrow, y = (kMargin + shadow) * unit;
        CallGuest(ctx, kText, {font, chars, std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y),
                               std::bit_cast<uint32_t>(kSize * unit), colour});
    }
    width = game_width;
    if (R32(kQuads)) CallGuest(ctx, kFlush, {});
    CallGuest(ctx, kEnd, {});
    W32(kTexture, 0);
    CallGuest(ctx, kApplyStates, {});
}
