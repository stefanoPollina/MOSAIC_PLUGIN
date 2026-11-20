// Renders Mosaic's effects chain on a deterministic input: render_fx state.json seconds seed out.raw
#include "../Source/dsp/MosaicFx.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    std::ifstream f (argv[1]); std::stringstream ss; ss << f.rdbuf();
    auto j = mosaic::json::parse (ss.str());
    const double secs = atof (argv[2]); const uint32_t seed = (uint32_t) atoi (argv[3]); const double sr = 48000;
    mosaic::MosaicFx fx; fx.testNoFxGating = true; fx.prepare (sr, seed);
    fx.apply (mosaic::FxState::fromJson (j), 0);
    if (j["force"].isObject()) for (auto& [k, v] : j["force"].obj) fx.testForce (k, v.num);
    // wait for the convolver's background IR load
    { std::vector<float> l (128), r (128); for (int k = 0; k < 40; ++k) std::this_thread::sleep_for (std::chrono::milliseconds (25)); }
    const int N = (int) (secs * sr); std::vector<float> out ((size_t) N * 2);
    uint32_t s = 12345; auto rnd = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0 * 2 - 1; };
    for (int pos = 0; pos < N; pos += 128)
    {
        const int n = std::min (128, N - pos); float L[128], R[128];
        for (int i = 0; i < n; ++i) { const int t = pos + i; const double tt = t / sr;
            const double burst = std::fmod (tt, 0.5) < 0.12 ? 1 : 0;
            const double v = burst * (0.4 * std::sin (2 * M_PI * 220 * tt) * std::exp (-8 * std::fmod (tt, 0.5)) + 0.15 * rnd());
            L[i] = (float) v; R[i] = (float) (v * 0.7 + 0.1 * std::sin (2 * M_PI * 331 * tt) * burst); }
        fx.process (L, R, n, pos / sr);
        for (int i = 0; i < n; ++i) { out[(size_t) (pos + i) * 2] = L[i]; out[(size_t) (pos + i) * 2 + 1] = R[i]; }
    }
    FILE* o = fopen (argv[4], "wb"); fwrite (out.data(), 4, out.size(), o); fclose (o);
}
