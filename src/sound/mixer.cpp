// decoding, levels and the mix
#include <chrono>

#include "audio/audio.h"
#include "sound/sound.h"

namespace sound {

const int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int kStep[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
                       31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
                       130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
                       544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
                       2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
                       9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

// one 36 byte per channel IMA block, the voice plays 64 of its 65 frames
void DecodeBlock(const uint8_t* b, int ch, int16_t* out) {
    for (int c = 0; c < ch; c++) {
        int pred = int16_t(b[c * 4] | b[c * 4 + 1] << 8), idx = std::min<int>(b[c * 4 + 2], 88);
        out[c] = int16_t(pred);
        const uint8_t* d = b + ch * 4;
        for (int k = 0; k < 8; k++)
            for (int j = 0; j < 8; j++) {
                int n = (d[(k * ch + c) * 4 + j / 2] >> ((j & 1) * 4)) & 15;
                int step = kStep[idx], diff = step >> 3;
                if (n & 1) diff += step >> 2;
                if (n & 2) diff += step >> 1;
                if (n & 4) diff += step;
                pred = std::clamp(n & 8 ? pred - diff : pred + diff, -32768, 32767);
                idx = std::clamp(idx + kIndex[n], 0, 88);
                out[(1 + k * 8 + j) * ch + c] = int16_t(pred);
            }
    }
}

// sample of channel c at frame f of memory starting at data
float Sample(Voice& v, uint32_t data, uint32_t f, int c) {
    c = std::min<int>(c, v.channels - 1);
    const uint8_t* p = static_cast<const uint8_t*>(HostPtr(data));
    if (v.tag == 0x69) {
        int64_t at = int64_t(data) + int64_t(f / 64) * 36 * v.channels;
        if (at != v.block) {
            DecodeBlock(static_cast<const uint8_t*>(HostPtr(uint32_t(at))), std::min<int>(v.channels, 8), v.decoded);
            v.block = at;
        }
        return v.decoded[(f % 64) * v.channels + c] / 32768.0f;
    }
    if (v.bits == 8) return (p[f * v.align + c] - 128) / 128.0f;
    int16_t s;
    std::memcpy(&s, p + f * v.align + c * 2, 2);
    return s / 32768.0f;
}

void Complete(Voice& v, Packet& pk, uint32_t status) {
    if (pk.completed_ptr) W32(pk.completed_ptr, status == 0 ? pk.size : pk.used);
    if (pk.status_ptr) W32(pk.status_ptr, status);
    if (v.callback) {
        g_done.push_back({v.obj, v.callback, v.context, pk.context, status});
        g_done_cv.notify_one();
    } else if (pk.context) {
        if (ObjectRef e = FromHandle(pk.context)) SetEvent(*e);
    }
}

// Degrees from straight ahead, plus to the right. The library's octant estimate (0x21BC01), not atan2: it picks
// the 3 degree HRTF entry and the interaural delay.
float Azimuth(const float d[3], const float front[3], const float right[3]) {
    float ahead = d[0] * front[0] + d[1] * front[1] + d[2] * front[2];
    float side = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
    float a = std::fabs(ahead);
    float s = std::fabs(side);
    float deg = 0;
    if (a > s) deg = 45 * s / a;
    else if (s > 0) deg = 90 - 45 * a / s;
    if (ahead < 0) deg = 180 - deg;
    return side < 0 ? -deg : deg;
}

// Interaural delay in samples, above 0 the left ear hears later. Black's light HRTF table (0x2233B0) holds it
// in the last byte of the far ear's filter, one entry every 3 degrees.
float Itd(float azimuth) {
    static const uint8_t kDelay[61] = {0,  2,  3,  4,  4,  5,  6,  9,  10, 11, 12, 12, 13, 14, 17, 18,
                                       19, 20, 21, 21, 22, 23, 24, 25, 26, 28, 29, 29, 31, 30, 30, 31,
                                       30, 28, 27, 28, 27, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15,
                                       14, 13, 13, 12, 11, 8,  7,  6,  6,  3,  2,  2,  0};
    if (!(std::fabs(azimuth) <= 180)) return 0;
    int deg = int(std::fabs(azimuth) + 1.5f) / 3 * 3;
    float delay = kDelay[deg / 3];
    return azimuth < 0 ? -delay : delay;
}

// distance attenuation, 1.0 inside the minimum distance
float Attenuation(const Voice& v, float dist) {
    if (dist <= v.min_dist) return 1.0f;
    if (v.curve.empty()) return v.min_dist / std::min(dist, v.max_dist);
    if (v.max_dist <= v.min_dist) return 1.0f;
    // points spread evenly from min to max distance, 1.0 at min
    float pos = (std::min(dist, v.max_dist) - v.min_dist) / (v.max_dist - v.min_dist) * v.curve.size();
    size_t i = std::min(size_t(pos), v.curve.size() - 1);
    float from = i ? v.curve[i - 1] : 1.0f;
    return from + (v.curve[i] - from) * (pos - i);
}

// Where a 3D voice sits for the library's 3D pass (DSOUND 0x219074): distance attenuation and azimuth
Place Locate(const Voice& v) {
    Place at;
    if (!v.is3d || v.mode == 2) return at;
    at.on = true;
    bool rel = v.mode == 1;  // the position is already in listener space
    float d[3] = {v.p[0], v.p[1], v.p[2]};
    for (int i = 0; i < 3 && !rel; i++) d[i] -= g_listener[i];
    at.att = Attenuation(v, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    static const float kHeadFront[3] = {0, 0, 1};
    static const float kHeadRight[3] = {1, 0, 0};
    float right[3] = {g_top[1] * g_front[2] - g_top[2] * g_front[1], g_top[2] * g_front[0] - g_top[0] * g_front[2],
                      g_top[0] * g_front[1] - g_top[1] * g_front[0]};
    float level[3] = {d[0], 0, d[2]};  // the library turns the sound on the horizontal plane only
    at.azimuth = Azimuth(level, rel ? kHeadFront : g_front, rel ? kHeadRight : right);
    return at;
}

// mix bins 6 to 9 are the crosstalk front left, front right, back left and back right
constexpr uint32_t kCrosstalkFrontLeft = 6;
constexpr uint32_t kCrosstalkFrontRight = 7;
constexpr uint32_t kCrosstalkBackLeft = 8;
constexpr uint32_t kCrosstalkBackRight = 9;

// The library's level of a 3D voice in a bin (0x21E94D): the crosstalk pair in front carries it, the pair behind
// is silent on a stereo console, every bin takes the distance attenuation. Left and right come from the HRTF.
float BinScale(const Place& at, uint32_t bin) {
    if (!at.on) return 1.0f;
    if (bin == kCrosstalkBackLeft || bin == kCrosstalkBackRight) return 0.0f;
    return at.att;
}

// Row of a voice channel's level. A mono 3D voice leaves the HRTF as two ears: the left ear feeds the left
// crosstalk bins, the right ear the right ones, every other bin hears the voice as it is.
uint32_t RowOf(const Place& at, const Voice& v, uint32_t c, uint32_t bin) {
    if (!at.on || v.channels != 1) return c;
    if (bin == kCrosstalkFrontLeft || bin == kCrosstalkBackLeft) return kLeftEar;
    if (bin == kCrosstalkFrontRight || bin == kCrosstalkBackRight) return kRightEar;
    return kDry;
}

// a speaker bin on host left and right: even bins left, odd right, 2 the centre on both
void PlaceBin(uint32_t bin, float x, float g[kOuts]) {
    if (bin == 2) x *= 0.707f;
    if (bin == 2 || !(bin & 1)) g[0] = std::max(g[0], x);
    if (bin == 2 || (bin & 1)) g[1] = std::max(g[1], x);
}

// Gain of each row on host left and right and into the reverb.
// Rows are the voice's channels, or the two ears and the voice itself for a mono 3D voice. A hardware voice
// holds two channels and owns a run of slots, which alternate between them.
Gains Levels(const Voice& v, const Place& at) {
    static const uint8_t kFront[2] = {0, 1};
    const uint8_t* bin = v.nbins ? v.bin : kFront;
    uint32_t n = v.nbins ? v.nbins : 2;
    uint32_t voices = (std::max<uint32_t>(v.channels, 1) - 1) / 2 + 1;
    uint32_t per = std::max<uint32_t>(n / voices, 2);
    uint32_t step = v.channels == 1 ? 1 : 2;
    bool speakers = false;
    // muted speaker bins do not count: such a voice is heard through its effect send
    for (uint32_t s = 0; s < n; s++) speakers |= bin[s] < 10 && bin[s] != 3 && v.bin_mb[bin[s]] > -10000;
    Gains g;
    for (uint32_t c = 0; c < v.channels && c < 2; c++) {
        for (uint32_t s = c; s < per && s < n; s += step) {
            uint32_t b = bin[s];
            float x = Db(v.bin_mb[b]) * v.volume * v.headroom * BinScale(at, b);
            uint32_t r = RowOf(at, v, c, b);
            if (kSends >> b & 1) {
                g.send[r] += std::ldexp(x, -g_bin_headroom[b]);
                continue;
            }
            if (speakers && (b == 3 || b > 9)) continue;  // sub and other effect sends
            float* row = g.out[r];
            if (speakers) {
                PlaceBin(b, x, row);
                continue;
            }
            // a voice with no speaker bin plays on both sides
            row[0] = std::max(row[0], x);
            row[1] = std::max(row[1], x);
        }
    }
    // a sound placed at NaN goes silent alone; its NaN gain would silence the whole mix
    for (uint32_t r = 0; r < kRows; r++) {
        for (uint32_t o = 0; o < kOuts; o++)
            if (!std::isfinite(g.out[r][o])) g.out[r][o] = 0;
        if (!std::isfinite(g.send[r])) g.send[r] = 0;
    }
    return g;
}

// 3D levels follow the listener once a game frame: stepped per block, a fast turn clicks
Gains UpdateRamp(Voice& v, const Gains& g, uint32_t frames) {
    if (!v.has_gain) v.gain = g;
    v.has_gain = true;
    Gains step;
    for (uint32_t r = 0; r < kRows; r++) {
        for (uint32_t o = 0; o < kOuts; o++) step.out[r][o] = (g.out[r][o] - v.gain.out[r][o]) / float(frames);
        step.send[r] = (g.send[r] - v.gain.send[r]) / float(frames);
    }
    return step;
}

// the next frame's levels, a step along the ramp
void StepGains(Gains& gain, const Gains& step) {
    for (uint32_t r = 0; r < kRows; r++) {
        for (uint32_t o = 0; o < kOuts; o++) gain.out[r][o] += step.out[r][o];
        gain.send[r] += step.send[r];
    }
}

// the 3D voice's recent samples, the ear further from the sound reads them later
void PushEar(Voice& v, float x) {
    v.ear[v.ear_pos % kEarLength] = x;
    v.ear_pos++;
}
float EarAt(const Voice& v, float delay) {
    float back = std::max(delay, 0.0f);
    uint32_t whole = uint32_t(back);
    float part = back - float(whole);
    float now = v.ear[(v.ear_pos - 1 - whole) % kEarLength];
    float before = v.ear[(v.ear_pos - 2 - whole) % kEarLength];
    return now + (before - now) * part;
}

// The stream's two channels at its position, between this frame and the next. Finished packets complete on the
// way; false once it has run dry.
bool StreamFrame(Voice& v, float s[2]) {
    uint32_t f = uint32_t(v.pos);
    while (!v.packets.empty() && f >= FramesOf(v, v.packets.front().size)) {
        Packet pk = v.packets.front();
        v.packets.pop_front();
        pk.used = pk.size;
        Complete(v, pk, 0);
        v.pos -= FramesOf(v, pk.size);
        f = uint32_t(v.pos);
    }
    if (v.packets.empty()) {
        v.pos = 0;
        if (v.ending) Deactivate(v);
        return false;
    }
    Packet& pk = v.packets.front();
    float t = float(v.pos - f);
    for (int c = 0; c < 2; c++) {
        float now = Sample(v, pk.data, f, c);
        float next = now;
        if (f + 1 < FramesOf(v, pk.size)) next = Sample(v, pk.data, f + 1, c);
        else if (v.packets.size() > 1) next = Sample(v, v.packets[1].data, 0, c);
        s[c] = now + (next - now) * t;
    }
    return true;
}

// The buffer's two channels at its position, looping or stopping at the end of its region; false once stopped.
bool BufferFrame(Voice& v, float s[2]) {
    uint32_t f = uint32_t(v.pos);
    uint32_t start = v.looping ? FramesOf(v, v.loop_start) : 0;
    uint32_t end = FramesOf(v, v.looping ? v.loop_start + v.loop_len : v.play_len);
    if (f >= end) {
        if (!v.looping || end <= start) {
            v.playing = false;
            v.pos = 0;
            return false;
        }
        v.pos = start + std::fmod(v.pos - start, double(end - start));
        f = uint32_t(v.pos);
    }
    if (!v.data) return false;
    uint32_t base = v.data + v.play_start;
    uint32_t next = f + 1 < end ? f + 1 : v.looping ? start : f;
    float t = float(v.pos - f);
    for (int c = 0; c < 2; c++) {
        float now = Sample(v, base, f, c);
        s[c] = now + (Sample(v, base, next, c) - now) * t;
    }
    return true;
}

// one voice's block into the mix and the reverb send
void MixVoice(Voice& v, float* out, float* send, uint32_t frames) {
    Place at = Locate(v);
    Gains g = Levels(v, at);
    bool ears = at.on && v.channels == 1;
    float itd = Itd(at.azimuth);
    if (!v.has_gain) v.itd = itd;
    float itd_step = (itd - v.itd) / float(frames);
    Gains dg = UpdateRamp(v, g, frames);
    double step = double(v.freq ? v.freq : v.rate) / audio::kRate;
    for (uint32_t i = 0; i < frames; i++) {
        float s[kRows];
        if (!(v.stream ? StreamFrame(v, s) : BufferFrame(v, s))) break;
        s[kDry] = s[0];
        if (ears) {
            v.itd += itd_step;
            PushEar(v, s[0]);
            s[kLeftEar] = EarAt(v, v.itd);
            s[kRightEar] = EarAt(v, -v.itd);
        }
        StepGains(v.gain, dg);
        float* frame = out + i * kOuts;
        for (uint32_t r = 0; r < kRows; r++) {
            for (uint32_t o = 0; o < kOuts; o++) frame[o] += s[r] * v.gain.out[r][o];
            send[i] += s[r] * v.gain.send[r];
        }
        v.pos += step;
    }
    v.gain = g;
    v.itd = itd;
}

void Render(float* out, uint32_t frames) {
    std::memset(out, 0, frames * kOuts * sizeof(float));
    static std::vector<float> send;  // the output thread alone renders
    send.assign(frames, 0.0f);
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (auto& [obj, v] : g_voices) {
        if (v.paused || v.sync || (!v.stream && !v.playing)) continue;
        MixVoice(v, out, send.data(), frames);
    }
    g_reverb.Run(send.data(), out, frames);
    for (uint32_t i = 0; i < frames * kOuts; i++) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Run(X86Context& ctx, const std::vector<Done>& done) {
    for (const Done& d : done) CallGuest(ctx, d.callback, {d.context, d.packet_context, d.status});
}

void Start() {
    if (g_started) return;
    g_started = true;
    audio::StartOutput(Render);
    // for a title that never calls DirectSoundDoWork: its callbacks come from here instead
    StartHostThread("dsound", [](X86Context& ctx) {
        constexpr int64_t kPumpMs = 250;
        for (;;) {
            std::vector<Done> done;
            {
                std::unique_lock<std::recursive_mutex> lock(g_lock);
                g_done_cv.wait_for(lock, std::chrono::milliseconds(50));
                if (NowMs() - g_pumped < kPumpMs) continue;
                done.assign(g_done.begin(), g_done.end());
                g_done.clear();
            }
            Run(ctx, done);
        }
    });
}

void ReadFormat(Voice& v, uint32_t wfx) {
    if (!wfx) return;
    v.tag = R16(wfx);
    v.channels = std::max<uint16_t>(R16(wfx + 2), 1);
    v.rate = R32(wfx + 4);
    v.align = std::max<uint16_t>(R16(wfx + 12), 1);
    v.bits = R16(wfx + 14);
    if (v.tag != 1 && v.tag != 0x69) LOG_FIRST(8, "dsound: format tag %X not supported", v.tag);
}

// DSMIXBINS: the voice's bins and their volumes
void SetBins(Voice& v, uint32_t mixbins) {
    v.nbins = 0;  // none listed: front left and right
    if (!mixbins) return;
    uint32_t n = std::min<uint32_t>(R32(mixbins), 8), pairs = R32(mixbins + 4);
    for (uint32_t i = 0; i < n; i++) {
        v.bin[i] = R32(pairs + i * 8) & 31;
        v.bin_mb[v.bin[i]] = int32_t(R32(pairs + i * 8 + 4));
    }
    v.nbins = uint8_t(n);
}
// volumes only, the bins stay
void SetBinVolumes(Voice& v, uint32_t mixbins) {
    if (!mixbins) return;
    uint32_t n = std::min<uint32_t>(R32(mixbins), 32), pairs = R32(mixbins + 4);
    for (uint32_t i = 0; i < n; i++) v.bin_mb[R32(pairs + i * 8) & 31] = int32_t(R32(pairs + i * 8 + 4));
}
// 600 mB unless 3D or a mix in or effect in buffer
float DefaultHeadroom(uint32_t flags) { return flags & 0x382010 ? 1.0f : Db(-600); }

}  // namespace sound
