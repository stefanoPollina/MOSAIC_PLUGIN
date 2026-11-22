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
    bool mstOn = false; double mstMono = 140, mstLowCut = 30, mstAir = 2.8, mstWarm = 0;   // PSY MASTER: sub mono below mstMono Hz, low cut, air shelf dB
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
    wa::Biquad mstHp, mstShelf, mstWarm, mstSide1, mstSide2; wa::Param mstDry, mstWet, mstHpF, mstMonoF, mstAirDb, mstWarmF;
    long long frame = 0;
    void setTarget (wa::Param& p, double v) { p.setTarget (v, 0.03, sr); }
    void applyBus (double beat);
    void gateFx (double now);
    std::vector<float> makeCurve (double amount) const;
};
} // namespace mosaic
