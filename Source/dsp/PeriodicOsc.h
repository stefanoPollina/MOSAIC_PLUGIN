#pragma once
/* Chrome's PeriodicWave and OscillatorNode, rebuilt to match: one band-limited table per third of an octave
   (partials culled as the pitch rises), normalised by the fullest table's peak, read with linear / 3-point / 5-point
   Lagrange interpolation depending on the step size, crossfaded between the two nearest tables. */
#include <cmath>
#include <memory>
#include <vector>

namespace mosaic::wa
{
class PeriodicWave
{
public:
    PeriodicWave (double sampleRate, const float* real, const float* imag, int numberOfComponents, bool disableNormalization = false);
    int size() const { return waveSize; }
    float rateScale() const { return rate; }
    float invRateScale() const { return invRate; }
    // as Chrome's WaveDataForFundamentalFrequency
    void tablesFor (float fundamental, const float*& lower, const float*& higher, float& tableInterp) const;
private:
    int waveSize = 4096, ranges = 36; float rate = 0, invRate = 0, lowestFundamental = 0, centsPerRange = 400;
    std::vector<std::vector<float>> tables;
    int partialsForRange (int r) const;
};
using WavePtr = std::shared_ptr<const PeriodicWave>;

// The read position of one oscillator; process() returns one sample for a phase increment `incr` (wave entries per sample).
struct OscPhase
{
    double vri = 0;   // virtual read index
    float tick (const PeriodicWave& w, float incr) const;
    void advance (const PeriodicWave& w, float incr) { const double N = w.size(); vri += incr; vri -= std::floor (vri * (1.0 / N)) * N; }
    // Chrome: frequency (Hz, already including FM input) × (2^(detune/1200) · rateScale), limited to ±nyquist
    static float increment (const PeriodicWave& w, float frequencyHz, float detuneScale, float nyquist)
    {
        const float inc = frequencyHz * (detuneScale * w.rateScale()), lim = nyquist * w.rateScale();
        return inc < -lim ? -lim : inc > lim ? lim : inc;
    }
};
} // namespace mosaic::wa
