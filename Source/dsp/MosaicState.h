#pragma once
/* Continuum Mosaic's sound state, as the voice engine reads it (the page's slimState()).
   Numbers are kept raw (NaN = missing/null) so each read can apply the prototype's own default the same way
   (`x || d` and `x == null ? d : x` differ); strings become small enums. Built off the audio thread. */
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include "MiniJson.h"

namespace mosaic
{
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
inline double OR (double x, double d) { return (x != x || x == 0) ? d : x; }   // JS: x || d
inline double NUL (double x, double d) { return x != x ? d : x; }               // JS: x == null ? d : x

enum class LfoWave : int { missing = -1, sine, square, saw, sample, triangle, other };
enum class OscWave : int { missing = -1, sine, saw, square, noise, pink, other };
enum class FiltType : int { missing = -1, lowpass, highpass, bandpass, notch, comb, formant, drip, other };
enum class DripMode : int { both, even, rain, splash, stream, storm, threeD };
enum class FmMode : int { off, fm, pd, both };
enum class PdCurve : int { bend, sine, pulse };
enum class StepMode : int { wave, mul, step, rand, off };
enum class StepLayer : int { any, A, B, AB };

// mod sources: 0 = none; 1 lfo1, 2 lfo2, 3 amp, 4 filter, 5 vel, 6 wheel, 7 key, 8 drip, 9 rain; 10+n = "l??N" (n = char3 - '1', 0..7)
struct ModSrc { int code = 0; int lfoN = -1; };   // lfoN: the 4-char "l..N" index the prototype reads (charCode(3) - 49), or -1
// destinations: 1..39 = the cross-destinations (X_DEST); then these
enum Dest : int { D_none = 0, D_pitch = 40, D_cutoff, D_res, D_pwm, D_fm, D_pan, D_osc1, D_osc2, D_osc3, D_gate, D_drip, D_off };

struct Env { double a = NaN, d = NaN, s = NaN, r = NaN, delay = NaN, hold = NaN, curve = NaN, fade = NaN, velocity = NaN, depth = NaN; };
struct Osc { OscWave wave = OscWave::missing; double octave = NaN, semi = NaN, fine = NaN, level = NaN, pwm = NaN, fold = NaN, ring = NaN, sync = NaN, xmod = NaN, sub = NaN; };
struct Filter
{
    FiltType type = FiltType::missing; double cutoff = NaN, res = NaN, env = NaN, lfo = NaN, key = NaN, drive = NaN, slope = NaN, comb = NaN, vowel = NaN;
    double ping = NaN, sweep = NaN, fall = NaN, bubble = NaN, fm = NaN, chirp = NaN, dwidth = NaN, density = NaN, dsize = NaN, dguard = NaN;
    DripMode dripMode = DripMode::both; bool dripStorm = false, drip3d = false, dripStyleSplash = false, dripStyleStream = false;
};
struct Layer
{
    bool present = false, disabled = false;
    Env amp, filt; Filter filter; std::array<Osc, 3> osc {};
    double character = NaN, fmRatio = NaN, fmIndex = NaN, pdDepth = NaN, level = NaN;
    FmMode fmMode = FmMode::off; PdCurve pdCurve = PdCurve::bend;
};
struct Route { double a = 0; int d = D_none; int t = 0; bool valid = false; };   // t: 0 own, 1 A, 2 B, 3 both
struct SeqStep { bool t = false; double p = NaN, s = NaN, v = NaN, r = NaN, mo = NaN; StepLayer l = StepLayer::any; StepMode m = StepMode::wave; LfoWave w = LfoWave::missing; };
struct LfoSeq { bool on = false; double length = NaN, slew = NaN; double beats = 0.25; bool bi = false; std::vector<SeqStep> steps; };
struct Lfo
{
    bool present = false; LfoWave wave = LfoWave::missing; double rate = NaN, morph = NaN, fold = NaN; bool retrig = false;
    std::vector<Route> osc, routes; bool hasOsc = false, hasRoutes = false; LfoSeq seq; bool hasSeq = false;
};
struct ModSlot { bool present = false; double amount = NaN; ModSrc src; int dest = D_none; int target = 0; };   // target: 0 skip, 1 A, 2 B, 3 both
struct USeq { bool present = false, on = false, retrig = false; std::vector<int> grid; bool hasGrid = false; double beats = 0.25, steps = NaN, xfade = NaN; };

struct State
{
    bool present = false;
    double bendRange = NaN, detune = NaN, unison = NaN, bpm = NaN, duck = NaN, duckRel = NaN, duckShape = NaN;
    bool fgOn = false; int fgSteps[16] {}; double fgCut = 0.42, fgRes = 0.35, fgDepth = 0.85;   // PSY FILTER GATE (state.fg)   // duck: the 1/4-note sidechain pump (fx.duckDepth)
    std::array<Lfo, 8> lfo {}; bool hasLfo = false;
    std::array<Lfo, 8> lfoB {}; bool hasLfoB = false;
    std::vector<ModSlot> mod; bool hasMod = false;
    Layer A, B; bool hasLayers = false;
    USeq lfoSeq;
};

State stateFromJson (const json::Value& v);
int xDest (const std::string& name);   // X_DEST, 0 when not one
} // namespace mosaic
