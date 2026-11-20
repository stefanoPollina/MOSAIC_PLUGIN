// osc_part coeffs.bin H freq detune fmHz fmDepth out.raw : one Chrome-style periodic oscillator
#include "../Source/dsp/PeriodicOsc.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main (int, char** a)
{
    std::vector<float> c (1026); FILE* f = fopen (a[1], "rb"); if (fread (c.data(), 4, 1026, f) != 1026) return 1; fclose (f);
    const int H = atoi (a[2]); const float freq = (float) atof (a[3]), det = (float) atof (a[4]); const double fmHz = atof (a[5]), fmD = atof (a[6]);
    mosaic::wa::PeriodicWave w (48000, c.data(), c.data() + 513, H + 1); mosaic::wa::OscPhase ph;
    const int V = 0; const float ds = V == 1 ? (float) std::exp2 ((double) det / 1200.0) : V == 2 ? (float) std::pow (2.0, (double) det / 1200.0) : std::exp2 (det / 1200.0f); FILE* o = fopen (a[7], "wb");
    for (int n = 0; n < 24000; ++n)
    {
        const float fm = (float) (std::sin (2 * M_PI * fmHz * n / 48000) * fmD);
        const float inc = V == 8 ? (freq + fm) * ((float) std::pow (2.0, (double) det / 1200.0) * w.rateScale()) : V == 9 ? (freq + fm) * ((float) std::pow (2.0f, det / 1200.0f) * w.rateScale()) : V == 10 ? (freq + fm) * ((float) std::exp ((double) (det / 1200.0f) * 0.6931471805599453) * w.rateScale()) : V == 5 ? (freq + fm) * (ds * w.rateScale()) : V == 6 ? (freq + fm) * (float) (std::exp2 ((double) det / 1200.0) * (double) w.rateScale()) : V == 7 ? (float) ((double) (freq + fm) * (double) (float) (std::exp2 ((double) det / 1200.0) * (double) w.rateScale())) : V == 3 ? (float) ((double) (freq + fm) * std::exp2 ((double) det / 1200.0) * (double) w.rateScale()) : V == 4 ? (float) ((freq + fm) * std::exp2 ((double) det / 1200.0)) * w.rateScale() : mosaic::wa::OscPhase::increment (w, freq + fm, ds, 24000);
        const float y = ph.tick (w, inc); ph.advance (w, inc); fwrite (&y, 4, 1, o);
    }
    fclose (o);
}
