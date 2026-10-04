// Hooks for default.xbe
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "app/app.h"
#include "common/options.h"
#include "core/input.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

namespace {

constexpr uint32_t kDevice = SYM(D3D_Device);
constexpr uint32_t kInitDefaultState = 0x271710;

void WriteFloat(uint32_t va, float f) { std::memcpy(HostPtr(va), &f, 4); }

// what the library's vertical blank interrupt does that the game sees: count, event, callback
void VerticalBlank(X86Context& ctx) {
    uint32_t info = mem::AllocSystem(12);
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        next += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(next);
        uint32_t count = R32(kDevice + XDK_CDevice_VBlankCount) + 1;
        W32(kDevice + XDK_CDevice_VBlankCount, count);
        ObjectRef event = FromPointer(kDevice + XDK_CDevice_VBlankEvent);
        SetEvent(*event);
        if (uint32_t callback = R32(kDevice + XDK_CDevice_VBlankCallback)) {
            W32(info, count);
            W32(info + 4, R32(kDevice + XDK_CDevice_SwapCount));
            W32(info + 8, 0);
            CallGuest(ctx, callback, {info});
        }
        // the game clears the event itself before each wait, which the host object does not see
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ResetEvent(*event);
    }
}

}  // namespace

// the viewport as D3DDevice_SetViewport stores it, for the game's own readers
void StoreViewport(X86Context&, uint8_t*) {
    std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
    const gpu::Viewport& v = d3d::State().viewport;
    uint32_t p = kDevice + XDK_CDevice_Viewport;
    W32(p, v.x);
    W32(p + 4, v.y);
    W32(p + 8, v.w);
    W32(p + 12, v.h);
    WriteFloat(p + 16, v.min_z);
    WriteFloat(p + 20, v.max_z);
}

// D3DDevice_GetViewportOffsetAndScale
X86_EXTERN_FUNC(__imp__sub_0026E9C0);
X86_FUNC(sub_0026E9C0) {
    StoreViewport(ctx, base);
    __imp__sub_0026E9C0(ctx, base);
}

// device fields the library's init sets and the game or its inlined library code reads, then the default states
void AfterCreateDevice(X86Context& ctx, uint8_t* base) {
    if (ctx.eax) return;
    W32(kDevice + XDK_CDevice_VertexShader, SYM(D3D_FvfShader));
    W32(kDevice + XDK_CDevice_AutoDepth, R32(kDevice + XDK_CDevice_Depth));
    WriteFloat(kDevice + XDK_CDevice_BackBufferScale, 1.0f);
    WriteFloat(kDevice + XDK_CDevice_BackBufferScale + 4, 1.0f);
    W32(kDevice + XDK_CDevice_DisplayFlags, 0x400000 | (app::Widescreen() ? 0x10000 : 0));  // 60 Hz, widescreen
    W32(kDevice + XDK_CDevice_VBlankEvent, 0x00040000);  // notification event
    W32(kDevice + XDK_CDevice_VBlankEvent + 4, 1);
    StoreViewport(ctx, base);
    uint32_t ecx = ctx.ecx, edx = ctx.edx;
    CallGuest(ctx, kInitDefaultState, {});
    ctx.eax = 0;
    ctx.ecx = ecx;
    ctx.edx = edx;
    static bool started = false;
    if (!started) StartHostThread("vertical blank", VerticalBlank);
    started = true;
}

// seconds the last frame took, the frame time the game was made for at most
static float FrameTime() {
    using clock = std::chrono::steady_clock;
    static clock::time_point last;
    static uint32_t frame;
    static float dt = G<float>(0x2F3A38);
    if (uint32_t f = R32(0x2F4368); f != frame) {
        auto now = clock::now();
        dt = std::min(std::chrono::duration<float>(now - last).count(), G<float>(0x2F3A38));
        frame = f;
        last = now;
    }
    return dt;
}

// The game's timers step one fixed frame time a frame: they step the real one, so any frame rate plays at speed
X86_EXTERN_FUNC(__imp__sub_000BE3F0);
X86_FUNC(sub_000BE3F0) {
    uint32_t t = ctx.ecx;
    auto steps = [t] { return R32(t + 0xC) + double(R32(t + 0x10)) / std::max(R32(t + 0x18), 1u); };
    double before = steps();
    float time = G<float>(t + 0x20);
    __imp__sub_000BE3F0(ctx, base);
    float dt = FrameTime();
    G<float>(t + 0x1C) = dt / std::max(R32(t + 0x18), 1u);
    G<float>(t + 0x20) = time + float((steps() - before) * dt);
}

// The Flash menus and HUD tick in milliseconds, 33 a call: the real ones, the part of one carried over
void MenuTick(X86Context& ctx, uint8_t*) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last;
    static float carry;
    auto now = clock::now();
    carry += std::min(std::chrono::duration<float>(now - last).count(), G<float>(0x2F3A38)) * 1000.0f;
    last = now;
    ctx.eax = uint32_t(carry);
    carry -= float(ctx.eax);
}

// what a step made once a frame at 30 becomes over the real frame: factor to the power of this
static float FrameShare() { return FrameTime() / G<float>(0x2F3A38); }

// The zoom of the aim grows or shrinks by a fixed share a frame
void ZoomStep(X86Context& ctx, uint8_t*) {
    float old = G<float>(R32(ctx.ecx + 0xF0) + 0x2A4), now = ctx.xmm[0].f[0];
    if (now == old || old <= 0) return;
    bool out = !G<uint8_t>(ctx.ecx + 0x105) && old > 1;  // back towards 1
    ctx.xmm[0].f[0] = out ? 1 + (old - 1) * std::pow((now - 1) / (old - 1), FrameShare())
                          : old * std::pow(now / old, FrameShare());
}

constexpr float kHalf = 3.14159265f / 360.0f;

// tangent of a setting's half angle over that of the game's 70 degrees
static float Widen(uint32_t degrees) { return std::tan(std::clamp(degrees, 45u, 140u) * kHalf) / std::tan(70 * kHalf); }

// The view of play, 70 over the zoom: the setting from the hip, the game's own once the aim is in.
// ecx the weapon, xmm0 its zoom. Scripted cameras write theirs after this
void FieldOfView(X86Context& ctx, uint8_t*) {
    uint32_t data = R32(ctx.ecx + 0xEC);
    float aim = data ? G<float>(data + 0xB4) : 1, zoom = ctx.xmm[0].f[0];
    float hip = aim > 1 ? std::clamp((aim - zoom) / (aim - 1), 0.0f, 1.0f) : 1.0f;
    float view = 1 + (Widen(game::Opt().fov) - 1) * hip;
    float& fov = ctx.xmm[1].f[0];
    if (view != 1) fov = std::atan(std::tan(fov * kHalf) * view) / kHalf;
}

// recoil dies away by a factor a frame
void RecoilDecay(X86Context& ctx, uint8_t*) { ctx.xmm[0].f[0] = std::pow(ctx.xmm[0].f[0], FrameShare()); }

// The gun trails the stick by two shares a frame, read from the weapon's data: scaled for the four blends
static uint32_t g_sway;
static float g_sway_share[2];
void SwayBegin(X86Context& ctx, uint8_t*) {
    g_sway = ctx.eax + 0x104;
    for (int i = 0; i < 2; i++) {
        float& k = G<float>(g_sway + i * 4);
        g_sway_share[i] = k;
        k = 1 - std::pow(1 - k, FrameShare());
    }
}
void SwayEnd(X86Context&, uint8_t*) {
    if (!g_sway) return;
    for (int i = 0; i < 2; i++) G<float>(g_sway + i * 4) = g_sway_share[i];
    g_sway = 0;
}

// The physics world steps once a frame by the frame time of the level's start and bodies hold their move a step:
// both follow the real frame time, before the characters write theirs
static bool g_rest_tick;
void PhysicsTime(X86Context&, uint8_t*) {
    uint32_t physics = R32(0x2D45F4), solver = physics ? R32(physics) : 0, world = solver ? R32(solver + 0x18) : 0;
    float dt = FrameTime(), share = FrameShare();
    if (!world || !(dt > 0)) return;
    float& step = G<float>(world + 0x380);
    if (step > 0 && step != dt) {
        static const uint32_t kMove[] = {0x20, 0x24, 0x28, 0x30, 0x34, 0x38}, kGravity[] = {0x90, 0x94, 0x98};
        float by = dt / step;
        uint32_t body = R32(world + 0x29C);
        for (int left = 4096; body && body != world + 0x270 && left; left--, body = R32(body + 0x2C)) {
            for (uint32_t at : kMove) G<float>(body + at) *= by;
            for (uint32_t at : kGravity) G<float>(body + at) *= by * by;
        }
    }
    step = dt;
    G<float>(world + 0x384) = 1 / dt;
    for (int i = 0; i < 16; i += 4) G<float>(world + 0x370 + i) = G<float>(world + 0x360 + i) * dt * dt;  // gravity
    // a body rests once its move a step stays under a limit for a count of steps: the limit shrinks with the
    // step, the count runs at the game's frame rate
    static float limit, written, ticks;
    float& now = G<float>(world + 0x390);
    if (now != written) limit = now;
    now = written = limit * share * share;
    ticks += share;
    g_rest_tick = ticks >= 1;
    if (g_rest_tick) ticks -= 1;
}
bool BodyRestTick(X86Context&, uint8_t*) { return g_rest_tick; }

// a body's damping is a factor a step, its top speed a move a step
void BodyDamping(X86Context& ctx, uint8_t*) {
    for (int i = 0; i < 2; i++)
        if (float& keep = ctx.xmm[i].f[0]; keep > 0) keep = std::pow(keep, FrameShare());
}
void BodyTopSpeed(X86Context& ctx, uint8_t*) { ctx.xmm[1].f[0] *= FrameShare(); }

// reload animations start the screen blur
bool ReloadBlur(X86Context&, uint8_t*) { return game::Opt().reload_blur; }

// A or Start on any pad, on the press
static bool SkipPressed() {
    static bool held = true;
    bool down = false;
    for (int port = 0; port < 4; port++) {
        input::Pad pad = input::Poll(port);
        down |= pad.analog[0] || (pad.buttons & 0x10);
    }
    bool pressed = down && !held;
    held = down;
    return pressed;
}

// The film player, one for every film: a film ends on a press unless the film table marks it as menu background,
// the logos (LO_) and the film after pressing start (02_) as they begin when intros are skipped
void SkipVideo(X86Context&, uint8_t*) {
    bool pressed = SkipPressed();
    uint32_t frontend = R32(0x2D45F0) + 0x20220, player = frontend + 0x4C;
    uint32_t film = R32(frontend + 0x20C), state = R32(player + 0x1A8);
    bool background = film < 27 && G<uint8_t>(0x29D9C8 + film * 12);
    if (pressed) LOGI("film %u: press in state %X, flags %X", film, state, R32(player + 0x198));
    // a film keeps the frame time of its start: its frame length, and its clock's step
    if (uint32_t movie = R32(player); movie && state >= 4 && state <= 0x1C) {
        G<float>(movie + 0x54) = G<float>(0x2F3A38);
        G<float>(movie + 0x5C) = G<float>(R32(0x2D45F0) + 0x2013C);
    }
    if (background || state < 2 || state > 0x1C) return;
    std::string name = ReadCString(player + 0x84, 8);
    bool intro = name.rfind("LO_", 0) == 0 || name.rfind("02_", 0) == 0;
    if (pressed || (game::Opt().skip_intros && intro)) G<uint8_t>(player + 0x1B9) = 1;
}
