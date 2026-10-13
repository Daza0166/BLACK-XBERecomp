// I3DL2 reverb, ported from the Half-Life 2 recomp: the XDK effect is the same DSP code in both effects images
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/output.h"

namespace sound {

// The XDK's I3DL2 reverb as its DSP program runs it: a 24 kHz core, delays in samples at that rate
struct Reverb {
    struct Pole {
        float a = 0, b = 0, z = 0;
        float Run(float x) { return z = a * z + b * x + 1e-20f; }  // the offset keeps silence out of denormals
    };
    struct Line {
        std::vector<float> s;
        uint32_t w = 0;
        // a delay of the whole line reads its oldest sample
        float At(uint32_t delay) const { return s[(w + s.size() - delay % s.size()) % s.size()]; }
        void Put(float x) {
            s[w] = x;
            w = (w + 1) % uint32_t(s.size());
        }
    };
    struct Params {
        uint32_t refl, tap[4], in[4], chain[8], comb[4], echo;
        float early[4], late, diff[8], rest, cross;
        Pole in_lp, chain_lp[3], loop_lp[4], echo_lp[2];
    };
    static constexpr int kLen[14] = {6000, 6000, 6000, 6000, 680, 612, 640, 720, 1392, 1684, 2044, 2476, 24000, 24000};
    Line line[14];
    Params now, next;
    bool on = false, pending = false;
    float fade = 1, held[4] = {};
    uint32_t tick = 0;

    Reverb() {
        for (int i = 0; i < 14; i++) line[i].s.assign(kLen[i], 0.0f);
    }
    static float Lin(float mb) { return std::pow(10.0f, mb / 2000.0f); }
    // y = a * y1 + b * x: mb millibels at 0 Hz, hf more at ref Hz
    static Pole Design(int mb, int hf, float ref, float rate) {
        Pole p;
        if (mb <= -10000) return p;
        // 1.0 is past the DSP's fractions and comes out as -1
        p.b = mb ? Lin(float(mb)) : -1;
        if (hf >= 0) return p;
        p.b = Lin(float(mb));
        float g = Lin(2.0f * hf), c = std::cos(6.2831853f * ref / rate);
        p.a = (1 - g * c - std::sqrt(2 * g * (1 - c) - g * g * (1 - c * c))) / (1 - g);
        p.b *= 1 - p.a;
        return p;
    }
    // DSI3DL2LISTENER as 12 floats, the library's own conversion
    void Set(const float* p) {
        static const float kGain[4] = {0.57f, 0.52f, 0.54f, 0.6f}, kTap[4] = {0.1052f, 0.0673f, 0.1737f, 0.2404f};
        static const float kComb[4] = {0.2469f, 0.2209f, 0.2311f, 0.2608f};
        static const float kCombBase[4] = {0.003651f, 0.003515f, 0.003673f, 0.003946f};
        static const float kChain[8] = {0.040023f, 0.288186f, 0.603764f, 0.851905f,
                                        0.021224f, 0.37542f,  0.623583f, 0.759229f};
        static const float kSign[8] = {1, -1, -1, 1, 1, 1, -1, -1};
        static const int kInput[4] = {0, 58, 134, 232};
        auto al = [](int n) { return (n + 1) & ~3; };
        // taps are read in blocks of 4 samples, 16 back at least
        auto tap = [](int n) { return uint32_t(std::max((n + 3) & ~3, 16)); };
        float decay = std::max(p[3], 0.1f), ratio = std::max(p[4], 0.1f), d = std::clamp(p[9] * 0.01f, 0.0f, 1.0f);
        float sc = std::clamp((2 - p[10] * 0.01f) * (decay - 1) * 5, 1.0f, 60.0f);
        float f = sc >= 2.5f ? 1.0f : (sc - 0.8f) * 0.5882353f;
        // a loop of n samples loses 60 dB in the decay time, high frequencies sooner by the ratio
        auto loop = [&](int n) {
            float x = n / 24000.0f / decay * -6000.0f;
            int lf = int(x), hf = int(ratio < 1 ? x / ratio : x);
            return Design(lf, hf - lf, p[11], 24000);
        };
        Params q;
        q.in_lp = Design(0, int(p[1]), p[11], 48000);
        int refl = al(int(p[6] * 24000)), rev = int(p[8] * 24000), echo = int(f * 24000), prev = 0;
        q.refl = tap(refl);
        q.late = Lin(p[7] + p[0]);
        for (int k = 0; k < 4; k++) {
            q.early[k] = Lin(p[5] + p[0]) * kGain[k];
            q.tap[k] = tap(refl + al(int(rev * kTap[k])));
            q.in[k] = tap(refl + al(rev) + kInput[k]);
            q.comb[k] = tap(std::min(al(int((p[8] * kComb[k] + kCombBase[k]) * 24000)), kLen[4 + k] - 1));
            q.loop_lp[k] = loop(kLen[8 + k]);
            // echo times count from the input, a stage of the chain holds 250 ms
            int a = al(int(f * kChain[k] * 24000)), b = al(int(f * kChain[4 + k] * 24000)), m = std::max(a, b) - prev;
            float r = m > 5999 ? 5999.0f / m : 0;
            q.chain[k * 2] = tap(r ? al(int((a - prev) * r)) : a - prev);
            q.chain[k * 2 + 1] = tap(r ? al(int((b - prev) * r)) : b - prev);
            prev = b;
        }
        q.echo = tap(std::min(al(echo), 23999));
        for (int k = 0; k < 3; k++) q.chain_lp[k] = loop(echo >> 2);
        q.echo_lp[0] = q.echo_lp[1] = loop(echo);
        for (int k = 0; k < 8; k++) q.diff[k] = d * kSign[k];
        q.rest = d > 0 ? 1 - d : -1;
        q.cross = 0.2f * d;
        // a change fades out, swaps, fades in
        (on ? next : now) = q;
        pending = on;
        on = true;
    }
    // one 24 kHz sample in, front left, front right, back left, back right out
    void Frame(float x, float o[4]) {
        static const float kFeed[4] = {-0.4f, 0.4f, -0.4f, -0.4f};
        Params& q = now;
        Line* l = line;
        float t = l[0].At(q.refl), e[4], in[4], c[8], u[4], sum = 0;
        for (int k = 0; k < 4; k++) {
            e[k] = t + l[0].At(q.tap[k]) + kFeed[k] * l[4 + k].At(q.comb[k]);
            in[k] = l[0].At(q.in[k]);
            c[k * 2] = l[k].At(q.chain[k * 2]);
            c[k * 2 + 1] = l[k].At(q.chain[k * 2 + 1]);
        }
        // the echoes, front and back
        float sa = c[1] + c[2] + c[4] + c[7], sb = c[0] + c[3] + c[5] + c[6];
        float ea = q.rest * sa + l[12].At(q.echo), eb = q.rest * sb + l[13].At(q.echo);
        for (int j = 0; j < 4; j++)
            sum += u[j] = l[8 + j].At(0) + q.diff[j * 2] * in[j & 1 ? 2 : 0] + q.diff[j * 2 + 1] * in[j & 1 ? 3 : 1];
        for (int j = 0; j < 4; j++) {
            o[j] = q.early[j] * e[j] + q.late * u[j] + q.late * 0.5f * (j < 2 ? ea : eb);
            l[4 + j].Put(e[j]);
            l[8 + j].Put(q.loop_lp[j].Run(u[j] - 0.5f * sum + q.cross * (sa + sb)));
        }
        l[0].Put(x);
        for (int k = 0; k < 3; k++) l[k + 1].Put(q.chain_lp[k].Run(c[k * 2 + 1]));
        l[12].Put(q.echo_lp[0].Run(ea));
        l[13].Put(q.echo_lp[1].Run(eb));
    }
    // adds the reverb of the 48 kHz send to the front and back pairs
    void Run(const float* send, float* out, uint32_t frames) {
        static const audio::Speaker kOut[4] = {audio::FrontLeft, audio::FrontRight, audio::BackLeft, audio::BackRight};
        for (uint32_t i = 0; on && i < frames; i++) {
            float x = now.in_lp.Run(send[i] * 0.5f), o[4];
            if (!(tick++ & 1)) {
                if (pending && (fade -= 1 / 128.0f) <= 0) {
                    now = next;
                    pending = false;
                    fade = 0;
                } else if (!pending) {
                    fade = std::min(fade + 1 / 128.0f, 1.0f);
                }
                Frame(x, o);
                for (int k = 0; k < 4; k++) held[k] = o[k] * fade;
            }
            for (int k = 0; k < 4; k++) out[i * audio::kChannels + kOut[k]] += held[k];
        }
    }
};
inline Reverb g_reverb;

}  // namespace sound
