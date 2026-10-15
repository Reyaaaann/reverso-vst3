// Plain-C++ sanity tests for the Reverso DSP engine (no test framework needed).
#include "../src/dsp.h"
#include <cstdio>
#include <vector>

using namespace reverso;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } else std::printf("ok:   %s\n", msg); } while (0)

static std::vector<float> run(Engine& e, const std::vector<float>& in, const Params& p, double bpm, int block = 256) {
    std::vector<float> out(in.size()), outR(in.size());
    for (size_t i = 0; i < in.size(); i += block) {
        int n = (int)std::min<size_t>(block, in.size() - i);
        e.process(&in[i], &in[i], &out[i], &outR[i], n, p, bpm);
    }
    return out;
}

int main() {
    const double sr = 48000.0;
    Params p;
    p.sync = 0; p.timeMs = 100; p.mix = 1.0; p.feedback = 0; p.diffuse = 0;
    p.lowCut = 20; p.highCut = 20000; p.width = 1; p.outputDb = 0;

    // 1) Overlap-add of windows must sum to unity: constant input -> constant output.
    for (double smooth : {0.0, 0.5, 1.0}) {
        Engine e; e.prepare(sr); p.smooth = smooth;
        std::vector<float> in((size_t)(sr * 1.0), 1.0f);
        auto out = run(e, in, p, 120);
        float lo = 10, hi = -10;
        for (size_t i = (size_t)(sr * 0.5); i < out.size(); ++i) { lo = std::min(lo, out[i]); hi = std::max(hi, out[i]); }
        std::printf("      smooth=%.1f steady-state range [%.5f, %.5f]\n", smooth, lo, hi);
        CHECK(lo > 0.97f && hi < 1.03f, "windows crossfade to ~unity gain (HP/LP settle included)");
    }

    // 2) A rising ramp must come out falling (i.e. actually reversed) inside a grain.
    {
        Engine e; e.prepare(sr); p.smooth = 0.0; p.timeMs = 100; // L=4800
        std::vector<float> in((size_t)sr);
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float)i / (float)in.size();
        auto out = run(e, in, p, 120);
        // pick a window well inside a plateau, after the first few grains
        size_t a = 4800 * 4 + 1000, b = a + 1000;
        int falling = 0;
        for (size_t i = a + 1; i < b; ++i) if (out[i] < out[i - 1]) ++falling;
        CHECK(falling > 990, "ramp input plays backwards (output falls while input rises)");
    }

    // 3) Tempo sync: 1/4 note at 120 BPM = 500 ms = 24000 samples.
    {
        Engine e; e.prepare(sr); Params q; q.sync = 1; q.div = 4;
        CHECK(e.grainLength(q, 120.0) == 24000, "1/4 @120 BPM -> 24000 samples");
        q.div = 6;
        CHECK(e.grainLength(q, 120.0) == 96000, "1 bar @120 BPM -> 96000 samples");
        q.sync = 0; q.timeMs = 250;
        CHECK(e.grainLength(q, 120.0) == 12000, "free mode ignores tempo");
    }

    // 4) Stability: max feedback with hot noise must stay finite and bounded.
    {
        Engine e; e.prepare(sr); Params q; q.sync = 0; q.timeMs = 80; q.feedback = 0.95; q.mix = 0.5;
        std::vector<float> in((size_t)(sr * 5));
        unsigned s = 1;
        for (auto& v : in) { s = s * 1664525u + 1013904223u; v = ((s >> 8) / 8388608.0f - 1.f); }
        auto out = run(e, in, q, 0);
        bool ok = true; float mx = 0;
        for (float v : out) { if (!std::isfinite(v)) ok = false; mx = std::max(mx, std::fabs(v)); }
        std::printf("      peak with fb=0.95: %.3f\n", mx);
        CHECK(ok && mx < 8.0f, "feedback 0.95 stays finite and bounded");
    }

    // 5) Dry path at mix=0 is transparent.
    {
        Engine e; e.prepare(sr); Params q; q.mix = 0.0; q.sync = 0;
        std::vector<float> in((size_t)sr);
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float)std::sin(2 * kPi * 440 * i / sr);
        auto out = run(e, in, q, 0);
        float err = 0; for (size_t i = 0; i < in.size(); ++i) err = std::max(err, std::fabs(out[i] - in[i]));
        CHECK(err < 1e-5f, "mix=0 passes dry signal unchanged");
    }

    // 6) Block-size independence (host may use any buffer size).
    {
        Params q; q.sync = 0; q.timeMs = 120; q.mix = 0.7; q.feedback = 0.4; q.diffuse = 0.5;
        std::vector<float> in((size_t)(sr * 1.5));
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float)std::sin(2 * kPi * 220 * i / sr) * ((i % 24000) < 2000 ? 1.f : 0.f);
        Engine a, b; a.prepare(sr); b.prepare(sr);
        auto o1 = run(a, in, q, 0, 64), o2 = run(b, in, q, 0, 1000);
        float d = 0; for (size_t i = 0; i < in.size(); ++i) d = std::max(d, std::fabs(o1[i] - o2[i]));
        CHECK(d < 1e-6f, "output identical for 64- and 1000-sample blocks");
    }

    std::printf(failures ? "\n%d test(s) FAILED\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
