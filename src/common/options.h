#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <string>

// the game's own settings, in the settings file through app::kGame.options
namespace game {

struct Options {
    bool skip_intros = false;  // logos at boot and the film after pressing start
    bool fps_counter = false;
    bool hdr_output = true;  // HDR10 on a display with Windows HDR on
    bool reload_blur = true;
    uint32_t ambient_occlusion = 0;  // screen space, from the scene's depth: 0 off, 1 SSAO, 2 GTAO
    uint32_t msaa = 0;  // samples a pixel of the scene, 0 off
    uint32_t draw_distance = 1;  // times the game's LOD and draw distances
    float mouse_sensitivity = 1.0f;
    bool invert_mouse = false;
    bool hold_aim = true;  // scopes too, keyboard and mouse
    uint32_t fov = 70;  // horizontal degrees on a 4:3 frame, 70 is the game's
    uint32_t viewmodel_fov = 70;  // the gun and arms, the same measure
    uint32_t walk_speed = 50;  // percent of running, while the walk key is held
    uint32_t audio_output = 0;  // audio::kOutputAuto, kOutputStereo, kOutputSurround or kOutputMono
    std::map<std::string, std::string> keys;  // action id to key names, absent is the default
};
Options& Opt();
inline std::atomic<float> g_fps{0};  // from the host, for the game's own counter
inline std::atomic<bool> g_hdr_display{false};  // the window's display has Windows HDR on, from the renderer

}  // namespace game
