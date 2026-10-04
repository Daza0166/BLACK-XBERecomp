// DirectSound (XDK 5849) on a host software mixer. Buffers and streams are read from guest memory as they play.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

#include "xdk/hle.h"

namespace sound {
using namespace kernel;

struct Packet {
    uint32_t data, size, completed_ptr, status_ptr, context;
    uint32_t used = 0;
};

struct Voice {
    uint32_t obj = 0;
    bool stream = false;
    int refs = 1;
    // format
    uint16_t tag = 1, channels = 1, bits = 16, align = 2;
    uint32_t rate = 48000;
    // buffer
    // play region in bytes from data, loop region in bytes from the play region
    uint32_t data = 0, bytes = 0, loop_start = 0, loop_len = 0, play_start = 0, play_len = 0;
    bool playing = false, looping = false, paused = false;
    bool sync = false;  // held until IDirectSound_SynchPlayback
    double pos = 0;  // frames
    uint32_t freq = 0;
    // stream
    uint32_t max_packets = 1, callback = 0, context = 0;
    std::deque<Packet> packets;
    // levels
    float volume = 1, headroom = 1, gain_l = 1, gain_r = 1;
    bool is3d = false;
    uint32_t mode = 0;  // DS3DMODE: 1 head relative, 2 disabled
    float p[3] = {}, min_dist = 1, max_dist = 1e9f;
    // ADPCM block cache
    int64_t block = -1;
    int16_t decoded[65 * 8];
};

struct Done {
    uint32_t voice, callback, context, packet_context, status;
};

inline std::recursive_mutex g_lock;
inline std::map<uint32_t, Voice> g_voices;
inline float g_listener[3] = {}, g_front[3] = {0, 0, 1}, g_top[3] = {0, 1, 0};
// Completed packets' callbacks, run as the XDK runs them: in DirectSoundDoWork on the title's thread, so they never
// overlap the title's own stream code. A stream flushed or released runs its own at once, before it is gone.
inline std::deque<Done> g_done;
inline std::condition_variable_any g_done_cv;
inline std::atomic<int64_t> g_pumped{0};          // the last DirectSoundDoWork, steady ms
inline bool g_started = false;
inline uint32_t g_ds = 0;

inline float Db(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : std::pow(10.0f, hundredths / 2000.0f); }

inline uint32_t FramesOf(const Voice& v, uint32_t bytes) {
    if (v.tag == 0x69) return bytes / (36u * v.channels) * 64;
    return bytes / std::max<uint32_t>(v.align, 1);
}
inline uint32_t BytesOf(const Voice& v, uint32_t frames) {
    if (v.tag == 0x69) return frames / 64 * 36 * v.channels;
    return frames * v.align;
}

inline Voice* Find(uint32_t obj) {
    auto it = g_voices.find(obj);
    return it == g_voices.end() ? nullptr : &it->second;
}

inline float F32(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void Start();
void Complete(Voice& v, Packet& pk, uint32_t status);
void Run(X86Context& ctx, const std::vector<Done>& done);
int64_t NowMs();
void ReadFormat(Voice& v, uint32_t wfx);
void MixBins(Voice& v, uint32_t mixbins);
float DefaultHeadroom(uint32_t flags);

}  // namespace sound
