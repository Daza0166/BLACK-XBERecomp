// The mouse on the game side: raw counts turn the view in the player's look update.
#include <cmath>

#include "common/mouse_keyboard.h"
#include "xdk/hle.h"

using namespace kernel;

// degrees a count at sensitivity 1
constexpr float kDegrees = 0.05f;

// esi the player's controller: +0x18 yaw (positive left), +0x1C pitch (positive down), degrees; xmm2 the zoom scale.
// The game adds the stick, clamps the pitch and wraps the yaw after this.
void MouseLook(X86Context& ctx, uint8_t*) {
    float dx, dy;
    if (!input::TakeMouse(dx, dy)) return;
    float scale = kDegrees * ctx.xmm[2].f[0];
    G<float>(ctx.esi + 0x18) -= std::remainder(dx * scale, 360.0f);
    G<float>(ctx.esi + 0x1C) += dy * scale;
}
