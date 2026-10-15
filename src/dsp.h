// Reverso - reverse-buffer effect DSP (header-only, no host/CLAP dependencies)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace reverso {

constexpr double kPi = 3.14159265358979323846;

enum ParamId : uint32_t {
    P_MIX = 0, P_SYNC, P_DIV, P_TIME, P_SMOOTH, P_FEEDBACK,
    P_LOWCUT, P_HIGHCUT, P_DIFFUSE, P_WIDTH, P_OUTPUT, P_COUNT
};

struct ParamDef {
    const char* name;
    double min, max, def;
    bool stepped;
};

inline const ParamDef kParams[P_COUNT] = {
    {"Mix",        0.0,   1.0,    0.5,   false},
    {"Tempo Sync", 0.0,   1.0,    1.0,   true},
    {"Division",   0.0,   7.0,    4.0,   true},
    {"Time",       10.0,  4000.0, 500.0, false},
    {"Smooth",     0.0,   1.0,    0.5,   false},
    {"Feedback",   0.0,   0.95,   0.25,  false},
    {"Low Cut",    20.0,  2000.0, 40.0,  false},
    {"High Cut",   1000.0, 20000.0, 12000.0, false},
    {"Diffusion",  0.0,   1.0,    0.2,   false},
    {"Width",      0.0,   2.0,    1.0,   false},
    {"Output",     -24.0, 12.0,   0.0,   false},
};

constexpr int kNumDivisions = 8;
inline const char* const kDivisionNames[kNumDivisions] = {
    "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "2 bars"};
inline const double kDivisionBeats[kNumDivisions] = {
    0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0, 8.0};

struct Params {
    double mix = 0.5, sync = 1.0, div = 4.0, timeMs = 500.0, smooth = 0.5;
    double feedback = 0.25, lowCut = 40.0, highCut = 12000.0;
    double diffuse = 0.2, width = 1.0, outputDb = 0.0;
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setLowpass(double fs, double f, double q = 0.70710678) { set(fs, f, q, false); }
    void setHighpass(double fs, double f, double q = 0.70710678) { set(fs, f, q, true); }
    void reset() { z1 = z2 = 0; }
    float process(float xf) {
        double x = xf;
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float)y;
    }
private:
    void set(double fs, double f, double q, bool hp) {
        f = std::min(std::max(f, 10.0), fs * 0.45);
        double w = 2.0 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        double a0 = 1.0 + al;
        if (hp) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
        else    { b0 = (1 - c) / 2; b1 = 1 - c;    b2 = (1 - c) / 2; }
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
};

struct Allpass {
    std::vector<float> b; int i = 0;
    void init(int n) { b.assign((size_t)std::max(n, 2), 0.f); i = 0; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); i = 0; }
    float process(float x, float g) {
        float d = b[(size_t)i];
        float v = x + g * d;
        b[(size_t)i] = v;
        if (++i >= (int)b.size()) i = 0;
        return d - g * v;
    }
};

struct Smoothed {
    double cur = 0, tgt = 0, coef = 0.001;
    void setTime(double fs, double ms) { coef = 1.0 - std::exp(-1.0 / (fs * ms * 0.001)); }
    void snap(double v) { cur = tgt = v; }
    double next() { cur += (tgt - cur) * coef; return cur; }
};

class Engine {
public:
    static constexpr double kMaxSeconds = 10.0;

    void prepare(double sampleRate) {
        sr_ = sampleRate;
        maxLen_ = (int)(sr_ * kMaxSeconds);
        size_t n = 1;
        while (n < (size_t)maxLen_ * 2 + 16) n <<= 1;
        for (auto& b : buf_) b.assign(n, 0.f);
        mask_ = (int64_t)n - 1;
        static const int dl[4] = {113, 167, 223, 331}, dr[4] = {131, 181, 241, 347};
        for (int k = 0; k < 4; ++k) {
            ap_[0][k].init((int)(dl[k] * sr_ / 44100.0));
            ap_[1][k].init((int)(dr[k] * sr_ / 44100.0));
        }
        for (auto* s : {&mix_, &fb_, &diff_, &width_, &out_}) s->setTime(sr_, 20.0);
        reset();
    }

    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.f);
        for (auto& g : grains_) g.active = false;
        for (auto& c : ap_) for (auto& a : c) a.clear();
        for (auto& c : hp_) c.reset();
        for (auto& c : lp_) c.reset();
        wpos_ = 0; hop_ = 0; first_ = true;
    }

    // Grain length in samples for the current settings.
    int grainLength(const Params& p, double bpm) const {
        double sec;
        if (p.sync >= 0.5 && bpm > 1.0) {
            int d = std::min(std::max((int)std::lround(p.div), 0), kNumDivisions - 1);
            sec = kDivisionBeats[d] * 60.0 / bpm;
        } else {
            sec = p.timeMs * 0.001;
        }
        return std::min(std::max((int)std::lround(sec * sr_), 64), maxLen_);
    }

    void process(const float* inL, const float* inR, float* outL, float* outR,
                 int n, const Params& p, double bpm) {
        mix_.tgt = p.mix; fb_.tgt = p.feedback; diff_.tgt = p.diffuse; width_.tgt = p.width;
        out_.tgt = std::pow(10.0, p.outputDb / 20.0);
        if (first_) {
            mix_.snap(mix_.tgt); fb_.snap(fb_.tgt); diff_.snap(diff_.tgt);
            width_.snap(width_.tgt); out_.snap(out_.tgt); first_ = false;
        }
        if (p.lowCut != lastLow_ || p.highCut != lastHigh_) {
            for (auto& f : hp_) f.setHighpass(sr_, p.lowCut);
            for (auto& f : lp_) f.setLowpass(sr_, p.highCut);
            lastLow_ = p.lowCut; lastHigh_ = p.highCut;
            hpOn_ = p.lowCut > 20.5;      // minimum = filter off
            lpOn_ = p.highCut < 19999.5;  // maximum = filter off
        }
        const int L = grainLength(p, bpm);
        const double fadeFrac = 0.02 + 0.48 * std::min(std::max(p.smooth, 0.0), 1.0);

        for (int i = 0; i < n; ++i) {
            double mix = mix_.next(), fb = fb_.next(), diff = diff_.next();
            double width = width_.next(), og = out_.next();

            if (hop_ <= 0) spawn(L, fadeFrac);
            --hop_;

            float wet[2] = {0.f, 0.f};
            for (auto& g : grains_) {
                if (!g.active) continue;
                float w = windowAt(g);
                int64_t idx = (g.start - g.t) & mask_;
                wet[0] += w * buf_[0][(size_t)idx];
                wet[1] += w * buf_[1][(size_t)idx];
                if (++g.t >= g.len) g.active = false;
            }

            const float in[2] = {inL[i], inR[i]};
            float fbSig[2];
            for (int c = 0; c < 2; ++c) {
                float x = wet[c];
                if (diff > 1e-4) {
                    float y = x;
                    for (auto& a : ap_[c]) y = a.process(y, 0.55f);
                    x = (float)(x * (1.0 - diff) + y * diff);
                }
                if (hpOn_) x = hp_[c].process(x);
                if (lpOn_) x = lp_[c].process(x);
                wet[c] = x;
                fbSig[c] = (float)std::tanh(fb * x);
            }
            // write input + feedback AFTER reading (reads always lag the write head)
            buf_[0][(size_t)(wpos_ & mask_)] = in[0] + fbSig[0];
            buf_[1][(size_t)(wpos_ & mask_)] = in[1] + fbSig[1];
            ++wpos_;

            float m = 0.5f * (wet[0] + wet[1]);
            float s = 0.5f * (wet[0] - wet[1]) * (float)width;
            wet[0] = m + s; wet[1] = m - s;

            double wg = std::sin(mix * kPi * 0.5), dg = std::cos(mix * kPi * 0.5);
            outL[i] = (float)((in[0] * dg + wet[0] * wg) * og);
            outR[i] = (float)((in[1] * dg + wet[1] * wg) * og);
        }
    }

private:
    struct Grain { bool active = false; int64_t start = 0; int t = 0, len = 0, fade = 1; };

    void spawn(int L, double fadeFrac) {
        Grain* slot = nullptr;
        for (auto& g : grains_) if (!g.active) { slot = &g; break; }
        if (!slot) { slot = &grains_[0]; for (auto& g : grains_) if (g.t > slot->t) slot = &g; }
        int fade = std::min(std::max((int)(L * fadeFrac), 1), L / 2);
        slot->active = true; slot->start = wpos_ - 1; slot->t = 0; slot->len = L; slot->fade = fade;
        hop_ = L - fade;
    }

    static float windowAt(const Grain& g) {
        if (g.t < g.fade) return 0.5f * (1.f - (float)std::cos(kPi * g.t / g.fade));
        int rem = g.len - g.t;
        if (rem <= g.fade) return 0.5f * (1.f - (float)std::cos(kPi * rem / g.fade));
        return 1.f;
    }

    double sr_ = 44100.0;
    int maxLen_ = 441000;
    std::vector<float> buf_[2];
    int64_t mask_ = 0, wpos_ = 0;
    int hop_ = 0;
    bool first_ = true;
    Grain grains_[4];
    Allpass ap_[2][4];
    Biquad hp_[2], lp_[2];
    double lastLow_ = -1, lastHigh_ = -1;
    bool hpOn_ = false, lpOn_ = false;
    Smoothed mix_, fb_, diff_, width_, out_;
};

}  // namespace reverso
