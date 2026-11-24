#pragma once
/* Continuum Mosaic's effects chain (the page's Engine.buildFx / applyFx / applyBus), rebuilt node for node:
   gate → drive → phaser → delay + multi-band echoes → reverb (shimmer, AIR) → FX BUS (DJ filter, crush, chorus,
   tremolo/auto-pan, width, ping-pong) → compressor → limiter → output safety → master.
   Processes in render quanta of up to 128 frames, as the browser does (feedback loops run one quantum behind). */
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "MiniJson.h"
#include "WebAudioParts.h"
#include "GranularCloud.h"
#include <juce_dsp/juce_dsp.h>

namespace mosaic
{
struct FxState
{
    double drive = 0, phaserWet = 0.28, phaserRate = 0.22, phaserDepth = 0.45, delayTime = 0.32, delayFb = 0.32, delayWet = 0;
    bool phaserOn = false, multiOn = false; std::string delaySync = "free";
    double multiTime = 0.31, multiFb = 0.34, multiWet = 0, reverbWet = 0, reverbDecay = 2;
    bool shimmerOn = false; std::string shimmerMode; double shimmerLevel = 0, shimmerPitch = 196, shimmerMotion = 0;
    double busFilter = 0, busFilterRes = 0.2; bool chorusOn = false; double chorusRate = 0.35, chorusDepth = 0.2, chorusMix = 0.15, chorusHp = 250;
    bool ppOn = false; std::string ppSync = "3/16"; double ppTime = 0.28, ppFb = 0.35, ppMix = 0.12, ppLp = 3500, ppHp = 300;
    bool crushOn = false; double crushBits = 8, crushMix = 0.5; bool tremOn = false; std::string tremMode = "trem", tremSync = "1/8"; double tremDepth = 0.5;
    bool widthOn = false; double width = 1.3; bool rvToneOn = false; double rvHp = 180, rvLp = 9000;
    bool compOn = false; double compThreshold = -18, compRatio = 3, compAttack = 0.01, compRelease = 0.2;
    bool mstOn = false; double mstMono = 140, mstLowCut = 30, mstAir = 2.8, mstWarm = 0, mstWidth = 1, mstTame = 0;
    bool fsOn = false; double fsShift = 4, fsFb = 0.25, fsMix = 0.35;          // PSY FREQ SHIFT (single-sideband, Hz)
    bool tapeOn = false; double tapeSat = 0.3, tapeHead = 0.45, tapeWow = 0.15; // ANALOG TAPE
    bool lofiOn = false; double lofiGrit = 0.35, lofiBits = 0.4, lofiLp = 0.45;              // VINYL & LO-FI
    bool ensOn = false; double ensRate = 0.32, ensDepth = 0.55, ensMix = 0.42;  // ENSEMBLE
    bool springOn = false; double springBoing = 0.55, springDecay = 0.5, springMix = 0.32;  // SPRING TANK
    bool gcOn = false, gcFreeze = false, gcRev = false; double gcMix = 0.35, gcRegen = 0.38, gcSize = 180, gcDens = 14, gcPitch = 2;   // GRANULAR CLOUD
    bool dubOn = false, dubThrow = false; double dubDiv = 1, dubFb = 0.58, dubMix = 0.38;   // DUB SPACE ECHO   // PSY MASTER: sub mono below mstMono Hz, low cut, air shelf dB
    bool rotOn = false; double rotSpeed = 0.35, rotDepth = 0.65, rotMix = 0.55;      // VINTAGE ROTARY
    bool vowOn = false; double vowPos = 0.35, vowRate = 0.28, vowRes = 0.55;          // VOWEL MORPH
    bool apOn = false; double apDiv = 0.5, apDepth = 0.7, apShape = 0.25;             // STEREO AUTO-PAN
    bool bpOn = false; double bpRate = 0.58, bpFb = 0.48, bpMix = 0.45;               // BARBERPOLE PHASER
    bool rtOn = false; double rtTune = 0.28, rtColor = 0.45, rtMix = 0.35;            // WARM RING TREM
    bool tcOn = false; double tcSub = 0.45, tcTube = 0.35, tcSilk = 0.35;             // TUBE CONSOLE
    bool nyOn = false; double nySmash = 0.65, nyBody = 0.45, nyMix = 0.3;             // NY PARALLEL GLUE
    bool tlOn = false, tlMono = false; double tlAngle = 0.38, tlFocus = 0.4, tlTrim = 0.5;   // MASTER TILT & MONO
    bool sirOn = false, sirHold = false; double sirDub = 0.55, sirGrit = 0.45, sirMix = 0.45, sirType = 0;    // DUB SIREN
    bool djOn = false, djKillLow = false, djKillMid = false, djKillHigh = false; double djHpf = 0, djLpf = 1, djCeil = 0.79;  // DJ ISOLATOR
    double master = 0.8, bpm = 120;
    static FxState fromJson (const json::Value& fullState);   // reads state.fx, state.master, state.seq.bpm as applyFx does
};

class MosaicFx
{
public:
    void prepare (double sampleRate, uint32_t seed);   // builds the graph (as buildFx) at time 0
    void apply (const FxState& f, double now);         // as applyFx at time `now` (seconds)
    void setGate (double value, double time, bool cancel);
    void process (float* L, float* R, int n, double now);   // n <= 128
    void loadImpulse (double seconds);
    // the plugin builds new impulses off the audio thread and hands them in (JUCE loads and crossfades them in the background)
    bool externalImpulses = false;
    static juce::AudioBuffer<float> makeImpulse (double sampleRate, double seconds, const std::function<double()>& random);
    void installImpulse (juce::AudioBuffer<float>&& ir, double seconds);
    static double decayFor (const FxState& f) { return std::min (6.0, std::max (0.4, f.reverbDecay)); }
    void setImpulseRng (std::function<double()> r) { irRandom = std::move (r); }
    bool testNoFxGating = false;
    void testForce (const std::string& name, double v);   // parity tests: pin one parameter to a value at once                       // parity tests: keep idle effects connected (browser timers don't fire in offline renders)

private:
    double sr = 48000; std::function<double()> irRandom; uint32_t rngState = 1;
    FxState st; bool applied = false;
    wa::Param gate; struct GateEv { double time, value; bool cancel; }; std::vector<GateEv> gateEvents;
    // drive
    std::vector<float> driveCurve; double driveAmount = -1, driveTarget = 0, driveNextStep = 0; bool driveRamping = false;
    wa::Shaper2x drive; wa::Param driveDry, driveWet;
    // phaser
    wa::Param phaserDry, phaserWet, phaseRate, phaseDepth; double phasePh = 0; std::array<wa::Biquad, 4> stages;
    // delay + multi
    wa::Delay delay; wa::Lag128 delayLoop; wa::Biquad delayDamp; wa::Param delayTime, delayFb, delayWet, delayDry;
    struct Band { wa::Biquad f; wa::Delay d; wa::Lag128 loop; wa::Param time, fb; double scale; };
    std::array<Band, 3> multi; wa::Param multiWet;
    // reverb
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 128 } }; double reverbSeconds = 0;
    wa::Param reverbWet; juce::AudioBuffer<float> revBuf;
    // shimmer
    std::array<double, 3> shPh {}; std::array<wa::Param, 3> shFreq, shGain; wa::Biquad shimmerFilter; wa::Param shimmerLfoRate, shimmerDepth; double shLfoPh = 0;
    // air
    wa::Biquad airHp, airLp; wa::Param airSend, airDirect; std::array<wa::Delay, 2> airTap; int airN = 0; long long airStart = 0; std::vector<float> airRamp, airWin;
    std::vector<float> airSendBuf[2];
    // gating of idle effects (timers, as the page's gateFx)
    struct Live { bool on = true; double offAt = -1; }; Live liveShimmer, liveAir, liveMulti;
    // bus
    wa::Param busDry, busWet, djLpF, djHpF, djLpQ, djHpQ, crushWet, crushDry, chRate, chDepthL, chDepthR, chWet, chHpF, tremRate, tremAmt, tremGain, panAmt, wSideL, wSideR,
              ppIn, ppWet, ppTime, ppFb, ppLpF, ppHpF, rvHpF, rvLpF;
    bool busOn = false; wa::Biquad djLp, djHp, chHp, ppHp, ppLp, rvHp, rvLp; std::vector<float> crushCurve; int crushBits = 0;
    double chPh = 0, tremPh = 0; wa::Delay chDelay; wa::Delay ppL, ppR; wa::Lag128 ppLoop; float ppRout = 0;
    // dynamics + output
    wa::Compressor comp, limiter; std::vector<float> safetyCurve; wa::Param master;
    // PSY MASTER (between compressor and limiter): low cut -> air shelf -> mid/side with the side high-passed below the mono frequency
    wa::Biquad mstHp, mstShelf, mstWarm, mstTame, mstSide1, mstSide2; wa::Param mstDry, mstWet, mstHpF, mstMonoF, mstAirDb, mstWarmF, mstWidthG, mstTameDb;
    // PSY FREQ SHIFT: two 4-section allpass chains (y = a²x - x[n-2] + a²y[n-2]) make a 90-degree pair, mixed with a cosine/sine carrier;
    // an 18 ms feedback loop low-passed at 2.2 kHz (a cycle, so it runs a quantum late as in Chrome)
    struct HilSec { double a2 = 0, x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
                    float tick (int c, float x) { const double y = a2 * x - x2[c] + a2 * y2[c]; x2[c] = x1[c]; x1[c] = x; y2[c] = y1[c]; y1[c] = y; return (float) y; } };
    HilSec hilI[4], hilQ[4]; float hilDelay[2] {}; double fsPh = 0; wa::Param fsHz, fsDryG, fsWetG, fsFbG; wa::Delay fsFbDelay; wa::Biquad fsFbLp; wa::Lag128 fsLoop;
    // ANALOG TAPE: tanh/k curve through Chrome's 2x oversampled shaper -> head low-pass -> 8 ms delay with slow wow
    // DUB SPACE ECHO: one stereo delay (channel 0 = left line, 1 = right line, read at 4:3), each repeat high-passed at 210 Hz, low-passed at 1.85 kHz,
    // tape-saturated at unity gain, then fed to the other line (the cross hop runs a quantum late, as Chrome runs a cycle)
    std::vector<float> lfCurve; double lfLv = -1, lfDr = -1; wa::Biquad lfLp; wa::Param lfLpF, lfDryG, lfWetG;
    wa::Biquad spHp, spAp[3], spLp, spDamp; wa::Delay spD; wa::Lag128 spLoop; wa::Param spSendG, spWetG, spFbG, spQ;
    GranularCloud gc;   // GRANULAR CLOUD (after SPRING TANK)
    wa::Delay enD; wa::Biquad enLp; double enPh1 = 0, enPh2 = 0; wa::Param enRateP, enDepthP, enDryG, enWetG;   // ENSEMBLE
    wa::Delay dubD; wa::Biquad dubHp, dubLp; std::vector<float> dubCurve; wa::Lag128 dubLoop; wa::Param dubSendG, dubWetG, dubFbG, dubTL, dubTR;
    wa::Shaper2x tpShaper; std::vector<float> tpCurve; double tpCurveK = -1, tpPh = 0; wa::Biquad tpHead; wa::Param tpHeadF, tpWowG, tpDryG, tpWetG; wa::Delay tpWowD;
    // DUB SIREN: band-pass -> bit grit -> 2.6 kHz low-pass with a 145 ms slapback fed back into it, plus a held siren through a 2.2 kHz low-pass:
    // a square tone, pitch cycled by WHOOP (rising 4 Hz saw sweeps), TRILL (8.5 Hz two-tone) or WAIL (3 Hz triangle up-and-down)
    wa::Biquad srBp, srLp; wa::Delay srD; std::vector<float> srCurve; double srKeyL = -1, srKeyD = -1; wa::Param srBpF, srFbG, srWetG;
    double srPh = 0, srWob = 0, srF = 560, srG = 0, srRelT = 0; bool srHeld = false, srHoldNow = false; int srMode = 0, srHeldMode = 0; wa::Biquad srSirLp;
    // DJ ISOLATOR (after PSY MASTER, before the limiter): crossfade onto a 3-band LR4 split when a band is killed, then DJ HPF / LPF
    wa::Biquad isoL1, isoL2, isoM1, isoM2, isoM3, isoM4, isoH1, isoH2, isoHp, isoLp; wa::Param isoDry, isoBands, isoLoG, isoMidG, isoHiG, isoHpF, isoLpF;
    // MOTION after ENSEMBLE: rotary -> vowel morph -> auto-pan -> barberpole -> ring trem
    wa::Biquad rotLp; wa::Delay rotD; double rotPh = 0; wa::Param rotHz, rotDepG, rotAmG, rotWetG, rotDryG;
    wa::Biquad vowF1, vowF2, vowBody; double vowPh = 0; wa::Param vowB1, vowB2, vowQ, vowHz, vowM1, vowM2, vowWetG, vowDryG;
    wa::Biquad apLo1, apLo2, apHi1, apHi2; double apPh = 0; bool apTri = false; wa::Param apHz, apDepG, apWetG, apDryG;
    wa::Biquad bpAp[6], bpLp; wa::Lag128 bpLoop; double bpPhase = 0, bpSpeed = 0; int bpCount = 0; wa::Param bpFbG, bpWetG, bpDryG;
    wa::Biquad rtPre, rtPost; double rtPh = 0; wa::Param rtHz, rtDepG, rtPostF, rtWetG, rtDryG;
    // MASTER between PSY MASTER and DJ ISOLATOR: tube console -> NY parallel glue -> tilt & mono
    wa::Biquad tcShelf, tcSilk; wa::Shaper2x tcShaper; std::vector<float> tcCurve; double tcKey = -1; wa::Param tcSubDb, tcSilkF, tcWetG, tcDryG;
    wa::Biquad nyLp; wa::Compressor nyComp; wa::Param nyLpF, nyWetG;
    wa::Biquad tlLow, tlHigh, tlFocus; wa::Param tlLowDb, tlHighDb, tlFocusDb, tlWetG, tlDryG, tlPassG, tlSumG, tlTrimG;
    long long frame = 0;
    void setTarget (wa::Param& p, double v) { p.setTarget (v, 0.03, sr); }
    void applyBus (double beat);
    void gateFx (double now);
    std::vector<float> makeCurve (double amount) const;
};
} // namespace mosaic
