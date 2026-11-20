#include "MosaicFx.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace mosaic
{
namespace
{
double numD (const json::Value& v, double d) { return v.type == json::Value::Number && std::isfinite (v.num) ? v.num : d; }   // the page's num(v, d)
double orD (const json::Value& v, double d) { return v.orNum (d); }                                                         // x || d
const std::map<std::string, double>& BEATS() { static const std::map<std::string, double> m { { "1/2", 2 }, { "1/4", 1 }, { "3/16", 0.75 }, { "1/8d", 0.75 }, { "1/8", 0.5 }, { "1/8t", 1.0 / 3 }, { "1/16", 0.25 }, { "1/16t", 1.0 / 6 }, { "1/32", 0.125 } }; return m; }
const std::map<std::string, double>& SYNC() { static const std::map<std::string, double> m { { "1/4", 1 }, { "1/8", 0.5 }, { "1/8d", 0.75 }, { "1/16", 0.25 } }; return m; }
inline double clampd (double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline float tri (double p) { return (float) (p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4); }
} // namespace

FxState FxState::fromJson (const json::Value& s)
{
    FxState f; const auto& x = s["fx"];
    f.drive = orD (x["drive"], 0); f.phaserOn = x["phaserOn"].truthy(); f.phaserWet = x["phaserWet"].numOr (0); f.phaserRate = orD (x["phaserRate"], 0); f.phaserDepth = orD (x["phaserDepth"], 0);
    f.delaySync = x["delaySync"].type == json::Value::String ? x["delaySync"].str : ""; f.delayTime = orD (x["delayTime"], 0.3); f.delayFb = orD (x["delayFb"], 0); f.delayWet = orD (x["delayWet"], 0);
    f.multiOn = x["multiOn"].truthy(); f.multiWet = orD (x["multiWet"], 0); f.multiTime = orD (x["multiTime"], 0.3); f.multiFb = orD (x["multiFb"], 0);
    f.reverbWet = orD (x["reverbWet"], 0); f.reverbDecay = orD (x["reverbDecay"], 2);
    f.shimmerOn = x["shimmerOn"].truthy(); f.shimmerMode = x["shimmerMode"].type == json::Value::String ? x["shimmerMode"].str : ""; f.shimmerLevel = orD (x["shimmerLevel"], 0);
    f.shimmerPitch = orD (x["shimmerPitch"], 196); f.shimmerMotion = orD (x["shimmerMotion"], 0);
    f.busFilter = numD (x["busFilter"], 0); f.busFilterRes = numD (x["busFilterRes"], 0.2);
    f.chorusOn = x["chorusOn"].truthy(); f.chorusRate = numD (x["chorusRate"], 0.35); f.chorusDepth = numD (x["chorusDepth"], 0.2); f.chorusMix = numD (x["chorusMix"], 0.15); f.chorusHp = numD (x["chorusHp"], 250);
    f.ppOn = x["ppOn"].truthy(); f.ppSync = x["ppSync"].type == json::Value::String ? x["ppSync"].str : ""; f.ppTime = numD (x["ppTime"], 0.28); f.ppFb = numD (x["ppFb"], 0.35);
    f.ppMix = numD (x["ppMix"], 0.12); f.ppLp = numD (x["ppLp"], 3500); f.ppHp = numD (x["ppHp"], 300);
    f.crushOn = x["crushOn"].truthy(); f.crushBits = numD (x["crushBits"], 8); f.crushMix = numD (x["crushMix"], 0.5);
    f.tremOn = x["tremOn"].truthy(); f.tremMode = x["tremMode"].type == json::Value::String ? x["tremMode"].str : ""; f.tremSync = x["tremSync"].type == json::Value::String ? x["tremSync"].str : ""; f.tremDepth = numD (x["tremDepth"], 0.5);
    f.widthOn = x["widthOn"].truthy(); f.width = numD (x["width"], 1.3);
    f.rvToneOn = x["rvToneOn"].truthy(); f.rvHp = numD (x["rvHp"], 180); f.rvLp = numD (x["rvLp"], 9000);
    f.compOn = x["compOn"].truthy(); f.compThreshold = x["compThreshold"].numOr (0); f.compRatio = x["compRatio"].numOr (1); f.compAttack = x["compAttack"].numOr (0.001); f.compRelease = x["compRelease"].numOr (0.02);
    f.master = s["master"].numOr (0); f.bpm = s["seq"].isObject() && s["seq"]["bpm"].truthy() ? s["seq"]["bpm"].numOr (120) : 120;
    return f;
}

std::vector<float> MosaicFx::makeCurve (double amount) const
{
    const int n = 256; std::vector<float> d ((size_t) n);
    const double k = 1 + std::max (0.0, amount) * 22, norm = std::tanh (k);
    for (int i = 0; i < n; ++i) { const double x = ((double) i / (n - 1)) * 2 - 1; d[(size_t) i] = (float) (std::tanh (k * x) / norm); }
    return d;
}

void MosaicFx::prepare (double sampleRate, uint32_t seed)
{
    sr = sampleRate; rngState = seed; frame = 0; applied = false;
    if (! irRandom) irRandom = [this]
    {
        rngState += 0x6D2B79F5u; uint32_t t = (rngState ^ (rngState >> 15)) * (1u | rngState); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return double (t ^ (t >> 14)) / 4294967296.0;
    };
    auto bq = [this] (wa::Biquad& b, wa::BiquadType t) { b = wa::Biquad(); b.type = t; b.sr = sr; };
    gate.init (1); gateEvents.clear();
    driveCurve = makeCurve (0); driveAmount = -1; driveRamping = false; drive.reset(); driveDry.init (1); driveWet.init (1);
    phaserDry.init (1); phaserWet.init (1); phaseRate.init (0.2); phaseDepth.init (400); phasePh = 0;
    for (auto& s : stages) bq (s, wa::BiquadType::allpass);
    delay.prepare (sr, 2, false); delay.monoFirstQuantum = true; delayLoop.reset(); bq (delayDamp, wa::BiquadType::lowpass); delayTime.init (0); delayFb.init (1); delayWet.init (1); delayDry.init (1);
    const wa::BiquadType mt[3] = { wa::BiquadType::lowpass, wa::BiquadType::bandpass, wa::BiquadType::highpass }; const double mscale[3] = { 1.35, 1, 0.72 };
    for (int b = 0; b < 3; ++b) { bq (multi[(size_t) b].f, mt[b]); multi[(size_t) b].d.prepare (sr, 2.5, false); multi[(size_t) b].d.monoFirstQuantum = true; multi[(size_t) b].loop.reset(); multi[(size_t) b].time.init (0); multi[(size_t) b].fb.init (1); multi[(size_t) b].scale = mscale[b]; }
    multiWet.init (1);
    conv.reset(); conv.prepare ({ sr, 128, 2 }); reverbSeconds = 0; reverbWet.init (1); revBuf.setSize (2, 128);
    shPh = {}; for (auto& p : shFreq) p.init (440); for (auto& p : shGain) p.init (0); bq (shimmerFilter, wa::BiquadType::lowpass); shimmerLfoRate.init (0.12); shimmerDepth.init (1); shLfoPh = 0;
    bq (airHp, wa::BiquadType::highpass); bq (airLp, wa::BiquadType::lowpass); airSend.init (0); airDirect.init (0);
    for (auto& t : airTap) { t.prepare (sr, 0.3, true); t.monoFirstQuantum = true; }
    const double airT = 0.1; airN = (int) std::lround (sr * airT); airRamp.assign ((size_t) airN, 0); airWin.assign ((size_t) airN, 0);
    for (int i = 0; i < airN; ++i) { airRamp[(size_t) i] = (float) (airT * (1 - (double) i / airN)); airWin[(size_t) i] = (float) std::pow (std::sin (wa::PI * i / airN), 2); }
    airStart = (long long) std::llround (0.1 * sr);
    for (auto& b : airSendBuf) b.assign (128, 0.f);
    liveShimmer = liveAir = liveMulti = Live();
    busDry.init (1); busWet.init (0); busOn = false;
    bq (djLp, wa::BiquadType::lowpass); bq (djHp, wa::BiquadType::highpass); djLpF.init (22000); djLpQ.init (0.7); djHpF.init (10); djHpQ.init (0.7);
    crushCurve.clear(); crushBits = 0; crushWet.init (0); crushDry.init (1);
    chRate.init (0.35); chDepthL.init (1); chDepthR.init (1); chWet.init (0); bq (chHp, wa::BiquadType::highpass); chHpF.init (250); chDelay.prepare (sr, 0.05, false); chPh = 0;
    tremRate.init (4); tremAmt.init (0); tremGain.init (1); panAmt.init (0); tremPh = 0; wSideL.init (0.5); wSideR.init (-0.5);
    ppIn.init (0); bq (ppHp, wa::BiquadType::highpass); ppHpF.init (300); bq (ppLp, wa::BiquadType::lowpass); ppLpF.init (3500);
    ppL.prepare (sr, 2, true); ppR.prepare (sr, 2, true); ppL.monoFirstQuantum = ppR.monoFirstQuantum = true; ppLoop.reset(); ppTime.init (0); ppFb.init (0.35); ppWet.init (0);
    bq (rvHp, wa::BiquadType::highpass); rvHpF.init (180); bq (rvLp, wa::BiquadType::lowpass); rvLpF.init (9000);
    comp.prepare (sr); comp.threshold.init (-24); comp.knee.init (30); comp.ratio.init (12); comp.attack.init (0.003); comp.release.init (0.25);
    limiter.prepare (sr); limiter.threshold.init (-1.5); limiter.knee.init (0); limiter.ratio.init (12); limiter.attack.init (0.003); limiter.release.init (0.12);
    { const int n = 16385; const double a = 0.425, w = 0.075; safetyCurve.assign ((size_t) n, 0);
      for (int i = 0; i < n; ++i) { const double u = ((double) i / (n - 1)) * 2 - 1, m = std::abs (u); safetyCurve[(size_t) i] = (float) (m <= a ? u : (u < 0 ? -1 : 1) * (a + w * std::tanh ((m - a) / w))); } }
    master.init (1);
}

juce::AudioBuffer<float> MosaicFx::makeImpulse (double sr, double seconds, const std::function<double()>& random)
{
    const int length = std::max (1, (int) std::floor (sr * seconds));
    juce::AudioBuffer<float> ir (2, length);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = ir.getWritePointer (ch);
        for (int i = 0; i < length; ++i)
        {
            const double t = (double) i / length, env = std::pow (1 - t, 1.6) * std::exp (-2.4 * t), early = i < sr * 0.03 ? 0.4 : 1;
            d[i] = (float) ((random() * 2 - 1) * env * early * 0.55);
        }
    }
    // ConvolverNode normalisation (Chrome): 1/RMS × 10^(-58/20) × 44100/sampleRate
    double power = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < length; ++i) { const double v = ir.getSample (c, i); power += v * v; }
    power = std::sqrt (power / (2.0 * length));
    if (! std::isfinite (power) || power < 0.000125) power = 0.000125;
    ir.applyGain ((float) (1 / power * std::pow (10.0, -58.0 / 20) * (44100.0 / sr)));
    return ir;
}

void MosaicFx::installImpulse (juce::AudioBuffer<float>&& ir, double seconds)
{
    conv.loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    reverbSeconds = seconds;
}

void MosaicFx::loadImpulse (double seconds)
{
    juce::AudioBuffer<float> ir = makeImpulse (sr, seconds, irRandom); const int length = ir.getNumSamples();
    const bool first = reverbSeconds <= 0;
    conv.loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    reverbSeconds = seconds;
    if (first)
    {
        // JUCE starts from a pass-through response and fades to a new one; the browser's first impulse is simply there.
        // So the first impulse is installed before any audio runs: pump silence until it is live and the fade is over, then clear.
        juce::AudioBuffer<float> z (2, 128);
        for (int guard = 0; guard < 4000 && conv.getCurrentIRSize() != length; ++guard)
        {
            z.clear(); juce::dsp::AudioBlock<float> b (z); conv.process (juce::dsp::ProcessContextReplacing<float> (b));
            juce::Thread::sleep (1);
        }
        for (int k = 0; k < (int) (0.1 * sr / 128) + 2; ++k) { z.clear(); juce::dsp::AudioBlock<float> b (z); conv.process (juce::dsp::ProcessContextReplacing<float> (b)); }
        conv.reset();
    }
}

void MosaicFx::setGate (double value, double time, bool cancel)
{
    const double t = std::max (time, (double) frame / sr);
    if (cancel) gateEvents.clear();
    gateEvents.push_back ({ t, clampd (value, 0, 1), cancel });
    std::stable_sort (gateEvents.begin(), gateEvents.end(), [] (const GateEv& a, const GateEv& b) { return a.time < b.time; });
}

void MosaicFx::gateFx (double now)
{
    const double lvl = st.shimmerLevel;
    const bool wShimmer = st.shimmerOn && st.shimmerMode != "air" && lvl > 0.001, wAir = st.shimmerOn && st.shimmerMode == "air" && lvl > 0.001, wMulti = st.multiOn && st.multiWet > 0.001;
    auto upd = [&] (Live& L, bool want, double hold) { if (want) { L.on = true; L.offAt = -1; } else if (L.on && L.offAt < 0) L.offAt = now + hold; };
    upd (liveShimmer, wShimmer, 0.7); upd (liveAir, wAir, 1.5); upd (liveMulti, wMulti, 3.0);
}

void MosaicFx::applyBus (double beat)
{
    const FxState& fx = st;
    const double f = clampd (fx.busFilter, -1, 1), res = clampd (fx.busFilterRes, 0, 1);
    const bool filterOn = std::abs (f) > 0.01;
    const bool active = filterOn || fx.chorusOn || fx.ppOn || fx.crushOn || fx.tremOn || fx.widthOn;
    if (active != busOn) { busOn = active; setTarget (busDry, active ? 0 : 1); setTarget (busWet, active ? 1 : 0); }
    setTarget (djLpF, f < 0 ? 20000 * std::pow (2.0, f * 7.4) : 22000);
    setTarget (djHpF, f > 0 ? 20 * std::pow (2.0, f * 5.9) : 10);
    const double q = 0.7 + res * 9 * std::abs (f);
    setTarget (djLpQ, f < 0 ? q : 0.7); setTarget (djHpQ, f > 0 ? q : 0.7);
    const int bits = (int) std::max (2.0, std::min (16.0, std::round (fx.crushBits)));
    if (fx.crushOn && bits != crushBits)
    {
        const int n = 8192; crushCurve.assign ((size_t) n, 0); const double lv = std::pow (2.0, bits - 1);
        for (int i = 0; i < n; ++i) { const double x = (double) i / (n - 1) * 2 - 1; const double v = x * lv; crushCurve[(size_t) i] = (float) (std::floor (v + 0.5) / lv); }
        crushBits = bits;
    }
    const double cm = fx.crushOn ? clampd (fx.crushMix, 0, 1) : 0;
    setTarget (crushWet, cm); setTarget (crushDry, 1 - cm);
    const bool chOn = fx.chorusOn; const double depth = clampd (fx.chorusDepth, 0, 1);
    setTarget (chRate, std::max (0.02, fx.chorusRate));
    setTarget (chDepthL, chOn ? depth * 0.006 : 0); setTarget (chDepthR, chOn ? -depth * 0.006 : 0);
    setTarget (chWet, chOn ? clampd (fx.chorusMix, 0, 1) : 0);
    setTarget (chHpF, std::max (20.0, fx.chorusHp));
    const bool tOn = fx.tremOn, pan = fx.tremMode == "pan"; const double td = clampd (fx.tremDepth, 0, 1);
    { auto it = BEATS().find (fx.tremSync); setTarget (tremRate, 1 / (beat * (it != BEATS().end() ? it->second : 0.5))); }
    setTarget (tremAmt, tOn && ! pan ? td * 0.5 : 0); setTarget (tremGain, tOn && ! pan ? 1 - td * 0.5 : 1);
    setTarget (panAmt, tOn && pan ? td : 0);
    const double w = fx.widthOn ? clampd (fx.width, 0, 2) : 1;
    setTarget (wSideL, 0.5 * w); setTarget (wSideR, -0.5 * w);
    const bool ppOn = fx.ppOn;
    double ppT = fx.ppTime; { auto it = BEATS().find (fx.ppSync); if (! fx.ppSync.empty() && fx.ppSync != "free" && it != BEATS().end()) ppT = beat * it->second; }
    setTarget (ppIn, ppOn ? 1 : 0); setTarget (ppWet, ppOn ? clampd (fx.ppMix, 0, 1) : 0);
    setTarget (ppTime, std::min (1.9, std::max (0.01, ppT)));
    setTarget (ppFb, ppOn ? std::min (0.85, std::max (0.0, fx.ppFb)) : 0);
    setTarget (ppLpF, std::max (200.0, fx.ppLp)); setTarget (ppHpF, std::max (20.0, fx.ppHp));
    setTarget (rvHpF, std::max (20.0, fx.rvHp)); setTarget (rvLpF, std::max (500.0, fx.rvLp));
}

void MosaicFx::apply (const FxState& f, double now)
{
    st = f; applied = true;
    gateFx (now);
    const double drv = clampd (f.drive, 0, 1);
    if (driveAmount < 0) { driveCurve = makeCurve (drv); driveAmount = drv; }
    else if (std::abs (drv - (driveRamping ? driveTarget : driveAmount)) > 0.02) { driveTarget = drv; if (! driveRamping) { driveRamping = true; driveNextStep = now + 0.008; } }
    setTarget (driveWet, drv > 0.001 ? 1 : 0); setTarget (driveDry, drv > 0.001 ? 1 - drv * 0.85 : 1);
    const double ph = f.phaserOn ? f.phaserWet : 0;
    setTarget (phaserWet, ph); setTarget (phaserDry, 1 - ph * 0.35);
    setTarget (phaseRate, 0.05 + f.phaserRate * 4); setTarget (phaseDepth, 120 + f.phaserDepth * 900);
    const double bpm = f.bpm;
    double dt = f.delayTime; { auto it = SYNC().find (f.delaySync); if (! f.delaySync.empty() && f.delaySync != "free" && it != SYNC().end()) dt = (60 / bpm) * it->second; }
    setTarget (delayTime, std::min (1.8, std::max (0.01, dt)));
    setTarget (delayFb, std::min (0.85, std::max (0.0, f.delayFb)));
    setTarget (delayWet, std::max (0.0, f.delayWet)); setTarget (delayDry, 1);
    const double mw = f.multiOn ? f.multiWet : 0;
    setTarget (multiWet, std::max (0.0, mw));
    for (auto& b : multi) { setTarget (b.time, std::min (2.2, std::max (0.02, f.multiTime * b.scale))); setTarget (b.fb, f.multiOn ? std::min (0.8, std::max (0.0, f.multiFb)) : 0); }
    setTarget (reverbWet, std::max (0.0, f.reverbWet));
    const double decay = std::min (6.0, std::max (0.4, f.reverbDecay));
    if (! externalImpulses && std::abs (decay - reverbSeconds) > 0.25) loadImpulse (decay);
    const double air = f.shimmerOn && f.shimmerMode != "air" ? f.shimmerLevel : 0;
    const double pitch = f.shimmerPitch; const double mul[3] = { 1, 1.5, 2.01 }, lv[3] = { 0.08, 0.035, 0.02 };
    for (int i = 0; i < 3; ++i) { setTarget (shFreq[(size_t) i], pitch * mul[i]); setTarget (shGain[(size_t) i], air * lv[i]); }
    setTarget (shimmerLfoRate, 0.05 + f.shimmerMotion * 0.7);
    setTarget (shimmerDepth, f.shimmerOn ? air * 0.15 : 0);
    const bool airOn = f.shimmerOn && f.shimmerMode == "air";
    setTarget (airSend, airOn ? f.shimmerLevel * 0.22 : 0); setTarget (airDirect, airOn ? f.shimmerLevel * 0.5 : 0);
    applyBus (60 / bpm);
    if (f.compOn) { setTarget (comp.threshold, f.compThreshold); setTarget (comp.ratio, std::max (1.0, f.compRatio)); setTarget (comp.attack, std::max (0.001, f.compAttack)); setTarget (comp.release, std::max (0.02, f.compRelease)); }
    else { setTarget (comp.threshold, 0); setTarget (comp.ratio, 1); setTarget (comp.attack, 0.01); setTarget (comp.release, 0.1); }
    setTarget (master, std::max (0.0, f.master));
}

void MosaicFx::testForce (const std::string& n, double v)
{
    std::map<std::string, wa::Param*> m { { "driveWet", &driveWet }, { "driveDry", &driveDry }, { "phaserWet", &phaserWet }, { "phaserDry", &phaserDry }, { "delayWet", &delayWet }, { "delayFb", &delayFb },
        { "multiWet", &multiWet }, { "reverbWet", &reverbWet }, { "master", &master }, { "busWet", &busWet }, { "busDry", &busDry }, { "shimmerDepth", &shimmerDepth }, { "delayTime", &delayTime },
        { "compThreshold", &comp.threshold }, { "compRatio", &comp.ratio }, { "compAttack", &comp.attack }, { "compRelease", &comp.release } };
    if (auto it = m.find (n); it != m.end()) it->second->init (v);
}

namespace
{
// Chrome gives each channel of a stereo DelayNode its own kernel, and each kernel pulls the delayTime automation for the quantum
// again, so the timeline advances twice per quantum: the left channel gets the first 128 steps and the right the next 128.
// (The first quantum is still mono: one pull, right channel silent.)
struct DTimes { double L[128], R[128]; };
void pullTimes (wa::Param& p, DTimes& t, int n, bool firstQuantum)
{
    for (int i = 0; i < n; ++i) t.L[i] = p.next();
    if (firstQuantum) { for (int i = 0; i < n; ++i) t.R[i] = t.L[i]; return; }
    p.clock -= n;   // the second pull happens at the same moment in time
    for (int i = 0; i < n; ++i) t.R[i] = p.next();
}
} // namespace

void MosaicFx::process (float* L, float* R, int n, double now)
{
    (void) now;
    // idle-effect timers and the drive ramp (the page runs these on its own timers)
    const double tNow = (double) frame / sr;
    if (! testNoFxGating)
        for (Live* Lv : { &liveShimmer, &liveAir, &liveMulti })
            if (Lv->offAt >= 0 && tNow >= Lv->offAt)
            {
                Lv->offAt = -1;
                const bool still = Lv == &liveShimmer ? (st.shimmerOn && st.shimmerMode != "air" && st.shimmerLevel > 0.001) : Lv == &liveAir ? (st.shimmerOn && st.shimmerMode == "air" && st.shimmerLevel > 0.001) : (st.multiOn && st.multiWet > 0.001);
                if (! still) Lv->on = false;
            }
    while (driveRamping && tNow >= driveNextStep)
    {
        const double d = driveTarget - driveAmount;
        if (std::abs (d) < 0.004) { driveAmount = driveTarget; driveCurve = makeCurve (driveAmount); driveRamping = false; break; }
        driveAmount += d * 0.22; driveCurve = makeCurve (driveAmount); driveNextStep += 0.008;
    }
    comp.quantumStart (n); limiter.quantumStart (n);

    // AIR taps read the past (their delay is at least one quantum inside the reverb loop)
    for (int i = 0; i < n; ++i)
    {
        const long long f = frame + i;
        double outL = 0, outR = 0;
        for (int k = 0; k < 2; ++k)
        {
            double d = 0, wg = 0;
            if (f >= airStart) { const long long idx = (f - airStart + (k ? airN / 2 : 0)) % airN; d = airRamp[(size_t) idx]; wg = airWin[(size_t) idx]; }
            auto& tap = airTap[(size_t) k];
            const int saveW = tap.w; tap.w = (tap.w + i) % tap.len;
            outL += tap.read (0, d) * wg; outR += tap.read (1, d) * wg;
            tap.w = saveW;
        }
        airSendBuf[0][(size_t) i] = (float) outL; airSendBuf[1][(size_t) i] = (float) outR;
    }

    const bool firstQ = frame == 0;
    DTimes tDelay, tPp, tMulti[3];
    pullTimes (delayTime, tDelay, n, firstQ); pullTimes (ppTime, tPp, n, firstQ);
    for (int b = 0; b < 3; ++b) pullTimes (multi[(size_t) b].time, tMulti[b], n, firstQ);
    revBuf.setSize (2, n, false, false, true);
    float fxPreL[128], fxPreR[128];
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) (frame + i) / sr;
        while (! gateEvents.empty() && gateEvents.front().time <= t) { gate.setTarget (gateEvents.front().value, 0.004, sr, (int) ((frame + i) & 127)); gateEvents.erase (gateEvents.begin()); }
        const float g = (float) gate.next();
        float x[2] = { L[i] * g, R[i] * g };
        // drive
        const float dd = (float) driveDry.next(), dw = (float) driveWet.next();
        float ds[2];
        for (int c = 0; c < 2; ++c) { const float wet = drive.tick (c, x[c], driveCurve); ds[c] = x[c] * dd + wet * dw; }
        drive.advance();
        // phaser
        const double lfo = std::sin (2 * wa::PI * phasePh); phasePh += phaseRate.next() / sr; phasePh -= std::floor (phasePh);
        const double depthOut = lfo * phaseDepth.next();
        const float pd = (float) phaserDry.next(), pw = (float) phaserWet.next();
        float ps[2];
        for (int s = 0; s < 4; ++s) stages[(size_t) s].coeffs (1600 + (float) (depthOut * (1 - s * 0.12)), 0.7);
        for (int c = 0; c < 2; ++c) { float y = ds[c]; for (auto& s : stages) y = s.tick (c, y); ps[c] = ds[c] * pd + y * pw; }
        // delay
        const double dT[2] = { tDelay.L[i], tDelay.R[i] }; const float dFb = (float) delayFb.next(), dWet = (float) delayWet.next(), dDry = (float) delayDry.next();
        delayDamp.coeffs (4200, 1);
        float pre[2];
        for (int c = 0; c < 2; ++c) { delay.write (c, ps[c] + delayLoop.read (c)); const float rd = delay.read (c, dT[c]); const float dm = delayDamp.tick (c, rd); delayLoop.write (c, dFb * dm); pre[c] = ps[c] * dDry + dm * dWet; }
        delay.advance(); delayLoop.advance();
        // multi-band echoes
        const float mWet = (float) multiWet.next(); float macc[2] = { 0, 0 };
        for (auto& b : multi)
        {
            const DTimes& bt = tMulti[&b - &multi[0]]; const float bfb = (float) b.fb.next();
            b.f.coeffs (b.f.type == wa::BiquadType::lowpass ? 380 : b.f.type == wa::BiquadType::bandpass ? 1400 : 3200, b.f.type == wa::BiquadType::bandpass ? 0.7 : 1);
            for (int c = 0; c < 2; ++c) { const float in = b.f.tick (c, ps[c]); b.d.write (c, in + b.loop.read (c)); const float rd = b.d.read (c, c ? bt.R[i] : bt.L[i]); b.loop.write (c, rd * bfb); macc[c] += rd * 0.34f; }
            b.d.advance(); b.loop.advance();
        }
        if (liveMulti.on) for (int c = 0; c < 2; ++c) pre[c] += macc[c] * mWet;
        // shimmer (three oscillators → low-pass → swaying level)
        double sh = 0;
        for (int k = 0; k < 3; ++k)
        {
            const double p = shPh[(size_t) k]; sh += (k == 0 ? std::sin (2 * wa::PI * p) : tri (p)) * shGain[(size_t) k].next();
            shPh[(size_t) k] = p + shFreq[(size_t) k].next() / sr; shPh[(size_t) k] -= std::floor (shPh[(size_t) k]);
        }
        shimmerFilter.coeffs (2200, 1);
        float shf = shimmerFilter.tick (0, (float) sh);
        const double shL = std::sin (2 * wa::PI * shLfoPh); shLfoPh += shimmerLfoRate.next() / sr; shLfoPh -= std::floor (shLfoPh);
        shf = (float) (shf * (1 + shL * shimmerDepth.next()));
        if (liveShimmer.on) { pre[0] += shf; pre[1] += shf; }
        fxPreL[i] = pre[0]; fxPreR[i] = pre[1];
        // reverb input (through the reverb tone filters while they are on) + AIR send
        float rin[2] = { pre[0], pre[1] };
        rvHp.coeffs (rvHpF.next(), 1); rvLp.coeffs (rvLpF.next(), 1);
        for (int c = 0; c < 2; ++c) { const float tone = rvLp.tick (c, rvHp.tick (c, pre[c])); if (st.rvToneOn) rin[c] = tone; }
        const float aS = (float) airSend.next();
        if (liveAir.on) { rin[0] += airSendBuf[0][(size_t) i] * aS; rin[1] += airSendBuf[1][(size_t) i] * aS; }
        revBuf.setSample (0, i, rin[0]); revBuf.setSample (1, i, rin[1]);
    }
    { juce::dsp::AudioBlock<float> blk (revBuf.getArrayOfWritePointers(), 2, (size_t) n); conv.process (juce::dsp::ProcessContextReplacing<float> (blk)); }

    for (int i = 0; i < n; ++i)
    {
        const float rv[2] = { revBuf.getSample (0, i), revBuf.getSample (1, i) };
        const float rw = (float) reverbWet.next(), aD = (float) airDirect.next();
        // AIR: the reverb tail through low/high cut into the swept taps (written now, read next quantum)
        airHp.coeffs (140, 1); airLp.coeffs (5200, 1);
        for (int c = 0; c < 2; ++c) { const float a = airLp.tick (c, airHp.tick (c, rv[c])); for (auto& tap : airTap) tap.write (c, a); }
        for (auto& tap : airTap) tap.advance();
        float sum[2] = { fxPreL[i] + rv[0] * rw, fxPreR[i] + rv[1] * rw };
        if (liveAir.on) { sum[0] += airSendBuf[0][(size_t) i] * aD; sum[1] += airSendBuf[1][(size_t) i] * aD; }
        // FX BUS: DJ filter → crush → chorus → tremolo / auto-pan → width, ping-pong as a send
        djLp.coeffs (djLpF.next(), djLpQ.next()); djHp.coeffs (djHpF.next(), djHpQ.next());
        const float cW = (float) crushWet.next(), cD = (float) crushDry.next();
        float cs[2];
        for (int c = 0; c < 2; ++c) { const float y = djHp.tick (c, djLp.tick (c, sum[c])); cs[c] = y * cD + (crushCurve.empty() ? y : wa::shapeCurve (crushCurve, y)) * cW; }
        const double chL = std::sin (2 * wa::PI * chPh); chPh += chRate.next() / sr; chPh -= std::floor (chPh);
        chHp.coeffs (chHpF.next(), 1);
        const double dL = 0.012 + chL * chDepthL.next(), dR = 0.016 + chL * chDepthR.next(); const float chw = (float) chWet.next();
        float chs[2];
        { const float hl = chHp.tick (0, cs[0]), hr = chHp.tick (1, cs[1]); chDelay.write (0, hl); chDelay.write (1, frame == 0 ? 0.f : hr);   // Chrome: the right splitter output is silent for the first quantum
          chs[0] = cs[0] + chDelay.read (0, dL) * chw; chs[1] = cs[1] + chDelay.read (1, dR) * chw; chDelay.advance(); }
        const double tl = std::sin (2 * wa::PI * tremPh); tremPh += tremRate.next() / sr; tremPh -= std::floor (tremPh);
        const float tg = (float) (tremGain.next() + tl * tremAmt.next());
        float tr[2] = { chs[0] * tg, chs[1] * tg };
        wa::stereoPan (tr[0], tr[1], tl * panAmt.next());
        const float mid = (tr[0] + tr[1]) * 0.5f, side = tr[0] * (float) wSideL.next() + tr[1] * (float) wSideR.next();
        float bw[2] = { mid + side, mid - side };
        // ping-pong
        const float pin = (float) ppIn.next(), pfb = (float) ppFb.next(), pwet = (float) ppWet.next();
        ppHp.coeffs (ppHpF.next(), 1); ppLp.coeffs (ppLpF.next(), 1);
        float lOut[2], rOut[2];
        for (int c = 0; c < 2; ++c) { const double pt = c ? tPp.R[i] : tPp.L[i]; lOut[c] = ppL.read (c, pt); rOut[c] = ppR.read (c, pt); }
        for (int c = 0; c < 2; ++c)
        {
            const float in = cs[c] * pin + rOut[c] * pfb; ppL.write (c, ppLp.tick (c, ppHp.tick (c, in)));
            ppR.write (c, ppLoop.read (c)); ppLoop.write (c, lOut[c]);   // the quantum late hop in this loop is left → right
        }
        ppL.advance(); ppR.advance(); ppLoop.advance();
        bw[0] += (lOut[0] + lOut[1]) * 0.5f * pwet; bw[1] += (rOut[0] + rOut[1]) * 0.5f * pwet;
        const float bd = (float) busDry.next(), bwg = (float) busWet.next();
        float o[2] = { sum[0] * bd + bw[0] * bwg, sum[1] * bd + bw[1] * bwg };
        comp.tick (o[0], o[1]); limiter.tick (o[0], o[1]);
        const float m = (float) master.next();
        for (int c = 0; c < 2; ++c) o[c] = wa::shapeCurve (safetyCurve, o[c] * 0.5f) * 2 * m;
        L[i] = o[0]; R[i] = o[1];
    }
    frame += n;
}
} // namespace mosaic
