// Frame rate (top left) and the main menu watermark, drawn by the game in its small font over the finished frame.
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "common/aspect.h"
#include "common/options.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

namespace {

// the game's 2D layer: Text(font, wide text, x, y, size, rgba floats), viewport pixels from the top left
constexpr uint32_t kBegin = 0xD99A0, kText = 0xD9600, kFlush = 0xD4C50, kEnd = 0xD3740, kApplyStates = 0x29EF0;
constexpr uint32_t kQuads = 0x2F3710, kTexture = 0x56EF30;
// the small font: +8 glyph width scale, +0x1C last glyph, +0x20 glyphs by code & 0x7F
constexpr uint32_t kFont = 0x2F433C, kWidthScale = 0x08, kLastGlyph = 0x1C, kGlyphs = 0x20;
// glyph, 0x28 bytes: +0x18 advance, +0x1C code
constexpr uint32_t kGlyphSize = 0x28, kAdvance = 0x18, kCode = 0x1C;
// UI object: +0x3840 page name
constexpr uint32_t kUi = 0x2D199C, kPageName = 0x3840;

constexpr float kSize = 15, kMargin = 8;  // on a frame 480 high
constexpr float kGrey = 186 / 255.0f;  // the HUD's text

// as SCDA: ends 80 in from the right, on the row of the menu's button legend
constexpr char kWatermark[] = "XBE Recomp V1";
constexpr float kWatermarkRight = 80, kWatermarkTop = 427;
// also BlackTitleMenu2 and the _wide pages
constexpr char kTitleMenu[] = "BlackTitleMenu";

struct View {
    float unit;    // viewport pixels per line of a 480 frame
    float narrow;  // a wide frame is shown stretched: glyphs are drawn narrower by as much
};

// the glyph 0xD9600 draws for a code
uint32_t Glyph(uint32_t font, uint16_t code) {
    uint32_t glyph = R32(font + kGlyphs + (code & 0x7F) * 4);
    uint32_t last = R32(font + kLastGlyph);
    while (G<uint16_t>(glyph + kCode) != code && glyph != last) glyph += kGlyphSize;
    return glyph;
}

// in lines of a 480 frame, at kSize
float TextWidth(uint32_t font, const char* text) {
    float advance = 0;
    for (const char* c = text; *c; c++) advance += G<float>(Glyph(font, uint8_t(*c)) + kAdvance);
    return advance * G<float>(font + kWidthScale) * kSize;
}

// x and y in lines of a 480 frame from the output's top left
void DrawText(X86Context& ctx, uint32_t font, const View& view, const char* text, float x, float y, bool shadow) {
    static const uint32_t block = mem::AllocSystem(0x60);
    uint32_t colour = block;
    uint32_t chars = block + 0x10;
    int n = std::min(int(std::strlen(text)), 31);
    for (int i = 0; i <= n; i++) G<uint16_t>(chars + i * 2) = i < n ? uint8_t(text[i]) : 0;
    for (int s = shadow; s >= 0; s--) {
        for (int i = 0; i < 4; i++) G<float>(colour + i * 4) = i == 3 ? 1.0f : s ? 0.0f : kGrey;
        float px = (x + s) * view.unit * view.narrow;
        float py = (y + s) * view.unit;
        CallGuest(ctx, kText, {font, chars, std::bit_cast<uint32_t>(px), std::bit_cast<uint32_t>(py),
                               std::bit_cast<uint32_t>(kSize * view.unit), colour});
    }
}

bool OnTitleMenu() {
    uint32_t ui = R32(kUi);
    if (!ui) return false;
    const char* page = static_cast<const char*>(HostPtr(ui + kPageName));
    return _strnicmp(page, kTitleMenu, sizeof kTitleMenu - 1) == 0;
}

}  // namespace

// the back buffer is the target here, the frame is shown next
void DrawOverlay(X86Context& ctx, uint8_t*) {
    uint32_t font = R32(kFont), viewport = SYM(D3D_Device) + XDK_CDevice_Viewport;
    float w = float(R32(viewport + 8)), h = float(R32(viewport + 12));
    bool fps = game::Opt().fps_counter;
    bool watermark = OnTitleMenu();
    if (!(fps || watermark) || !font || !(h > 0)) return;
    View view{h / 480, w / h / game::Aspect()};
    float& width = G<float>(font + kWidthScale);
    float game_width = width;
    float output_width = game::Aspect() * 480;
    float watermark_x = output_width - kWatermarkRight - TextWidth(font, kWatermark);
    CallGuest(ctx, kBegin, {});
    width = game_width * view.narrow;
    if (fps) {
        char text[32];
        std::snprintf(text, sizeof text, "FPS: %.1f", double(game::g_fps.load()));
        DrawText(ctx, font, view, text, kMargin, kMargin, true);
    }
    if (watermark) DrawText(ctx, font, view, kWatermark, watermark_x, kWatermarkTop, false);
    width = game_width;
    if (R32(kQuads)) CallGuest(ctx, kFlush, {});
    CallGuest(ctx, kEnd, {});
    W32(kTexture, 0);
    CallGuest(ctx, kApplyStates, {});
}
