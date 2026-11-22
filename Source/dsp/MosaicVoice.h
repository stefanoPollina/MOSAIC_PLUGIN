#pragma once
/* Continuum Mosaic's polyphonic voice engine (the page's 'mosaic-voice' AudioWorklet), ported to C++.
   Two layers, three oscillators each, multimode filter (LP/HP/BP/notch/comb/formant/drip), amp and filter
   envelopes, FM / phase distortion, 16 LFOs with step sequencers, the LFO-SEQ umbrella sequencer and the mod matrix.
   It takes the same messages as the worklet (state, on/off with times, bend, mod, sustain, panic, clock) and is
   checked sample-for-sample against it (same seeded random numbers). The render loop does not allocate. */
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include "MosaicState.h"

namespace mosaic
{
struct Rng   // mulberry32, identical to the JavaScript used in the parity tests
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) noexcept { s = v; }
    double next() noexcept { s += 0x6D2B79F5u; uint32_t t = (s ^ (s >> 15)) * (1u | s); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return double (t ^ (t >> 14)) / 4294967296.0; }
};

enum class EvType : int { on, off, bend, mod, sustain };
struct Event
{
    EvType type = EvType::on; uint64_t id = 0; int note = 60; double vel = NaN, glide = NaN, value = 0, time = 0; int layer = 0; bool legato = false, on = false;
};

enum class Stage : int { idle, del, attack, hold, decay, sustain, release };

class VoiceEngine
{
public:
    static constexpr int MAX_VOICES = 20;
    VoiceEngine();
    void seed (uint32_t s);                       // re-seeds and rebuilds the voices, as a fresh worklet would
    void setState (std::shared_ptr<const State> s);   // audio thread: swap in a parsed state (built elsewhere)
    void post (const Event& e);                   // audio thread: queue an event (as port.postMessage)
    void panic();
    void setClockAnchor (double a) { l8anchor = a; }
    // render frames starting at absolute time `now` (seconds); sr = sample rate
    void process (float* L, float* R, int frames, double now, double sr);
    int activeVoices() const { return activeCount; }
    void scopeCopy (float* out160) const;         // the last 160 samples, oldest first (the meter's waveform)
    static uint64_t hashId (const char* s, size_t n);

private:
    struct Voice
    {
        bool active = false; uint64_t id = 0; int layer = 0, note = 60; double vel = 0.8;
        bool gate = false, sustainHold = false, fading = false, hasPend = false;
        int pendNote = 60; double pendVel = 0.8; int pendLayer = 0; double pendGlide = 0;
        long long age = 0; double freq = 440, target = 440, glide = 0, fc = 1000, amp = 0, fenv = 0;
        Stage ampStage = Stage::idle, filtStage = Stage::idle; double ampWait = 0, filtWait = 0;
        double phase[3] {}, uni[4] {}, fmPhase = 0;
        double lfo[16] {}, sh[16] {}, lsm[16] {}; uint8_t luni[16] {}; double lv[16] {}; double gs = 1; double ugp[16] {};
        uint8_t dtrig[16] {}; double dstep[16] {};
        double dripT0 = 0, dripEnv = 0; int dripKick = 0; double drift = 0, driftPhase = 0;
        double ic1 = 0, ic2 = 0, ic1b = 0, ic2b = 0, fic1[3] {}, fic2[3] {}, fcoef[9] {}, pk[3] {};
        bool wrap0 = false; double s0 = 0, s2prev = 0, subPh = 0, subPh2 = 0, subPh3 = 0; bool wrap2 = false;
        double nw[768] {}, npk[768] {};
        float comb[4096] {}; int combPos = 0; double combDamp = 0; int lastType = -100;
        double fg = 0, fa1 = 0, fa2 = 0, fa3 = 0, fk = 1; int ftick = 0;
        double dripPh = 0, dripWait = 0, dripAge = 0, dripHit = 0, dripSize = 0.5, dripJit = 0, dripRatio = 0, dripAmp = 0, dripRate = 1;
        float dripDelay[32] {}; int dripDPos = 0; int splashN = 0;
        double stW = 0, spW = 0, dripKind = 1, dGuard = 1, dgLvl = 0;
    };
    void makeVoice (Voice& v);
    void noisePattern (double* a, bool pink);
    void silence (Voice& v);
    Voice* findVoice (uint64_t id);
    Voice& alloc();
    void arm (Voice& v, int note, double vel, int layer, double glide, bool legato);
    void queueRetrigger (Voice& v, const Event& m, int layer);
    const Layer* layerState (int i) const;
    void applyEvent (const Event& m);
    void cacheRatios (const State& s);
    double envShapeCoef (const Env& p, double time, double sr, double distance) const;
    void stepEnv (Stage stage, double value, const Env& p, bool gate, double sr, double wait);
    double sourceValue (const ModSrc& src, const Voice& v, double l1, double l2, double wheel, const double* lv) const;

    std::array<std::unique_ptr<Voice>, MAX_VOICES> voices;
    Rng rng;
    int l8VoiceSeed = 0;
    std::shared_ptr<const State> state;
    std::array<Event, 96> ev {}, due {}; int evCount = 0;
    double bend = 0, bendTarget = 0, wheel = 0, wheelTarget = 0; bool sustain = false;
    double dcInL = 0, dcOutL = 0, dcInR = 0, dcOutR = 0;
    float scope[160] {}; int scopePos = 0;
    int activeCount = 0;
    Stage envStage = Stage::idle; double envValue = 0, envWait = 0;
    double oscRatios[2][3] {}; double unisonRatios[5][4] {};
    double l8anchor = 0;
    // PSY FILTER GATE: smoothed cutoff / level, two low-pass sections per channel (transposed direct form II), as the page's worklet
    // RESO COMB: two 4096-sample lines read at a fractional delay, one-pole 2.4 kHz low-pass in the feedback
    std::vector<float> cbBuf[2]; int cbW = 0; double cbLp[2] {};
    double fgF = 1000, fgG = 1, fgZ[8] {}, fgC[10] {}; unsigned fgN = 0; bool fgInit = false;
    double ug[16] {}; double uAbs = -1e12; int uMask = 0, uRise = 0; double uK = 1;
    double xd[40] {};
    uint8_t cf[16] {};
    double sAbs[16]; int sK[16] {}; uint8_t sOk[16] {}; double sRand[16] {}, sCoef[16], sFrac[16] {};
};
} // namespace mosaic
