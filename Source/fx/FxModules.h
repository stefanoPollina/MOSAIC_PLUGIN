#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "../dsp/MosaicFx.h"

/* Mosaic FX Suite only (the Mosaic synth never includes this file).

   FxPre: the four tempo / level driven effects that in the Mosaic synth live on its voice bus, ahead of the FX bus:
   SIDECHAIN (the 1/4-note pump), RESO COMB, ENVELOPE FILTER and PSY FILTER GATE. Same maths and constants as
   MosaicVoice.cpp; here they run on the track's audio, clocked by the DAW's beat position.

   FxLfos: three tempo-synced LFOs (with a 16-step STEPS shape, LFO SEQ style) that move FX controls. They work on
   the settings, not the audio: once per 128 samples the moved settings go to the FX engine, which glides to them. */
namespace mosaicfx
{
inline double clamp01 (double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

struct PreParams
{
    bool duck = false; double duckDepth = 0, duckRel = 0.55, duckShape = 0.35;
    bool comb = false; double combTune = 0.35, combFb = 0.55, combMix = 0.35;
    bool env = false; double envSens = 0.65, envRes = 0.48, envRange = 0.55;
    bool fg = false; int fgSteps[16] {}; double fgCut = 0.42, fgRes = 0.35, fgDepth = 0.85;
    bool any() const { return duck || comb || env || fg; }
};

class FxPre
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate; for (auto& b : cbBuf) b.assign (4096, 0.f); cbW = 0; cbLp[0] = cbLp[1] = 0;
        efInit = fgInit = false;
    }

    // beat0: the DAW's beat (quarter-note) position at the first sample; bps: beats per sample
    void process (float* L, float* R, int n, double beat0, double bps, const PreParams& P)
    {
        if (! P.any()) { efInit = fgInit = false; return; }
        const double duck = P.duck && P.duckDepth > 0.001 ? std::min (1.0, P.duckDepth) : 0.0;
        const double duckRel = std::min (0.96, std::max (0.12, P.duckRel > 0 ? P.duckRel : 0.55));
        const double duckExp = 0.25 + clamp01 (P.duckShape) * 1.75;
        double cbDL = 0, cbDR = 0, cbFb = 0, cbMix = 0, cbA = 0;
        if (P.comb) { const double hz = 55 + std::pow (clamp01 (P.combTune), 1.8) * 650; cbDL = sr / hz; cbDR = sr / (hz * 1.015);
                      cbFb = clamp01 (P.combFb) * 0.88; cbMix = clamp01 (P.combMix); cbA = 1 - std::exp (-2 * 3.141592653589793 * 2400 / sr); }
        if (P.env && ! efInit) { efInit = true; efE = 0; efN = 0; for (auto& z : efZ) z = 0; }
        if (! P.env) efInit = false;
        const double efAtt = 1 - std::exp (-1 / (0.005 * sr)), efRel = 1 - std::exp (-1 / (0.12 * sr));
        double efGain = 0, efSpan = 0, efQ = 0.8;
        if (P.env) { efGain = 1.2 + clamp01 (P.envSens) * 5; efSpan = 400 + clamp01 (P.envRange) * 2600; efQ = 0.8 + clamp01 (P.envRes) * 3.2; }
        if (P.fg && ! fgInit) { fgInit = true; fgF = 1000; fgG = 1; fgN = 0; for (auto& z : fgZ) z = 0; }
        if (! P.fg) fgInit = false;
        const double fgKf = 1 - std::exp (-1 / (0.008 * sr)), fgKg = 1 - std::exp (-1 / (0.006 * sr));
        double fgOpen = 0, fgClosed = 0, fgLow = 1, fgQ = 0.70710678;
        if (P.fg) { const double cut = clamp01 (P.fgCut), dep = clamp01 (P.fgDepth), base = 180 + std::pow (cut, 1.8) * 3800;
                    fgOpen = std::min (4800.0, base * (1.2 + dep * 2.4)); fgClosed = std::max (110.0, base * (1 - dep * 0.72)); fgLow = std::max (0.05, 1 - dep * 0.92);
                    fgQ = 0.70710678 + clamp01 (P.fgRes) * 2.5; }

        for (int i = 0; i < n; ++i)
        {
            const double beat = beat0 + i * bps;
            double busL = L[i], busR = R[i];
            if (duck > 0)
            {
                const double bp = beat - std::floor (beat);
                const double g = 1 - duck * (1 - (bp < 0.03 ? 1 - bp / 0.03 : std::pow (std::min (1.0, (bp - 0.03) / duckRel), duckExp)));
                busL *= g; busR *= g;
            }
            if (P.comb)
            {
                auto rd = [this] (int c, double d) { double r = cbW - d; if (r < 0) r += 4096; const int i0 = (int) r; const double f = r - i0; const float a = cbBuf[c][(size_t) i0], b = cbBuf[c][(size_t) ((i0 + 1) & 4095)]; return a + (b - a) * f; };
                const double yl = rd (0, cbDL), yr = rd (1, cbDR);
                cbLp[0] += (yl - cbLp[0]) * cbA; cbLp[1] += (yr - cbLp[1]) * cbA;
                cbBuf[0][(size_t) cbW] = (float) (busL + cbLp[0] * cbFb); cbBuf[1][(size_t) cbW] = (float) (busR + cbLp[1] * cbFb); cbW = (cbW + 1) & 4095;
                busL = (float) (busL * (1 - cbMix * 0.4) + cbLp[0] * cbMix); busR = (float) (busR * (1 - cbMix * 0.4) + cbLp[1] * cbMix);
            }
            if (P.env)
            {
                const double lv = std::max (std::abs (busL), std::abs (busR)); efE += (lv - efE) * (lv > efE ? efAtt : efRel);
                if ((efN++ & 15u) == 0) { const double f = 180 + std::min (1.0, efE * efGain) * efSpan; coef (efC, 0, f, efQ); coef (efC, 5, f * 1.15, 0.9); }
                busL = (float) tick (efC, 5, efZ, 4, tick (efC, 0, efZ, 0, busL)); busR = (float) tick (efC, 5, efZ, 6, tick (efC, 0, efZ, 2, busR));
            }
            if (P.fg)
            {
                const double k16 = std::floor (beat * 4); const int idx = (int) ((((long long) k16 % 16) + 16) % 16); const bool act = P.fgSteps[idx] != 0;
                fgF += ((act ? fgOpen : fgClosed) - fgF) * fgKf; fgG += ((act ? 1.0 : fgLow) - fgG) * fgKg;
                if ((fgN++ & 15u) == 0) { coef (fgC, 0, fgF, fgQ); coef (fgC, 5, fgF, 0.70710678); }
                busL = (float) (tick (fgC, 5, fgZ, 4, tick (fgC, 0, fgZ, 0, busL)) * fgG); busR = (float) (tick (fgC, 5, fgZ, 6, tick (fgC, 0, fgZ, 2, busR)) * fgG);
            }
            L[i] = std::isfinite (busL) ? (float) busL : 0.f; R[i] = std::isfinite (busR) ? (float) busR : 0.f;
        }
    }

private:
    double sr = 48000;
    std::array<std::vector<float>, 2> cbBuf; int cbW = 0; double cbLp[2] {};
    bool efInit = false; double efE = 0; unsigned efN = 0; double efC[10] {}, efZ[8] {};
    bool fgInit = false; double fgF = 1000, fgG = 1; unsigned fgN = 0; double fgC[10] {}, fgZ[8] {};
    void coef (double* c, int o, double f, double q) const
    {
        const double w0 = 2 * 3.141592653589793 * std::min (f, sr * 0.45) / sr, cs = std::cos (w0), al = std::sin (w0) / (2 * q), a0 = 1 + al;
        c[o] = (1 - cs) / 2 / a0; c[o + 1] = (1 - cs) / a0; c[o + 2] = (1 - cs) / 2 / a0; c[o + 3] = -2 * cs / a0; c[o + 4] = (1 - al) / a0;
    }
    static double tick (const double* c, int o, double* z, int zo, double x)
    {
        const double y = c[o] * x + z[zo]; z[zo] = c[o + 1] * x - c[o + 3] * y + z[zo + 1]; z[zo + 1] = c[o + 2] * x - c[o + 4] * y; return y;
    }
};

// ---------------------------------------------------------------- LFOs
struct Dest { const char* key; double mosaic::FxState::* field; double lo, hi; };
// only controls the engine glides to cheaply (none that rebuild a curve or a reverb)
inline const std::vector<Dest>& dests()
{
    using S = mosaic::FxState;
    static const std::vector<Dest> d {
        { "busFilter", &S::busFilter, -1, 1 }, { "djHpf", &S::djHpf, 0, 1 }, { "djLpf", &S::djLpf, 0, 1 },
        { "phaserWet", &S::phaserWet, 0, 1 }, { "chorusMix", &S::chorusMix, 0, 1 }, { "ensMix", &S::ensMix, 0, 1 },
        { "tremDepth", &S::tremDepth, 0, 1 }, { "fsShift", &S::fsShift, -45, 45 }, { "fsMix", &S::fsMix, 0, 1 },
        { "rotSpeed", &S::rotSpeed, 0, 1 }, { "rotMix", &S::rotMix, 0, 1 }, { "vowPos", &S::vowPos, 0, 1 },
        { "apDepth", &S::apDepth, 0, 1 }, { "bpMix", &S::bpMix, 0, 1 }, { "rtMix", &S::rtMix, 0, 1 },
        { "delayWet", &S::delayWet, 0, 1 }, { "delayFb", &S::delayFb, 0, 0.85 }, { "ppMix", &S::ppMix, 0, 1 },
        { "dubMix", &S::dubMix, 0, 1 }, { "dubFb", &S::dubFb, 0, 1 }, { "reverbWet", &S::reverbWet, 0, 1 },
        { "springMix", &S::springMix, 0, 1 }, { "gcMix", &S::gcMix, 0, 1 }, { "width", &S::width, 0, 2 },
        { "tapeWow", &S::tapeWow, 0, 1 }, { "tlAngle", &S::tlAngle, 0, 1 },
    };
    return d;
}

struct Lfo
{
    bool on = false; int dest = -1; double div = 4; int shape = 0; double depth = 0.5; float steps[16] {};
};
enum Shape { sine, tri, sawUp, sawDown, square, random, stepsShape };

inline int shapeIndex (const std::string& s)
{
    static const char* N[] = { "sine", "tri", "saw", "ramp", "square", "random", "steps" };
    for (int i = 0; i < 7; ++i) if (s == N[i]) return i;
    return 0;
}

// -1..1 for the swinging shapes, 0..1 for STEPS (which only adds)
inline double lfoValue (const Lfo& l, double beat, int lane)
{
    const double div = std::max (1.0 / 16, l.div);
    if (l.shape == stepsShape)
    {
        const long long k = (long long) std::floor (beat / div); return l.steps[(int) (((k % 16) + 16) % 16)];
    }
    const double cyc = beat / div, p = cyc - std::floor (cyc);
    switch (l.shape)
    {
        case tri: return p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4;
        case sawUp: return 2 * p - 1;
        case sawDown: return 1 - 2 * p;
        case square: return p < 0.5 ? 1 : -1;
        case random: { uint32_t h = (uint32_t) (long long) std::floor (cyc) * 2654435761u ^ (uint32_t) (lane + 1) * 40503u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                       return (h & 0xffff) / 32767.5 - 1; }
        default: return std::sin (2 * 3.141592653589793 * p);
    }
}

// the settings with every LFO's move applied
inline void modulate (mosaic::FxState& s, const std::array<Lfo, 3>& lfos, double beat)
{
    const auto& D = dests();
    for (int k = 0; k < 3; ++k)
    {
        const Lfo& l = lfos[(size_t) k]; if (! l.on || l.dest < 0 || l.dest >= (int) D.size()) continue;
        const Dest& d = D[(size_t) l.dest]; const double span = d.hi - d.lo, v = lfoValue (l, beat, k);
        const double move = l.shape == stepsShape ? l.depth * span * v : l.depth * span * 0.5 * v;
        s.*(d.field) = std::min (d.hi, std::max (d.lo, s.*(d.field) + move));
    }
}
} // namespace mosaicfx
