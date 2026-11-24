#pragma once
// GRANULAR CLOUD: mirrors class GranularCloud in the page's worklet (same grains, random sequence, filters and guards).
// A 3 s stereo ring buffer of the signal; Hann-windowed grains scattered 0.08-1.68 s back, pitched (x0.5 / x1 / x1.498 / x2, +-0.5% per grain),
// panned across +-85%, optionally alternating reversed (REV). The cloud passes a warm 1.8 kHz low-pass; REGEN feeds it back
// (180 Hz - 1.65 kHz band, soft-clipped, capped at 85%); FREEZE stops recording.
// Robustness: off = untouched pass-through; grain gain normalised by overlap; at most 24 grains; mix / regen smoothed over 40 ms;
// any non-finite value resets the effect to silence. No allocation after prepare().
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

class GranularCloud
{
public:
    struct Params { bool on = false, freeze = false, rev = false; double mix = 0.35, regen = 0.38, size = 180, dens = 14, pitch = 2; };

    void prepare (double sampleRate)
    {
        sr = sampleRate; N = (int) std::ceil (sr * 3); rL.assign ((size_t) N, 0.0f); rR.assign ((size_t) N, 0.0f); w = 0;
        for (auto& g : gs) g = Grain{};
        seed = 0x2b1c5e3du; cnt = 1; flip = 0; mix = 0; regen = 0; active = 0;
        k = 1 - std::exp (-1 / (0.04 * sr));
        coef (lp, 1800, 0.7071, false); coef (fhp, 180, 0.7071, true); coef (flp, 1650, 0.7071, false);
        z.fill (0); fbL = fbR = 0;
    }
    void set (const Params& in)
    {
        auto c = [] (double v, double d, double lo, double hi) { return std::isfinite (v) ? std::max (lo, std::min (hi, v)) : d; };
        p.on = in.on; p.freeze = in.freeze; p.rev = in.rev; p.mix = c (in.mix, 0.35, 0, 1); p.regen = c (in.regen, 0.38, 0, 0.85);
        p.size = c (in.size, 180, 45, 380); p.dens = c (in.dens, 14, 4, 28); p.pitch = c (in.pitch, 2, 0.5, 2);
    }
    void process (float* L, float* R, int n)
    {
        if (N <= 0) return;
        if (! p.on && mix < 1e-5 && active == 0)
        {   // idle: untouched pass-through, keep recording so switching on has material
            for (int i = 0; i < n; ++i) { rL[(size_t) w] = L[i]; rR[(size_t) w] = R[i]; w = (w + 1) % N; }
            mix = 0; fbL = fbR = 0; return;
        }
        const double tm = p.on ? p.mix : 0, tr = p.on ? p.regen : 0; const int spawnEvery = std::max (1, (int) std::lround (sr / p.dens));
        for (int i = 0; i < n; ++i)
        {
            mix += (tm - mix) * k; regen += (tr - regen) * k;
            const double xL = L[i], xR = R[i];
            if (! p.freeze) { rL[(size_t) w] = (float) std::tanh (xL + fbL); rR[(size_t) w] = (float) std::tanh (xR + fbR); w = (w + 1) % N; }
            if (p.on && --cnt <= 0) { cnt = spawnEvery; spawn(); }
            double wl = 0, wr = 0;
            if (active)
                for (auto& g : gs)
                {
                    if (! g.on) continue;
                    const double adv = g.t * g.rate; if (adv >= g.len - 1) { g.on = false; --active; continue; }
                    const double pos = g.rev ? g.len - 1 - adv : adv, win = 0.5 * (1 - std::cos (2 * PI * pos / (g.len - 1)));
                    const double f = g.src + pos; const long i0 = (long) std::floor (f); const double fr = f - (double) i0;
                    const size_t a = (size_t) (i0 % N), b = (a + 1) % (size_t) N;
                    const double m = 0.5 * ((rL[a] + (rL[b] - rL[a]) * fr) + (rR[a] + (rR[b] - rR[a]) * fr)) * win * g.amp;
                    wl += m * g.gl; wr += m * g.gr; g.t += 1;
                }
            const double yl = bq (lp, 0, wl), yr = bq (lp, 2, wr);
            if (! std::isfinite (yl + yr)) { reset(); L[i] = (float) xL; R[i] = (float) xR; continue; }   // never pass a bad sample on
            fbL = bq (flp, 8, bq (fhp, 4, yl)) * regen; fbR = bq (flp, 10, bq (fhp, 6, yr)) * regen;
            L[i] = (float) (xL + yl * mix); R[i] = (float) (xR + yr * mix);
        }
        if (! std::isfinite (fbL + fbR + z[0] + z[2] + mix)) reset();
    }

private:
    static constexpr double PI = 3.14159265358979323846;
    struct Grain { bool on = false, rev = false; double src = 0, rate = 1, len = 0, t = 0, gl = 0, gr = 0, amp = 0; };
    double sr = 48000, k = 0, mix = 0, regen = 0, fbL = 0, fbR = 0; int N = 0, w = 0, cnt = 1, active = 0; uint32_t seed = 0, flip = 0;
    std::vector<float> rL, rR; std::array<Grain, 24> gs {}; std::array<double, 5> lp {}, fhp {}, flp {}; std::array<double, 12> z {}; Params p;

    void coef (std::array<double, 5>& c, double f, double q, bool hp)
    {
        const double w0 = 2 * PI * f / sr, cs = std::cos (w0), al = std::sin (w0) / (2 * q), a0 = 1 + al;
        const double b0 = (hp ? (1 + cs) / 2 : (1 - cs) / 2) / a0, b1 = (hp ? -(1 + cs) : 1 - cs) / a0;
        c = { b0, b1, b0, (-2 * cs) / a0, (1 - al) / a0 };
    }
    double bq (const std::array<double, 5>& c, int zi, double x)
    {
        const double y = c[0] * x + z[(size_t) zi]; z[(size_t) zi] = c[1] * x - c[3] * y + z[(size_t) zi + 1]; z[(size_t) zi + 1] = c[2] * x - c[4] * y; return y;
    }
    double rnd() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; }
    void reset()
    {
        std::fill (rL.begin(), rL.end(), 0.0f); std::fill (rR.begin(), rR.end(), 0.0f); z.fill (0); fbL = fbR = 0;
        for (auto& g : gs) g.on = false;
        active = 0; mix = 0; regen = 0;
    }
    void spawn()
    {
        if (active >= 24) return;
        Grain* g = nullptr; for (auto& x : gs) if (! x.on) { g = &x; break; } if (! g) return;
        const double len = std::max (512.0, std::floor (p.size / 1000 * sr)), scatter = std::floor ((0.08 + rnd() * 1.6) * sr);
        const long s = ((long) (w - scatter - len) % N + (long) N * 2) % N; g->src = (double) s; g->len = len; g->t = 0;
        g->rate = p.pitch * (1 + (rnd() - 0.5) * 0.01);
        const double a = ((rnd() * 2 - 1) * 0.85 + 1) * PI / 4; g->gl = std::cos (a) * std::sqrt (2.0); g->gr = std::sin (a) * std::sqrt (2.0);
        g->rev = p.rev && (flip++ % 2 == 1);
        g->amp = 1.0 / std::sqrt (std::max (1.0, p.dens * (len / g->rate) / sr * 0.5));
        g->on = true; ++active;
    }
};
