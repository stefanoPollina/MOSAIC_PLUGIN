#include "PeriodicOsc.h"
#include <algorithm>

namespace mosaic::wa
{
namespace
{
int waveSizeFor (double sr) { return sr <= 24000 ? 2048 : sr <= 88200 ? 4096 : 16384; }
// in-place radix-2 complex FFT with e^(+i…) (an unscaled inverse transform)
void ifft (std::vector<double>& re, std::vector<double>& im)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; i++) { int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); } }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int h = len >> 1;
        for (int k = 0; k < h; k++)
        {
            const double a = 2 * 3.141592653589793 * k / len, cr = std::cos (a), ci = std::sin (a);
            for (int i = 0; i < n; i += len)
            {
                const int p = i + k, q = p + h;
                const double xr = re[(size_t) q] * cr - im[(size_t) q] * ci, xi = re[(size_t) q] * ci + im[(size_t) q] * cr;
                re[(size_t) q] = re[(size_t) p] - xr; im[(size_t) q] = im[(size_t) p] - xi; re[(size_t) p] += xr; im[(size_t) p] += xi;
            }
        }
    }
}
}

PeriodicWave::PeriodicWave (double sr, const float* real, const float* imag, int nc, bool disableNormalization)
{
    waveSize = waveSizeFor (sr);
    ranges = (int) (0.5 + 3 * std::log2 ((float) waveSize));
    rate = (float) (waveSize / sr); invRate = (float) (sr / waveSize);
    lowestFundamental = (float) (sr / 2) / (float) (waveSize / 2);
    const int half = waveSize / 2; nc = std::min (nc, half);
    float norm = 1;
    tables.resize ((size_t) ranges);
    int lastKeep = -1;
    for (int r = 0; r < ranges; ++r)
    {
        const int keep = std::min (nc, partialsForRange (r) + 1);   // bins [keep, half) are cleared, as are DC and Nyquist
        auto& t = tables[(size_t) r];
        if (keep == lastKeep) { t = tables[(size_t) r - 1]; continue; }   // same partials as the range before: same table
        lastKeep = keep;
        // x[n] = Σ_k real[k]·cos(2πkn/N) + imag[k]·sin(2πkn/N), k = 1 .. keep-1
        std::vector<double> xr ((size_t) waveSize, 0.0), xi ((size_t) waveSize, 0.0);
        for (int k = 1; k < keep; ++k) { xr[(size_t) k] = 0.5 * real[k]; xi[(size_t) k] = -0.5 * imag[k]; xr[(size_t) (waveSize - k)] = 0.5 * real[k]; xi[(size_t) (waveSize - k)] = 0.5 * imag[k]; }
        ifft (xr, xi);
        t.resize ((size_t) waveSize); for (int n = 0; n < waveSize; ++n) t[(size_t) n] = (float) xr[(size_t) n];
        if (! disableNormalization && r == 0) { float mx = 0; for (float v : t) mx = std::max (mx, std::abs (v)); if (mx != 0) norm = 1.0f / mx; }
        for (float& v : t) v *= norm;
    }
}

int PeriodicWave::partialsForRange (int r) const
{
    const float centsToCull = (float) r * centsPerRange;
    const float cullingScale = (float) std::pow (2.0, -centsToCull / 1200);
    return (int) (unsigned) (cullingScale * (float) (waveSize / 2));
}

void PeriodicWave::tablesFor (float f, const float*& lower, const float*& higher, float& tif) const
{
    f = std::abs (f);
    const float ratio = f > 0 ? f / lowestFundamental : 0.5f;
    const float centsAbove = std::log2 (ratio) * 1200;
    float pitchRange = 1 + centsAbove / centsPerRange;
    pitchRange = std::max (pitchRange, 0.0f); pitchRange = std::min (pitchRange, (float) (ranges - 1));
    const int r1 = (int) (unsigned) pitchRange, r2 = r1 < ranges - 1 ? r1 + 1 : r1;
    lower = tables[(size_t) r2].data(); higher = tables[(size_t) r1].data(); tif = pitchRange - (float) r1;
}

float OscPhase::tick (const PeriodicWave& w, float incr) const
{
    const float* lo; const float* hi; float tif;
    w.tablesFor (w.invRateScale() * incr, lo, hi, tif);
    const unsigned mask = (unsigned) w.size() - 1; const unsigned i0 = (unsigned) vri; const float aincr = std::abs (incr);
    double sl = 0, sh = 0;
    if (aincr >= 0.3f)
    {
        const unsigned a = i0 & mask, b = (i0 + 1) & mask; const double f = (double) ((float) vri - (float) i0);
        sh = (1 - f) * hi[a] + f * hi[b]; sl = (1 - f) * lo[a] + f * lo[b];
    }
    else if (aincr >= 0.16f)
    {
        const double t = vri - i0, c[3] = { 0.5 * t * (t - 1), 1 - t * t, 0.5 * t * (t + 1) };
        for (int k = 0; k < 3; ++k) { const unsigned idx = (i0 + (unsigned) (k - 1)) & mask; sl += c[k] * lo[idx]; sh += c[k] * hi[idx]; }
    }
    else
    {
        const double t = vri - i0, t2 = t * t;
        const double c[5] = { t * (t2 - 1) * (t - 2) / 24, -t * (t - 1) * (t2 - 4) / 6, (t2 - 1) * (t2 - 4) / 4, -t * (t + 1) * (t2 - 4) / 6, t * (t2 - 1) * (t + 2) / 24 };
        for (int k = 0; k < 5; ++k) { const unsigned idx = (i0 + (unsigned) (k - 2)) & mask; sl += c[k] * lo[idx]; sh += c[k] * hi[idx]; }
    }
    return (float) ((1 - tif) * sh + tif * sl);
}
} // namespace mosaic::wa
