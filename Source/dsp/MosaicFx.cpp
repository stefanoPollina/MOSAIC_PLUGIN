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
    f.mstOn = x["mstOn"].truthy(); f.mstMono = numD (x["mstMono"], 140); f.mstLowCut = numD (x["mstLowCut"], 30); f.mstAir = numD (x["mstAir"], 2.8); f.mstWarm = numD (x["mstWarm"], 0); f.mstWidth = numD (x["mstWidth"], 1); f.mstTame = numD (x["mstTame"], 0);
    f.fsOn = x["fsOn"].truthy(); f.fsShift = numD (x["fsShift"], 4); f.fsFb = numD (x["fsFb"], 0.25); f.fsMix = numD (x["fsMix"], 0.35);
    f.lofiOn = x["lofiOn"].truthy(); f.lofiGrit = numD (x["lofiGrit"], 0.35); f.lofiBits = numD (x["lofiBits"], 0.4); f.lofiLp = numD (x["lofiLp"], 0.45);
    f.ensOn = x["ensOn"].truthy(); f.ensRate = numD (x["ensRate"], 0.32); f.ensDepth = numD (x["ensDepth"], 0.55); f.ensMix = numD (x["ensMix"], 0.42);
    f.springOn = x["springOn"].truthy(); f.springBoing = numD (x["springBoing"], 0.55); f.springDecay = numD (x["springDecay"], 0.5); f.springMix = numD (x["springMix"], 0.32);
    f.rotOn = x["rotOn"].truthy(); f.rotSpeed = numD (x["rotSpeed"], 0.35); f.rotDepth = numD (x["rotDepth"], 0.65); f.rotMix = numD (x["rotMix"], 0.55);
    f.vowOn = x["vowOn"].truthy(); f.vowPos = numD (x["vowPos"], 0.35); f.vowRate = numD (x["vowRate"], 0.28); f.vowRes = numD (x["vowRes"], 0.55);
    f.apOn = x["apOn"].truthy(); f.apDiv = numD (x["apDiv"], 0.5); f.apDepth = numD (x["apDepth"], 0.7); f.apShape = numD (x["apShape"], 0.25);
    f.bpOn = x["bpOn"].truthy(); f.bpRate = numD (x["bpRate"], 0.58); f.bpFb = numD (x["bpFb"], 0.48); f.bpMix = numD (x["bpMix"], 0.45);
    f.rtOn = x["rtOn"].truthy(); f.rtTune = numD (x["rtTune"], 0.28); f.rtColor = numD (x["rtColor"], 0.45); f.rtMix = numD (x["rtMix"], 0.35);
    f.tcOn = x["tcOn"].truthy(); f.tcSub = numD (x["tcSub"], 0.45); f.tcTube = numD (x["tcTube"], 0.35); f.tcSilk = numD (x["tcSilk"], 0.35);
    f.nyOn = x["nyOn"].truthy(); f.nySmash = numD (x["nySmash"], 0.65); f.nyBody = numD (x["nyBody"], 0.45); f.nyMix = numD (x["nyMix"], 0.3);
    f.tlOn = x["tlOn"].truthy(); f.tlMono = x["tlMono"].truthy(); f.tlAngle = numD (x["tlAngle"], 0.38); f.tlFocus = numD (x["tlFocus"], 0.4); f.tlTrim = numD (x["tlTrim"], 0.5);
    f.sirOn = x["sirOn"].truthy(); f.sirHold = x["sirHold"].truthy(); f.sirDub = numD (x["sirDub"], 0.55); f.sirGrit = numD (x["sirGrit"], 0.45); f.sirMix = numD (x["sirMix"], 0.45);
    f.djOn = x["djOn"].truthy(); f.djKillLow = x["djKillLow"].truthy(); f.djKillMid = x["djKillMid"].truthy(); f.djKillHigh = x["djKillHigh"].truthy();
    f.djHpf = numD (x["djHpf"], 0); f.djLpf = numD (x["djLpf"], 1); f.djCeil = numD (x["djCeil"], 0.79);
    f.dubOn = x["dubOn"].truthy(); f.dubThrow = x["dubThrow"].truthy(); f.dubDiv = numD (x["dubDiv"], 1); f.dubFb = numD (x["dubFb"], 0.58); f.dubMix = numD (x["dubMix"], 0.38);
    f.tapeOn = x["tapeOn"].truthy(); f.tapeSat = numD (x["tapeSat"], 0.3); f.tapeHead = numD (x["tapeHead"], 0.45); f.tapeWow = numD (x["tapeWow"], 0.15);
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
    mstDry.init (1); mstWet.init (0); bq (mstHp, wa::BiquadType::highpass); mstHpF.init (30); bq (mstShelf, wa::BiquadType::highshelf); mstAirDb.init (2.8); bq (mstWarm, wa::BiquadType::lowpass); mstWarmF.init (16000); bq (mstTame, wa::BiquadType::highshelf); mstTameDb.init (0); mstWidthG.init (1);
    { const double A[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 }, B[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
      for (int k = 0; k < 4; ++k) { hilI[k] = HilSec(); hilI[k].a2 = A[k] * A[k]; hilQ[k] = HilSec(); hilQ[k].a2 = B[k] * B[k]; } }
    hilDelay[0] = hilDelay[1] = 0; fsPh = 0; fsHz.init (0); fsDryG.init (1); fsWetG.init (0); fsFbG.init (0); fsFbDelay.prepare (sr, 0.2, true); bq (fsFbLp, wa::BiquadType::lowpass); fsLoop.reset();
    lfLv = lfDr = -1; bq (lfLp, wa::BiquadType::lowpass); lfLpF.init (5500); lfDryG.init (1); lfWetG.init (0);
    bq (spHp, wa::BiquadType::highpass); for (auto& a : spAp) bq (a, wa::BiquadType::allpass); bq (spLp, wa::BiquadType::lowpass); bq (spDamp, wa::BiquadType::lowpass);
    spD.prepare (sr, 0.5, true); spLoop.reset(); spSendG.init (0); spWetG.init (0); spFbG.init (0); spQ.init (4.25);
    enD.prepare (sr, 0.05, false); bq (enLp, wa::BiquadType::lowpass); enPh1 = enPh2 = 0; enRateP.init (0.868); enDepthP.init (0); enDryG.init (1); enWetG.init (0);
    dubD.prepare (sr, 2, true); bq (dubHp, wa::BiquadType::highpass); bq (dubLp, wa::BiquadType::lowpass); dubLoop.reset();
    bq (rotLp, wa::BiquadType::lowpass); rotD.prepare (sr, 0.05, false); rotPh = 0; rotHz.init (1.8); rotDepG.init (0); rotAmG.init (0); rotWetG.init (0); rotDryG.init (1);
    bq (vowF1, wa::BiquadType::bandpass); bq (vowF2, wa::BiquadType::bandpass); bq (vowBody, wa::BiquadType::lowpass); vowPh = 0;
    vowB1.init (450); vowB2.init (1100); vowQ.init (4.5); vowHz.init (0.4); vowM1.init (0); vowM2.init (0); vowWetG.init (0); vowDryG.init (1);
    for (auto* b : { &apLo1, &apLo2 }) bq (*b, wa::BiquadType::lowpass); for (auto* b : { &apHi1, &apHi2 }) bq (*b, wa::BiquadType::highpass); apPh = 0; apHz.init (1); apDepG.init (0); apWetG.init (0); apDryG.init (1);
    for (int i = 0; i < 6; ++i) { bq (bpAp[i], wa::BiquadType::allpass); bpAp[i].coeffs (160 * std::pow (14.0, i / 6.0), 1.6); } bq (bpLp, wa::BiquadType::lowpass); bpLoop.reset(); bpPhase = 0; bpCount = 0; bpFbG.init (0); bpWetG.init (0); bpDryG.init (1);
    bq (rtPre, wa::BiquadType::lowpass); bq (rtPost, wa::BiquadType::lowpass); rtPh = 0; rtHz.init (55); rtDepG.init (0); rtPostF.init (1800); rtWetG.init (0); rtDryG.init (1);
    bq (tcShelf, wa::BiquadType::lowshelf); bq (tcSilk, wa::BiquadType::lowpass); tcShaper.reset(); tcKey = -1; tcSubDb.init (2.7); tcSilkF.init (11000); tcWetG.init (0); tcDryG.init (1);
    bq (nyLp, wa::BiquadType::lowpass); nyLpF.init (3200); nyWetG.init (0);
    nyComp.prepare (sr); nyComp.threshold.init (-25); nyComp.knee.init (6); nyComp.ratio.init (12); nyComp.attack.init (0.008); nyComp.release.init (0.14);
    bq (tlLow, wa::BiquadType::lowshelf); bq (tlHigh, wa::BiquadType::highshelf); bq (tlFocus, wa::BiquadType::peaking);
    tlLowDb.init (0); tlHighDb.init (0); tlFocusDb.init (0); tlWetG.init (0); tlDryG.init (1); tlPassG.init (1); tlSumG.init (0); tlTrimG.init (1);
    bq (srBp, wa::BiquadType::bandpass); bq (srLp, wa::BiquadType::lowpass); srD.prepare (sr, 0.5, true); srKeyL = srKeyD = -1; srBpF.init (790); srFbG.init (0); srWetG.init (0);
    srPh = srWob = 0; srF = 560; srG = 0; srRelT = 0; srHeld = srHoldNow = false;
    for (auto* b : { &isoL1, &isoL2, &isoM3, &isoM4, &isoLp }) bq (*b, wa::BiquadType::lowpass);
    for (auto* b : { &isoM1, &isoM2, &isoH1, &isoH2, &isoHp }) bq (*b, wa::BiquadType::highpass);
    isoDry.init (1); isoBands.init (0); isoLoG.init (1); isoMidG.init (1); isoHiG.init (1); isoHpF.init (10); isoLpF.init (std::min (20000.0, sr * 0.45));
    dubSendG.init (0); dubWetG.init (0); dubFbG.init (0); dubTL.init (0.32); dubTR.init (0.42);
    { const double k = 1 + 0.45 * 5.5; dubCurve.assign (2048, 0.f); for (int i = 0; i < 2048; ++i) dubCurve[(size_t) i] = (float) (std::tanh (((double) i * 2 / 2047 - 1) * k) / k); }
    tpShaper.reset(); tpCurveK = -1; tpPh = 0; bq (tpHead, wa::BiquadType::lowpass); tpHeadF.init (10000); tpWowG.init (0); tpDryG.init (1); tpWetG.init (0); tpWowD.prepare (sr, 0.05, false);
    bq (mstSide1, wa::BiquadType::highpass); bq (mstSide2, wa::BiquadType::highpass); mstMonoF.init (140);
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
    setTarget (mstDry, fx.mstOn ? 0 : 1); setTarget (mstWet, fx.mstOn ? 1 : 0);
    setTarget (mstHpF, clampd (fx.mstLowCut, 20, 65)); setTarget (mstMonoF, clampd (fx.mstMono, 60, 260)); setTarget (mstAirDb, clampd (fx.mstAir, 0, 8)); setTarget (mstWarmF, 16000 - clampd (fx.mstWarm, 0, 1) * 11500); setTarget (mstWidthG, clampd (fx.mstWidth, 0, 2)); setTarget (mstTameDb, -8 * clampd (fx.mstTame, 0, 1));
    { const double mix = clampd (fx.fsMix, 0, 1); setTarget (fsHz, clampd (fx.fsShift, -45, 45)); setTarget (fsDryG, fx.fsOn ? 1 - mix * 0.45 : 1); setTarget (fsWetG, fx.fsOn ? mix : 0); setTarget (fsFbG, fx.fsOn ? clampd (fx.fsFb, 0, 1) * 0.65 : 0); }
    { const double k = 1 + clampd (fx.tapeSat, 0, 1) * 5.5;
      if (std::abs (k - tpCurveK) > 1e-6) { tpCurveK = k; const int n = 2048; tpCurve.assign ((size_t) n, 0.f); for (int i = 0; i < n; ++i) tpCurve[(size_t) i] = (float) (std::tanh (((double) i * 2 / (n - 1) - 1) * k) / k); }
      setTarget (tpHeadF, 4500 + (1 - clampd (fx.tapeHead, 0, 1)) * 11500); setTarget (tpWowG, fx.tapeOn ? clampd (fx.tapeWow, 0, 1) * 0.0012 : 0);
      setTarget (tpDryG, fx.tapeOn ? 0 : 1); setTarget (tpWetG, fx.tapeOn ? 1 : 0); }
    { const double lv = std::max (4.0, std::round (64 - clampd (fx.lofiBits, 0, 1) * 56)), ld = 1 + clampd (fx.lofiGrit, 0, 1) * 3.5;
      if (lv != lfLv || std::abs (ld - lfDr) > 1e-9) { lfLv = lv; lfDr = ld; lfCurve.assign (2048, 0.f); for (int i = 0; i < 2048; ++i) lfCurve[(size_t) i] = (float) (std::round (std::tanh (((double) i * 2 / 2047 - 1) * ld) / ld * lv) / lv); }
      setTarget (lfLpF, 1400 + (1 - clampd (fx.lofiLp, 0, 1)) * 8600); setTarget (lfDryG, fx.lofiOn ? 0 : 1); setTarget (lfWetG, fx.lofiOn ? 1 : 0);
      { const double em = clampd (fx.ensMix, 0, 1); setTarget (enRateP, 0.1 + clampd (fx.ensRate, 0, 1) * 2.4); setTarget (enDepthP, fx.ensOn ? clampd (fx.ensDepth, 0, 1) * 0.0038 : 0);
        setTarget (enWetG, fx.ensOn ? em : 0); setTarget (enDryG, fx.ensOn ? 1 - em * 0.35 : 1); }
      setTarget (spQ, 1.5 + clampd (fx.springBoing, 0, 1) * 5); setTarget (spFbG, fx.springOn ? 0.35 + clampd (fx.springDecay, 0, 1) * 0.52 : 0);
      setTarget (spSendG, fx.springOn ? 1 : 0); setTarget (spWetG, fx.springOn ? clampd (fx.springMix, 0, 1) * 1.2 : 0); }
    { static const double M[5] = { 0.5, 0.75, 1.0, 1.5, 2.0 };
      const double b = 60.0 / std::max (60.0, std::min (220.0, fx.bpm)), tl = std::min (1.9, b * M[(int) std::lround (clampd (fx.dubDiv, 0, 4))]);
      setTarget (dubTL, tl); setTarget (dubTR, std::min (1.9, tl * 1.333));
      const bool live = fx.dubOn || fx.dubThrow; setTarget (dubFbG, fx.dubThrow ? 0.94 : fx.dubOn ? clampd (fx.dubFb, 0, 1) * 0.86 : 0);
      setTarget (dubSendG, live ? 1 : 0); setTarget (dubWetG, live ? clampd (fx.dubMix, 0, 1) * 1.15 : 0); }
    {   // MOTION after ENSEMBLE
      const double beatS = 60.0 / std::max (40.0, std::min (240.0, fx.bpm));
      const double rd = clampd (fx.rotDepth, 0, 1), rm = clampd (fx.rotMix, 0, 1);
      setTarget (rotHz, 0.4 + std::pow (clampd (fx.rotSpeed, 0, 1), 1.8) * 6.8); setTarget (rotDepG, fx.rotOn ? rd * 0.0018 : 0); setTarget (rotAmG, fx.rotOn ? rd * 0.35 : 0);
      setTarget (rotWetG, fx.rotOn ? rm : 0); setTarget (rotDryG, fx.rotOn ? 1 - rm * 0.45 : 1);
      const double vp = clampd (fx.vowPos, 0, 1), vr = clampd (fx.vowRate, 0, 1); const bool manual = vr < 0.04;
      setTarget (vowB1, 280 + vp * 460); setTarget (vowB2, 820 + vp * 1050); setTarget (vowQ, 2.5 + clampd (fx.vowRes, 0, 1) * 7.5);
      setTarget (vowHz, manual ? 0.001 : 0.08 + std::pow (vr, 1.6) * 3.5); setTarget (vowM1, manual ? 0 : 160); setTarget (vowM2, manual ? 0 : 420);
      setTarget (vowWetG, fx.vowOn ? 0.85 : 0); setTarget (vowDryG, fx.vowOn ? 0.25 : 1);
      static const double PER[5] = { 0.5, 0.75, 1, 2, 4 }; const int idx = std::min (4, (int) std::floor (clampd (fx.apDiv, 0, 1) * 4.99));
      apTri = clampd (fx.apShape, 0, 1) > 0.5; setTarget (apHz, 1 / (beatS * PER[idx])); setTarget (apDepG, fx.apOn ? clampd (fx.apDepth, 0, 1) * 0.48 : 0);
      setTarget (apWetG, fx.apOn ? 1 : 0); setTarget (apDryG, fx.apOn ? 0 : 1);
      const double bm = clampd (fx.bpMix, 0, 1); setTarget (bpFbG, fx.bpOn ? clampd (fx.bpFb, 0, 1) * 0.72 : 0); setTarget (bpWetG, fx.bpOn ? bm : 0); setTarget (bpDryG, fx.bpOn ? 1 - bm * 0.35 : 1);
      bpSpeed = fx.bpOn ? (clampd (fx.bpRate, 0, 1) - 0.5) * 1.4 : 0;
      const double rtm = clampd (fx.rtMix, 0, 1); setTarget (rtHz, 2 + std::pow (clampd (fx.rtTune, 0, 1), 2.2) * 218); setTarget (rtDepG, fx.rtOn ? 0.85 : 0);
      setTarget (rtPostF, 900 + clampd (fx.rtColor, 0, 1) * 2100); setTarget (rtWetG, fx.rtOn ? rtm : 0); setTarget (rtDryG, fx.rtOn ? 1 - rtm * 0.4 : 1); }
    {   // MASTER trio
      const double tt = clampd (fx.tcTube, 0, 1);
      if (std::abs (tt - tcKey) > 1e-6) { tcKey = tt; const double k = 1 + tt * 4.2; tcCurve.assign (2048, 0.f); for (int i = 0; i < 2048; ++i) { const double x = (double) i * 2 / 2047 - 1; tcCurve[(size_t) i] = (float) (std::tanh ((x + 0.08 * tt * x * x) * k) / k * (k / (1 + (k - 1) * 0.6))); } }
      setTarget (tcSubDb, clampd (fx.tcSub, 0, 1) * 6); setTarget (tcSilkF, 5500 + (1 - clampd (fx.tcSilk, 0, 1)) * 10500); setTarget (tcWetG, fx.tcOn ? 1 : 0); setTarget (tcDryG, fx.tcOn ? 0 : 1);
      const double smv = clampd (fx.nySmash, 0, 1); setTarget (nyComp.ratio, 4 + smv * 12); setTarget (nyComp.threshold, -16 - smv * 14);
      setTarget (nyLpF, 1600 + (1 - clampd (fx.nyBody, 0, 1)) * 3200); setTarget (nyWetG, fx.nyOn ? clampd (fx.nyMix, 0, 1) * 0.85 : 0);
      const double tilt = (clampd (fx.tlAngle, 0, 1) - 0.5) * 10; const bool mono = fx.tlOn && fx.tlMono;
      setTarget (tlLowDb, -tilt); setTarget (tlHighDb, std::min (3.5, tilt * 0.75)); setTarget (tlFocusDb, clampd (fx.tlFocus, 0, 1) * 4.5);
      setTarget (tlWetG, fx.tlOn ? 1 : 0); setTarget (tlDryG, fx.tlOn ? 0 : 1); setTarget (tlPassG, mono ? 0 : 1); setTarget (tlSumG, mono ? 1 : 0);
      setTarget (tlTrimG, std::pow (10.0, (fx.tlOn ? (clampd (fx.tlTrim, 0, 1) - 0.5) * 12 : 0) / 20)); }
    {   // DUB SIREN
      const double sd = clampd (fx.sirDub, 0, 1), sg = clampd (fx.sirGrit, 0, 1), lv = std::max (4.0, std::round (64 - (0.25 + sg * 0.55) * 56)), dr = 1 + sg * 3.5; const bool live = fx.sirOn || fx.sirHold;
      if (lv != srKeyL || std::abs (dr - srKeyD) > 1e-9) { srKeyL = lv; srKeyD = dr; srCurve.assign (2048, 0.f); for (int i = 0; i < 2048; ++i) srCurve[(size_t) i] = (float) (std::round (std::tanh (((double) i * 2 / 2047 - 1) * dr) / dr * lv) / lv); }
      setTarget (srBpF, 340 + sd * 820); setTarget (srFbG, live ? 0.2 + sd * 0.58 : 0); setTarget (srWetG, live ? clampd (fx.sirMix, 0, 1) * 1.2 : 0);
      srHoldNow = fx.sirHold; }
    {   // DJ ISOLATOR
      const bool dj = fx.djOn, kl = dj && fx.djKillLow, km = dj && fx.djKillMid, kh = dj && fx.djKillHigh, kill = kl || km || kh;
      auto fast = [this] (wa::Param& p, double v) { p.setTarget (v, 0.008, sr); };
      fast (isoDry, kill ? 0 : 1); fast (isoBands, kill ? 1 : 0); fast (isoLoG, kl ? 0 : 1); fast (isoMidG, km ? 0 : 1); fast (isoHiG, kh ? 0 : 1);
      const double hp = clampd (fx.djHpf, 0, 1), lp = clampd (fx.djLpf, 0, 1), top = std::min (20000.0, sr * 0.45);
      setTarget (isoHpF, dj && hp > 0.02 ? 20 + std::pow (hp, 2.2) * 1800 : 10); setTarget (isoLpF, dj && lp < 0.98 ? std::min (top, 280 + lp * lp * 15720) : top);
      setTarget (limiter.threshold, dj ? -6 + clampd (fx.djCeil, 0, 1) * 5.7 : -1.5); }
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
    comp.quantumStart (n); limiter.quantumStart (n); nyComp.quantumStart (n);

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
        comp.tick (o[0], o[1]);
        {   // PSY FREQ SHIFT
            const double hz = fsHz.next(); const float fd = (float) fsDryG.next(), fw = (float) fsWetG.next(), fg = (float) fsFbG.next();
            const float cv = (float) std::cos (2 * wa::PI * fsPh), sv = (float) std::sin (2 * wa::PI * fsPh);
            fsPh += hz / sr; fsPh -= std::floor (fsPh);
            fsFbLp.coeffs (2200, 1);
            float sh[2];
            for (int c = 0; c < 2; ++c)
            {
                const float in = o[c] + fsLoop.read (c) * fg;
                float vi = hilDelay[c]; hilDelay[c] = in; float vq = in;
                for (int k = 0; k < 4; ++k) { vi = hilI[k].tick (c, vi); vq = hilQ[k].tick (c, vq); }
                sh[c] = vi * cv + vq * sv;
                fsFbDelay.write (c, sh[c]);
            }
            for (int c = 0; c < 2; ++c) { fsLoop.write (c, fsFbLp.tick (c, fsFbDelay.read (c, 0.018))); o[c] = o[c] * fd + sh[c] * fw; }
            fsFbDelay.advance(); fsLoop.advance();
        }
        {   // VINYL & LO-FI
            const float ld = (float) lfDryG.next(), lw = (float) lfWetG.next(); lfLp.coeffs (lfLpF.next(), 0.85);
            for (int c = 0; c < 2; ++c) { const float wv = lfLp.tick (c, wa::shapeCurve (lfCurve, o[c])); o[c] = o[c] * ld + wv * lw; }
        }
        {   // DUB SPACE ECHO
            const float ds = (float) dubSendG.next(), dw = (float) dubWetG.next(), dg = (float) dubFbG.next(); const double tl = dubTL.next(), tr = dubTR.next();
            dubHp.coeffs (210, 1); dubLp.coeffs (1850, 1);
            float y[2], sat[2];
            for (int c = 0; c < 2; ++c) { y[c] = dubLp.tick (c, dubHp.tick (c, dubD.read (c, c ? tr : tl))); sat[c] = wa::shapeCurve (dubCurve, y[c]) * dg; }
            dubD.write (0, o[0] * ds + dubLoop.read (1)); dubD.write (1, o[1] * ds + sat[0]); dubLoop.write (1, sat[1]);
            dubD.advance(); dubLoop.advance();
            for (int c = 0; c < 2; ++c) o[c] += y[c] * dw;
        }
        {   // SPRING TANK
            const float ss = (float) spSendG.next(), sw = (float) spWetG.next(), sg = (float) spFbG.next(); const double q = spQ.next();
            spHp.coeffs (240, 1); spAp[0].coeffs (620, q); spAp[1].coeffs (1180, q * 1.1); spAp[2].coeffs (1950, q * 1.2); spLp.coeffs (2300, 1); spDamp.coeffs (3000, 1);
            float y[2], x[2];
            for (int c = 0; c < 2; ++c) { float v = spHp.tick (c, o[c] * ss); for (auto& a : spAp) v = a.tick (c, v); x[c] = spLp.tick (c, v); }
            y[0] = spD.read (0, 0.041); y[1] = spD.read (1, 0.053);
            const float toR = spDamp.tick (0, y[0]) * sg, toL = spDamp.tick (1, y[1]) * sg;
            spD.write (0, x[0] + spLoop.read (0)); spD.write (1, x[1] + toR); spLoop.write (0, toL);
            spD.advance(); spLoop.advance();
            for (int c = 0; c < 2; ++c) o[c] += y[c] * sw;
        }
        {   // ENSEMBLE: sine on the left line, triangle at 1.47x on the right with opposite depth, both rolled off at 3.4 kHz
            const double r = enRateP.next(), d = enDepthP.next(); const float ed = (float) enDryG.next(), ew = (float) enWetG.next();
            const double l1 = std::sin (2 * wa::PI * enPh1), l2 = 2 / wa::PI * std::asin (std::sin (2 * wa::PI * enPh2));
            enPh1 += r / sr; enPh1 -= std::floor (enPh1); enPh2 += r * 1.47 / sr; enPh2 -= std::floor (enPh2);
            enLp.coeffs (3400, 1); enD.write (0, o[0]); enD.write (1, o[1]);
            const float wl = enLp.tick (0, enD.read (0, 0.008 + d * l1)), wr = enLp.tick (1, enD.read (1, 0.011 - d * l2)); enD.advance();
            o[0] = o[0] * ed + wl * ew; o[1] = o[1] * ed + wr * ew;
        }
        {   // VINTAGE ROTARY
            const double l = std::sin (2 * wa::PI * rotPh), dep = rotDepG.next(), am = rotAmG.next(); rotPh += rotHz.next() / sr; rotPh -= std::floor (rotPh);
            const float rw = (float) rotWetG.next(), rdr = (float) rotDryG.next(); rotLp.coeffs (3200, 1);
            for (int c = 0; c < 2; ++c) rotD.write (c, rotLp.tick (c, o[c]));
            const float wl = rotD.read (0, 0.006 + dep * l) * (float) (1 + am * l), wr = rotD.read (1, 0.006 - dep * l) * (float) (1 - am * l); rotD.advance();
            o[0] = o[0] * rdr + wl * rw; o[1] = o[1] * rdr + wr * rw;
        }
        {   // VOWEL MORPH
            const double l = std::sin (2 * wa::PI * vowPh); vowPh += vowHz.next() / sr; vowPh -= std::floor (vowPh);
            const double q = vowQ.next(); vowF1.coeffs (std::max (20.0, vowB1.next() + vowM1.next() * l), q); vowF2.coeffs (std::max (20.0, vowB2.next() + vowM2.next() * l), q * 1.1); vowBody.coeffs (900, 1);
            const float vw = (float) vowWetG.next(), vd = (float) vowDryG.next();
            for (int c = 0; c < 2; ++c) o[c] = o[c] * vd + (vowF1.tick (c, o[c]) + vowF2.tick (c, o[c]) + vowBody.tick (c, o[c])) * vw;
        }
        {   // STEREO AUTO-PAN (lows below 160 Hz untouched)
            const double w = apTri ? (apPh < 0.25 ? 4 * apPh : apPh < 0.75 ? 2 - 4 * apPh : 4 * apPh - 4) : std::sin (2 * wa::PI * apPh); apPh += apHz.next() / sr; apPh -= std::floor (apPh);
            const double dep = apDepG.next(); const float aw = (float) apWetG.next(), ad = (float) apDryG.next();
            for (auto* b : { &apLo1, &apLo2, &apHi1, &apHi2 }) b->coeffs (160, -3.0103);
            for (int c = 0; c < 2; ++c) { const float lo = apLo2.tick (c, apLo1.tick (c, o[c])), hi = apHi2.tick (c, apHi1.tick (c, o[c]));
                o[c] = o[c] * ad + (lo + hi * (float) (c ? 1 - dep * w : 1 + dep * w)) * aw; }
        }
        {   // BARBERPOLE PHASER
            bpPhase += bpSpeed / sr; bpPhase -= std::floor (bpPhase);
            if (++bpCount >= 16) { bpCount = 0; for (int i = 0; i < 6; ++i) { double p = bpPhase + i / 6.0; p -= std::floor (p); bpAp[i].coeffs (160 * std::pow (14.0, p), 1.6); } }
            bpLp.coeffs (2800, 1); const float fb = (float) bpFbG.next(), bw = (float) bpWetG.next(), bd = (float) bpDryG.next();
            for (int c = 0; c < 2; ++c) { float v = o[c] + bpLoop.read (c) * fb; for (auto& a : bpAp) v = a.tick (c, v); const float y = bpLp.tick (c, v); bpLoop.write (c, y); o[c] = o[c] * bd + y * bw; }
            bpLoop.advance();
        }
        {   // WARM RING TREM
            const double car = std::sin (2 * wa::PI * rtPh); rtPh += rtHz.next() / sr; rtPh -= std::floor (rtPh);
            const float g = (float) (rtDepG.next() * car), rw = (float) rtWetG.next(), rdr = (float) rtDryG.next(); rtPre.coeffs (2600, 1); rtPost.coeffs (rtPostF.next(), 1);
            for (int c = 0; c < 2; ++c) o[c] = o[c] * rdr + rtPost.tick (c, rtPre.tick (c, o[c]) * g) * rw;
        }
        {   // DUB SIREN
            const float bw = (float) srWetG.next(), fb = (float) srFbG.next(); srBp.coeffs (srBpF.next(), 1.8); srLp.coeffs (2600, 1);
            if (srHoldNow != srHeld) { srHeld = srHoldNow; if (srHeld) srF = 560; else srRelT = 0.2; }
            const double kF = 1 - std::exp (-1 / (0.35 * sr)), kUp = 1 - std::exp (-1 / (0.015 * sr)), kDn = 1 - std::exp (-1 / (0.04 * sr)), kBack = 1 - std::exp (-1 / (0.02 * sr));
            if (srHeld) { srF += (190 - srF) * kF; srG += (0.28 - srG) * kUp; }
            else { srG += (0 - srG) * kDn; if (srRelT > 0) srRelT -= 1 / sr; else srF += (560 - srF) * kBack; }
            float siren = 0;
            if (srG > 1e-6) { const double wob = 2 * srWob - 1, f = std::max (1.0, srF + 140 * wob); srPh += f / sr; srPh -= std::floor (srPh);
                siren = (float) (srG * (srPh < 0.5 ? 4 * srPh - 1 : 3 - 4 * srPh)); }
            srWob += 5.5 / sr; srWob -= std::floor (srWob);
            if (bw > 0 || srG > 1e-6)
                for (int c = 0; c < 2; ++c) { const float slap = srD.read (c, 0.145), lo = srLp.tick (c, wa::shapeCurve (srCurve, srBp.tick (c, o[c])) + slap * fb);
                    srD.write (c, lo + siren); o[c] += (lo + slap + siren) * bw; }
            else for (int c = 0; c < 2; ++c) srD.write (c, 0.f);
            srD.advance();
        }
        {   // ANALOG TAPE
            const float td = (float) tpDryG.next(), tw = (float) tpWetG.next(); const double wow = tpWowG.next();
            tpHead.coeffs (tpHeadF.next(), -3.0103);
            const double dt = 0.008 + wow * std::sin (2 * wa::PI * tpPh); tpPh += 1.4 / sr; tpPh -= std::floor (tpPh);
            float w[2]; for (int c = 0; c < 2; ++c) { tpWowD.write (c, tpHead.tick (c, tpShaper.tick (c, o[c], tpCurve))); w[c] = tpWowD.read (c, dt); }
            tpShaper.advance(); tpWowD.advance();
            for (int c = 0; c < 2; ++c) o[c] = o[c] * td + w[c] * tw;
        }
        {
            const float md = (float) mstDry.next(), mw = (float) mstWet.next();
            const double hpF = mstHpF.next(), monoF = mstMonoF.next(), airDb = mstAirDb.next();
            mstHp.coeffs (hpF, -3.0103); mstShelf.coeffs (8500, airDb); mstWarm.coeffs (mstWarmF.next(), -3.0103); mstTame.coeffs (3200, mstTameDb.next()); mstSide1.coeffs (monoF, -3.0103); mstSide2.coeffs (monoF, -3.0103);
            float h[2]; for (int c = 0; c < 2; ++c) h[c] = mstTame.tick (c, mstWarm.tick (c, mstShelf.tick (c, mstHp.tick (c, o[c]))));
            const float mid = (h[0] + h[1]) * 0.5f, side = mstSide2.tick (0, mstSide1.tick (0, (h[0] - h[1]) * 0.5f)) * (float) mstWidthG.next();
            if (mw > 0) { o[0] = o[0] * md + (mid + side) * mw; o[1] = o[1] * md + (mid - side) * mw; }
        }
        {   // TUBE CONSOLE
            tcShelf.coeffs (55, tcSubDb.next()); tcSilk.coeffs (tcSilkF.next(), 0.55); const float tw = (float) tcWetG.next(), td = (float) tcDryG.next();
            float y[2]; for (int c = 0; c < 2; ++c) y[c] = tcSilk.tick (c, tcShaper.tick (c, tcShelf.tick (c, o[c]), tcCurve)); tcShaper.advance();
            for (int c = 0; c < 2; ++c) o[c] = o[c] * td + y[c] * tw;
        }
        {   // NY PARALLEL GLUE
            nyLp.coeffs (nyLpF.next(), 1); float w0 = nyLp.tick (0, o[0]), w1 = nyLp.tick (1, o[1]); nyComp.tick (w0, w1);
            const float nw = (float) nyWetG.next(); o[0] += w0 * nw; o[1] += w1 * nw;
        }
        {   // MASTER TILT & MONO
            tlLow.coeffs (320, tlLowDb.next()); tlHigh.coeffs (1800, tlHighDb.next()); tlFocus.peakDb = tlFocusDb.next(); tlFocus.coeffs (95, 0.9);
            const float tw = (float) tlWetG.next(), td = (float) tlDryG.next(), pass = (float) tlPassG.next(), sum = (float) tlSumG.next(), trim = (float) tlTrimG.next();
            for (int c = 0; c < 2; ++c) o[c] = o[c] * td + tlFocus.tick (c, tlHigh.tick (c, tlLow.tick (c, o[c]))) * tw;
            const float mid = (o[0] + o[1]) * 0.5f; o[0] = (o[0] * pass + mid * sum) * trim; o[1] = (o[1] * pass + mid * sum) * trim;
        }
        {   // DJ ISOLATOR
            const float idr = (float) isoDry.next(), ib = (float) isoBands.next(), gl = (float) isoLoG.next(), gm = (float) isoMidG.next(), gh = (float) isoHiG.next();
            for (auto* b : { &isoL1, &isoL2, &isoM1, &isoM2 }) b->coeffs (180, -3.0103);
            for (auto* b : { &isoM3, &isoM4, &isoH1, &isoH2 }) b->coeffs (2800, -3.0103);
            isoHp.coeffs (isoHpF.next(), 2.92); isoLp.coeffs (isoLpF.next(), 0.83);
            for (int c = 0; c < 2; ++c) { const float x = o[c];
                const float lo = isoL2.tick (c, isoL1.tick (c, x)), mid = isoM4.tick (c, isoM3.tick (c, isoM2.tick (c, isoM1.tick (c, x)))), hi = isoH2.tick (c, isoH1.tick (c, x));
                o[c] = isoLp.tick (c, isoHp.tick (c, x * idr + (lo * gl + mid * gm + hi * gh) * ib)); }
        }
        limiter.tick (o[0], o[1]);
        const float m = (float) master.next();
        for (int c = 0; c < 2; ++c) o[c] = wa::shapeCurve (safetyCurve, o[c] * 0.5f) * 2 * m;
        L[i] = o[0]; R[i] = o[1];
    }
    frame += n;
}
} // namespace mosaic
