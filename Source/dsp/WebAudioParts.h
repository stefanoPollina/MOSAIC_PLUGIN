#pragma once
/* The parts of Chrome's Web Audio that Mosaic's effects are built from, rebuilt to behave the same:
   AudioParam smoothing (setTargetAtTime), BiquadFilter (Chrome's coefficient formulas), Delay (linear interpolation),
   WaveShaper (with Chrome's 2x up/down-sampler), DynamicsCompressor (Chrome's kernel, 32-frame divisions) and the
   equal-power StereoPanner. Everything works per sample so audio-rate modulation is exact. */
#include <array>
#include <cmath>
#include <vector>

namespace mosaic::wa
{
constexpr double PI = 3.141592653589793;

// AudioParam with setValue / setTargetAtTime (Chrome: per sample, v += (target - v) * (1 - e^(-1/(tau·sr))))
struct Param
{
    // Chrome iterates v += (target - v)·(1 - e^(-1/(tau·sr))) and, at each render-quantum boundary, snaps to the target once
    // 10 time constants have passed or |v - target| < e^-10 · |target|.
    double v = 0, target = 0, k = 0, snapAfter = 0; bool moving = false; int phase = 0; long long clock = 0;   // phase: frame within the quantum
    void init (double x) { v = target = x; moving = false; }
    void setTarget (double x, double tau, double sr, int framePhase = 0)
    {
        target = x; k = 1 - std::exp (-1 / (std::max (1e-6, tau) * sr)); moving = v != x; phase = framePhase & 127;
        clock = 0; snapAfter = 10 * tau * sr;
    }
    bool close() const
    {
        constexpr double th = 4.539992976248485e-05;
        return (double) clock >= snapAfter || std::abs (v - target) < th * std::abs (target);
    }
    double next()   // value at this frame, then step to the next
    {
        if (moving && phase == 0 && close()) { v = target; moving = false; }
        const double out = v;
        if (moving) v += (target - v) * k;
        phase = (phase + 1) & 127; ++clock;
        return out;
    }
    double nextK (int n)   // k-rate: one value per render quantum
    {
        if (moving && close()) { v = target; moving = false; }
        const double out = v;
        if (moving) v += (target - v) * (1 - std::pow (1 - k, n));
        clock += n;
        return out;
    }
};

enum class BiquadType { lowpass, highpass, bandpass, allpass, notch, highshelf };   // highshelf: the Q argument carries the gain in dB, as WebAudio's gain
struct Biquad
{
    BiquadType type = BiquadType::lowpass;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
    double lastF = -1, lastQ = -1, sr = 48000;
    void reset() { for (int c = 0; c < 2; ++c) x1[c] = x2[c] = y1[c] = y2[c] = 0; }
    void coeffs (double freq, double Q)
    {
        if (freq == lastF && Q == lastQ) return;
        lastF = freq; lastQ = Q;
        const double nyq = sr * 0.5, f = std::max (0.0, std::min (1.0, freq / nyq));
        auto set = [&] (double B0, double B1, double B2, double A0, double A1, double A2) { const double inv = 1 / A0; b0 = B0 * inv; b1 = B1 * inv; b2 = B2 * inv; a1 = A1 * inv; a2 = A2 * inv; };
        switch (type)
        {
            case BiquadType::lowpass:
                if (f >= 1) set (1, 0, 0, 1, 0, 0);
                else if (f > 0) { const double r = std::pow (10.0, Q / 20), th = PI * f, al = std::sin (th) / (2 * r), cw = std::cos (th), be = (1 - cw) / 2; set (be, 2 * be, be, 1 + al, -2 * cw, 1 - al); }
                else set (0, 0, 0, 1, 0, 0);
                break;
            case BiquadType::highpass:
                if (f >= 1) set (0, 0, 0, 1, 0, 0);
                else if (f > 0) { const double r = std::pow (10.0, Q / 20), th = PI * f, al = std::sin (th) / (2 * r), cw = std::cos (th), be = (1 + cw) / 2; set (be, -2 * be, be, 1 + al, -2 * cw, 1 - al); }
                else set (1, 0, 0, 1, 0, 0);
                break;
            case BiquadType::bandpass:
                if (f > 0 && f < 1) { if (Q > 0) { const double w = PI * f, al = std::sin (w) / (2 * Q), k = std::cos (w); set (al, 0, -al, 1 + al, -2 * k, 1 - al); } else set (1, 0, 0, 1, 0, 0); }
                else set (0, 0, 0, 1, 0, 0);
                break;
            case BiquadType::allpass:
                if (f > 0 && f < 1) { if (Q > 0) { const double w = PI * f, al = std::sin (w) / (2 * Q), k = std::cos (w); set (1 - al, -2 * k, 1 + al, 1 + al, -2 * k, 1 - al); } else set (-1, 0, 0, 1, 0, 0); }
                else set (1, 0, 0, 1, 0, 0);
                break;
            case BiquadType::highshelf:   // WebAudio highshelf (S = 1)
                if (f >= 1) set (1, 0, 0, 1, 0, 0);
                else if (f <= 0) { const double A2 = std::pow (10.0, Q / 20); set (A2, 0, 0, 1, 0, 0); }
                else { const double A = std::pow (10.0, Q / 40), w = PI * f, al = std::sin (w) / 2 * std::sqrt (2.0), k = std::cos (w), sa = 2 * std::sqrt (A) * al;
                       set (A * ((A + 1) + (A - 1) * k + sa), -2 * A * ((A - 1) + (A + 1) * k), A * ((A + 1) + (A - 1) * k - sa), (A + 1) - (A - 1) * k + sa, 2 * ((A - 1) - (A + 1) * k), (A + 1) - (A - 1) * k - sa); }
                break;
            case BiquadType::notch:
                if (f > 0 && f < 1) { if (Q > 0) { const double w = PI * f, al = std::sin (w) / (2 * Q), k = std::cos (w); set (1, -2 * k, 1, 1 + al, -2 * k, 1 - al); } else set (0, 0, 0, 1, 0, 0); }
                else set (1, 0, 0, 1, 0, 0);
                break;
        }
    }
    float tick (int c, float xf)
    {
        const double x = xf; const float y = (float) (b0 * x + b1 * x1[c] + b2 * x2[c] - a1 * y1[c] - a2 * y2[c]);   // Chrome keeps y as float
        x2[c] = x1[c]; x1[c] = x; y2[c] = y1[c]; y1[c] = y;
        return y;
    }
};

struct Delay   // stereo DelayNode, rebuilt with Chrome's own buffer length and float read-position maths
{
    std::vector<float> buf[2]; int len = 0, w = 0; double sr = 48000, maxDelay = 1, minDelay = 0;
    void prepare (double sampleRate, double maxSeconds, bool inCycle)
    {
        sr = sampleRate; maxDelay = maxSeconds; minDelay = inCycle ? 128.0 / sampleRate : 0;
        len = 1 + (int) std::ceil (maxSeconds * sr) + 128;   // Chrome: BufferLengthForDelay
        for (auto& b : buf) b.assign ((size_t) len, 0.f); w = 0; written = 0;
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f); w = 0; written = 0; }
    // Chrome builds a DelayNode's kernels for one channel and only adds the second after its first render quantum,
    // so the right channel is silent for the very first quantum (its input is still stored).
    bool monoFirstQuantum = false; long long written = 0;
    bool muted (int c) const { return monoFirstQuantum && c == 1 && written < 128; }
    float read (int c, double seconds) const
    {
        if (muted (c)) return 0;
        const float dt = (float) std::max (minDelay, std::min (maxDelay, seconds));
        const float frames = dt * (float) sr;
        float rp = ((float) w - frames) + (float) len;
        if (rp >= (float) len) rp -= (float) len;
        const int i0 = (int) rp, i1 = (i0 + 1) % len; const float f = rp - (float) i0;
        const float s1 = buf[c][(size_t) i0], s2 = buf[c][(size_t) i1];
        return s1 + f * (s2 - s1);
    }
    void write (int c, float x) { buf[c][(size_t) w] = x; }
    void advance() { if (++w >= len) w = 0; ++written; }
};

// Chrome runs a feedback cycle through a DelayNode one render quantum late: the signal coming back round the loop arrives 128 frames after it left.
struct Lag128
{
    float buf[2][128] {}; int pos = 0;
    void reset() { for (auto& b : buf) for (auto& x : b) x = 0; pos = 0; }
    float read (int c) const { return buf[c][pos]; }
    void write (int c, float x) { buf[c][pos] = x; }
    void advance() { pos = (pos + 1) & 127; }
};

inline float shapeCurve (const std::vector<float>& curve, float x)
{
    if (curve.empty()) return x;
    const int n = (int) curve.size(); const double v = (n - 1) * 0.5 * ((double) x + 1);
    if (v < 0) return curve[0];
    if (v >= n - 1) return curve[(size_t) n - 1];
    const int k = (int) v; const double f = v - k;
    return (float) ((1 - f) * curve[(size_t) k] + f * curve[(size_t) k + 1]);
}

// Chrome's WaveShaper oversample '2x': UpSampler (128-tap Blackman-windowed half-sample sinc) → curve at 2x → DownSampler (255-tap half-band)
struct Shaper2x
{
    std::array<double, 128> up {}; std::array<double, 128> down {};
    std::vector<float> inHist[2], hiHist[2]; int inPos = 0, hiPos = 0;
    Shaper2x()
    {
        const double alpha = 0.16, a0 = 0.5 * (1 - alpha), a1 = 0.5, a2 = 0.5 * alpha;
        { const int n = 128, half = n / 2; const double off = -0.5;
          for (int i = 0; i < n; ++i) { const double s = PI * (i - half - off); const double sinc = s == 0 ? 1.0 : std::sin (s) / s; const double x = (i - off) / n;
                                        up[(size_t) i] = sinc * (a0 - a1 * std::cos (2 * PI * x) + a2 * std::cos (4 * PI * x)); } }
        { const int n = 256, half = n / 2;
          for (int i = 1; i < n; i += 2) { double s = 0.5 * PI * (i - half); double sinc = s == 0 ? 1.0 : std::sin (s) / s; sinc *= 0.5; const double x = (double) i / n;
                                           down[(size_t) (i - 1) / 2] = sinc * (a0 - a1 * std::cos (2 * PI * x) + a2 * std::cos (4 * PI * x)); } }
        for (int c = 0; c < 2; ++c) { inHist[c].assign (256, 0.f); hiHist[c].assign (512, 0.f); }
    }
    void reset() { for (int c = 0; c < 2; ++c) { std::fill (inHist[c].begin(), inHist[c].end(), 0.f); std::fill (hiHist[c].begin(), hiHist[c].end(), 0.f); } inPos = hiPos = 0; }
    // one input sample per channel in, one out (shared write positions: call for c = 0 then c = 1, then advance())
    float tick (int c, float x, const std::vector<float>& curve)
    {
        auto& ih = inHist[c]; ih[(size_t) (inPos & 255)] = x;
        // up: even = x[n-64], odd = Σ up[j] x[n-j]
        const float even = ih[(size_t) ((inPos - 64) & 255)];
        double odd = 0; for (int j = 0; j < 128; ++j) odd += up[(size_t) j] * ih[(size_t) ((inPos - j) & 255)];
        auto& hh = hiHist[c];
        hh[(size_t) (hiPos & 511)] = shapeCurve (curve, even);
        hh[(size_t) ((hiPos + 1) & 511)] = shapeCurve (curve, (float) odd);
        // down: Σ down[j] · odd-stream[i-j] (odd-stream[i] = hi[2i-1]) + 0.5 · hi[2i-128]   (2i = hiPos)
        double y = 0; for (int j = 0; j < 128; ++j) y += down[(size_t) j] * hh[(size_t) ((hiPos - 1 - 2 * j) & 511)];
        y += 0.5 * hh[(size_t) ((hiPos - 128) & 511)];
        return (float) y;
    }
    void advance() { inPos++; hiPos += 2; }
};

// Chrome's DynamicsCompressor kernel (float, 32-frame divisions, 6 ms pre-delay, adaptive release, makeup gain)
struct Compressor
{
    double sr = 48000; Param threshold, knee, ratio, attack, release;
    std::vector<float> pre[2]; int preW = 0, preR = 0; static constexpr int MAXPRE = 1024;
    float detectorAverage = 0, compressorGain = 1, maxAttackDb = -1;
    float dbThreshold = -1e9f, dbKnee = -1, cRatio = -1, linearThreshold = 0, slope = 1, kneeThresholdDb = 0, kneeThreshold = 0, ykneeThresholdDb = 0, K = 1;
    int divCount = 0; float envelopeRate = 1, scaledDesiredGain = 1, masterLinearGain = 1;
    float kThr = 0, kKnee = 0, kRatio = 0, kAtt = 0, kRel = 0;
    void prepare (double sampleRate)
    {
        sr = sampleRate; for (auto& p : pre) p.assign (MAXPRE, 0.f);
        const int pd = (int) (0.006 * sr); preW = 0; preR = (preW - std::min (pd, MAXPRE - 1)) & (MAXPRE - 1);
        detectorAverage = 0; compressorGain = 1; maxAttackDb = -1; divCount = 0;
    }
    static float db2lin (float d) { return std::pow (10.f, 0.05f * d); }
    static float lin2db (float l) { return 20.f * std::log10 (l); }
    float kneeCurve (float x, float k) const { if (x < linearThreshold) return x; return linearThreshold + (1 - std::exp (-k * (x - linearThreshold))) / k; }
    float saturate (float x, float k) const
    {
        if (x < kneeThreshold) return kneeCurve (x, k);
        const float xDb = lin2db (x), yDb = ykneeThresholdDb + slope * (xDb - kneeThresholdDb);
        return db2lin (yDb);
    }
    float slopeAt (float x, float k) const
    {
        if (x < linearThreshold) return 1;
        const float x2 = x * 1.001f, xDb = lin2db (x), x2Db = lin2db (x2), yDb = lin2db (kneeCurve (x, k)), y2Db = lin2db (kneeCurve (x2, k));
        return (y2Db - yDb) / (x2Db - xDb);
    }
    float kAtSlope (float desired) const
    {
        const float xDb = dbThreshold + dbKnee, x = db2lin (xDb);
        float minK = 0.1f, maxK = 10000, k = 5;
        for (int i = 0; i < 15; ++i) { const float s = slopeAt (x, k); if (s < desired) maxK = k; else minK = k; k = std::sqrt (minK * maxK); }
        return k;
    }
    float updateCurve (float th, float kn, float ra)
    {
        if (th != dbThreshold || kn != dbKnee || ra != cRatio)
        {
            dbThreshold = th; linearThreshold = db2lin (th); dbKnee = kn; cRatio = ra; slope = 1 / ra;
            const float k = kAtSlope (1 / ra);
            kneeThresholdDb = th + kn; kneeThreshold = db2lin (kneeThresholdDb);
            ykneeThresholdDb = lin2db (kneeCurve (kneeThreshold, k)); K = k;
        }
        return K;
    }
    // call once per render quantum (k-rate parameters)
    void quantumStart (int n) { kThr = (float) threshold.nextK (n); kKnee = (float) knee.nextK (n); kRatio = (float) ratio.nextK (n); kAtt = (float) attack.nextK (n); kRel = (float) release.nextK (n); }
    void tick (float& l, float& r)
    {
        if (divCount == 0)
        {
            const float k = updateCurve (kThr, kKnee, kRatio);
            const float fullRangeGain = saturate (1, k);
            masterLinearGain = std::pow (1 / fullRangeGain, 0.6f);
            const float attackFrames = std::max (0.001f, kAtt) * (float) sr, releaseFrames = (float) sr * kRel;
            const float y1 = releaseFrames * 0.09f, y2 = releaseFrames * 0.16f, y3 = releaseFrames * 0.42f, y4 = releaseFrames * 0.98f;
            const float kA = 0.9999999999999998f * y1 + 1.8432219684323923e-16f * y2 - 1.9373394351676423e-16f * y3 + 8.824516011816245e-18f * y4;
            const float kB = -1.5788320352845888f * y1 + 2.3305837032074286f * y2 - 0.9141194204840429f * y3 + 0.1623677525612032f * y4;
            const float kC = 0.5334142869106424f * y1 - 1.272736789213631f * y2 + 0.9258856042207512f * y3 - 0.18656310191776226f * y4;
            const float kD = 0.08783463138207234f * y1 - 0.1694162967925622f * y2 + 0.08588057951595272f * y3 - 0.00429891410546283f * y4;
            const float kE = -0.042416883008123074f * y1 + 0.1115693827987602f * y2 - 0.09764676325265872f * y3 + 0.028494263462021576f * y4;
            if (std::isnan (detectorAverage) || std::isinf (detectorAverage)) detectorAverage = 1;
            const float desiredGain = detectorAverage;
            scaledDesiredGain = std::asin (desiredGain) / (0.5f * (float) PI);
            const bool isReleasing = scaledDesiredGain > compressorGain;
            float diffDb = lin2db (compressorGain / scaledDesiredGain);
            if (isReleasing)
            {
                maxAttackDb = -1;
                if (std::isnan (diffDb) || std::isinf (diffDb)) diffDb = -1;
                float x = std::min (0.f, std::max (-12.f, diffDb)); x = 0.25f * (x + 12);
                const float x2 = x * x, x3 = x2 * x, x4 = x2 * x2;
                const float rf = kA + kB * x + kC * x2 + kD * x3 + kE * x4;
                envelopeRate = db2lin (5 / rf);
            }
            else
            {
                if (std::isnan (diffDb) || std::isinf (diffDb)) diffDb = 1;
                if (maxAttackDb == -1 || maxAttackDb < diffDb) maxAttackDb = diffDb;
                const float eff = std::max (0.5f, maxAttackDb), x = 0.25f / eff;
                envelopeRate = 1 - std::pow (x, 1 / attackFrames);
            }
        }
        const float satReleaseFrames = 0.0025f * (float) sr;
        pre[0][(size_t) preW] = l; pre[1][(size_t) preW] = r;
        const float absIn = std::max (std::abs (l), std::abs (r));
        const float shaped = saturate (absIn, K);
        const float att = absIn <= 0.0001f ? 1 : shaped / absIn;
        float attDb = -lin2db (att); attDb = std::max (2.f, attDb);
        const float satReleaseRate = db2lin (attDb / satReleaseFrames) - 1;
        const float rate = att > detectorAverage ? satReleaseRate : 1;
        detectorAverage += (att - detectorAverage) * rate; detectorAverage = std::min (1.f, detectorAverage);
        if (std::isnan (detectorAverage) || std::isinf (detectorAverage)) detectorAverage = 1;
        if (envelopeRate < 1) compressorGain += (scaledDesiredGain - compressorGain) * envelopeRate;
        else { compressorGain *= envelopeRate; compressorGain = std::min (1.f, compressorGain); }
        const float post = std::sin (0.5f * (float) PI * compressorGain);
        const float total = masterLinearGain * post;
        l = pre[0][(size_t) preR] * total; r = pre[1][(size_t) preR] * total;
        preR = (preR + 1) & (MAXPRE - 1); preW = (preW + 1) & (MAXPRE - 1);
        if (++divCount >= 32) divCount = 0;
    }
};

// StereoPanner (equal power), stereo input
inline void stereoPan (float& l, float& r, double pan)
{
    pan = std::max (-1.0, std::min (1.0, pan));
    if (pan <= 0) { const double x = pan + 1, gl = std::cos (x * PI / 2), gr = std::sin (x * PI / 2); const float L = l, R = r; l = (float) (L + R * gl); r = (float) (R * gr); }
    else { const double gl = std::cos (pan * PI / 2), gr = std::sin (pan * PI / 2); const float L = l, R = r; l = (float) (L * gl); r = (float) (R + L * gr); }
}
} // namespace mosaic::wa
