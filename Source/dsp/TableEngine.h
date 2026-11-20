#pragma once
/* TABLE: Mosaic's wavetable second engine (the page's table.js), rebuilt. Voices of Chrome-style periodic oscillators
   (unison, stereo L/R frames, crossfaded pairs), FM from Mosaic's own voices, self-FM, a low-pass, an ADSR, and the
   eight-lane MOTION step modulator. Its output joins Mosaic's voices at the effects gate. */
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "MiniJson.h"
#include "PeriodicOsc.h"
#include "Timeline.h"
#include "WebAudioParts.h"
#include "WtCore.h"

namespace mosaic
{
struct MotionTarget { std::string dest; double depth = 0; bool alt = false; };
struct MotionLane { std::string name; bool semis = false; std::array<double, 16> steps {}; std::array<bool, 16> rnd {}; std::vector<MotionTarget> targets; };
struct Motion { bool on = false; double scene = .06; bool all = false; std::string rate = "1/16"; double swing = 0, slew = 0; std::array<MotionLane, 8> lanes; };

struct TableSettings
{
    bool on = false; std::string table = "WT17"; double scan = .5, M1 = 0, M2 = 0, M3 = 0, M4 = 0, level = .6, attack = .01, decay = .35, sustain = .75, release = .4,
           cutoff = 9000, res = .8, detune = 14, fmDepth = 0, selfFm = 0, fmRatio = 2; int unison = 1, octave = 0, poly = 12;
    std::string follow = "A", mode = "replace"; Motion motion;
};

// the waves for one table / scan / macro setting: one PeriodicWave per band limit (MIPS), L and (when wide) R
struct TableWaves { std::array<wa::WavePtr, 10> L, R; bool stereo = false; double pitchDiv = 1; std::string key; };
std::shared_ptr<TableWaves> buildTableWaves (const wt::Bank&, const TableSettings&, double sampleRate);

class TableEngine
{
public:
    void prepare (double sampleRate);
    void setBank (std::shared_ptr<const wt::Bank> b) { bank = std::move (b); }
    // the page's hooks.state: reads state.wtLab (or the starter preset named like the state) into the settings
    void setState (const json::Value& state, double now);
    const TableSettings& settings() const { return S; }
    std::string waveKey() const;                        // changes when the waves must be rebuilt
    void setWaves (std::shared_ptr<TableWaves> w, double now);   // install new waves (crossfading sounding voices, as swapWaves)
    bool hasWaves() const { return waves != nullptr; }
    bool replaces() const { return S.on && S.mode == "replace"; }   // TABLE ONLY: Mosaic's own voices stay silent
    bool follows (const std::string& layer, bool layerAEnabled) const;

    void noteOn (uint32_t id, int note, double vel, const std::string& layer, double time, bool layerAEnabled, double now);
    void noteOff (uint32_t id, double time, double now);
    void sustain (bool on, double now);
    void bend (double value, double bendRange, double now);
    void panic (double now);
    void setClock (double bpm, double anchor) { motionBpm = bpm; motionAnchor = anchor; }

    // adds TABLE's output into L/R. fmIn: Mosaic's voices summed to mono (the FM source), n <= 128, frame = absolute frame of L[0]
    void process (float* L, float* R, const float* fmIn, int n, long long frame);
    int activeVoices() const;
    std::vector<std::string> starterNames() const;

private:
    struct Pair { std::array<wa::WavePtr, 2> wave; std::array<wa::Timeline, 2> gain; bool right = false; };
    struct Voice
    {
        uint32_t id = 0; int note = 0; double f = 0, t0 = 0, a = 0, d = 0, s = 0, r = 0, peak = 0; bool rel = false, mot = false; double stopAt = 1e30;
        int mi = 0, U = 1; std::vector<double> cents; std::vector<wa::OscPhase> phase; std::vector<wa::Timeline> detune;   // per unison voice
        std::vector<Pair> pairs; int pairSide = 0;                                                                         // [u*2 + 0] = L, [u*2 + 1] = R
        wa::Timeline g, fmGain, lpF, lpQ, mg, pan;
        bool selfFm = false; wa::OscPhase foPhase; wa::Timeline foDetune, fg; double foFreq = 0;
        wa::Biquad lp; long long startFrame = 0;
        // motion
        long long mStep = 0; double mNext = -1; int frame = -1; double xEnd = 0;
    };
    double sr = 48000; std::shared_ptr<const wt::Bank> bank; TableSettings S; std::shared_ptr<TableWaves> waves; wa::WavePtr sine;
    std::map<int, std::shared_ptr<TableWaves>> motionFrames; std::string motionKey;
    std::vector<std::unique_ptr<Voice>> voices; std::vector<Voice*> held; bool sustainOn = false; double bendCents = 0;
    wa::Timeline bus; double motionBpm = 120, motionAnchor = 0; long long nowFrame = 0;
    void doRelease (Voice& v, double time, bool quick, double now);
    double envAt (const Voice& v, double t) const;
    void applyLive (double now);
    // motion
    struct StepMods { double scan = 0, cutoff = 0, res = 0, gate = 1, pitch = 0, fm = 0, pan = 0; };
    StepMods stepMods (long long n) const;
    std::array<double, 8> laneWeights() const;
    double stepDur() const;
    double stepTime (long long n) const;
    long long firstStep (double t) const;
    bool usesScan() const;
    void motionTick (double now, double horizon);
    std::shared_ptr<TableWaves> frameWave (int idx);
};
} // namespace mosaic
