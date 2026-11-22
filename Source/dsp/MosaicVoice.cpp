#include "MosaicVoice.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mosaic
{
namespace
{
constexpr double TAU = 6.283185307179586;
constexpr double PI = 3.141592653589793;
constexpr double ENV_OCTAVES = 6;
const double FORMANTS[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
const double FORMANT_GAIN[3] = { 1, 0.6, 0.35 };
constexpr double FORMANT_TRIM = 0.55;

inline double clamp (double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double wrap01 (double x) { x -= std::floor (x); return x < 0 ? x + 1 : x; }
inline double mtof (double n) { return 440 * std::pow (2.0, (n - 69) / 12); }
inline double resonanceQ (double res) { const double r = clamp (res, 0, 1); return 0.7 + r * r * 16; }
inline bool finite (double x) { return std::isfinite (x); }
inline double blep (double t, double dt)
{
    if (dt <= 1e-6) return 0;
    if (t < dt) { const double x = t / dt; return x + x - x * x - 1; }
    if (t > 1 - dt) { const double x = (t - 1) / dt; return x * x + x + x + 1; }
    return 0;
}
inline double oscSample (OscWave type, double phase, double dt, double pwm)
{
    if (type == OscWave::sine) return std::sin (TAU * phase);
    if (type == OscWave::saw) return (2 * phase - 1) - blep (phase, dt);
    if (type == OscWave::square)
    {
        const double pw = pwm < 0.05 ? 0.05 : pwm > 0.95 ? 0.95 : pwm;
        double y = phase < pw ? 1 : -1;
        y += blep (phase, dt);
        double p2 = phase - pw; if (p2 < 0) p2 += 1;
        y -= blep (p2, dt);
        return y;
    }
    return phase < 0.5 ? phase * 4 - 1 : 3 - phase * 4;
}
inline double warpPhase (double phase, double depth, PdCurve curve)
{
    if (depth < 0.001) return phase;
    if (curve == PdCurve::sine) return wrap01 (phase + depth * 0.45 * std::sin (TAU * phase));
    if (curve == PdCurve::pulse)
    {
        const double b = 0.5 - depth * 0.42;
        if (phase < b) return (phase / b) * 0.5;
        return 0.5 + ((phase - b) / (1 - b)) * 0.5;
    }
    return std::pow (clamp (phase, 0, 1), 1 + depth * 4);
}
inline double lfoWaveAt (LfoWave w, double t, double hold)
{
    if (w == LfoWave::sine) return std::sin (TAU * t);
    if (w == LfoWave::square) return t < 0.5 ? 1 : -1;
    if (w == LfoWave::saw) return 2 * t - 1;
    if (w == LfoWave::sample) return hold;
    if (t < 0.25) return t * 4;
    if (t < 0.75) return 1 - (t - 0.25) * 4;
    return -1 + (t - 0.75) * 4;
}
inline double lfoSample (LfoWave wave, double phase, double hold, double morph, double fold)
{
    if (wave == LfoWave::sample) return hold;
    double y = lfoWaveAt (wave, phase, hold);
    if (morph > 0.0001)
    {
        LfoWave a = LfoWave::saw, b = LfoWave::square;
        if (wave == LfoWave::triangle) { a = LfoWave::sine; b = LfoWave::saw; }
        else if (wave == LfoWave::saw) { a = LfoWave::square; b = LfoWave::sine; }
        else if (wave == LfoWave::square) { a = LfoWave::saw; b = LfoWave::sine; }
        const double m = clamp (morph, 0, 1) * 2;
        const double from = m <= 1 ? y : lfoWaveAt (a, phase, hold);
        const double to = lfoWaveAt (m <= 1 ? a : b, phase, hold);
        const double blend = m <= 1 ? m : m - 1;
        y = from + (to - from) * blend;
    }
    if (fold > 0.0001)
    {
        double w = std::fmod (y * (1 + fold * 4) + 1, 4.0);
        if (w < 0) w += 4;
        const double folded = w < 2 ? w - 1 : 3 - w;
        y += (folded - y) * fold;
    }
    return y;
}
inline double envCoef (double seconds, double sr) { const double tau = std::max (0.0004, seconds / 2.3); return 1 - std::exp (-1 / (tau * sr)); }
inline uint32_t imul (uint32_t a, uint32_t b) { return a * b; }
inline double l8hash (long long a, long long b)
{
    uint32_t h = (imul ((uint32_t) (int32_t) a, 0x9E3779B1u) ^ imul ((uint32_t) (int32_t) (b + 1), 0x85EBCA77u));
    h ^= h >> 15; h = imul (h, 0x2C1B3C6Du); h ^= h >> 12;
    return (double) h / 4294967296.0;
}
// route targets present: 1 own, 2 A, 4 B, 8 both; 32 extra destination, 64 gate, 128 drip
inline int l8Scan (const std::vector<Route>& rs, bool has)
{
    if (! has) return 0; int f = 0;
    for (auto& ro : rs)
    {
        if (! ro.valid) continue;
        f |= ro.t == 1 ? 2 : ro.t == 2 ? 4 : ro.t == 3 ? 8 : 1;
        if (ro.d >= 1 && ro.d <= 39) f |= 32; else if (ro.d == D_gate) f |= 64; else if (ro.d == D_drip) f |= 128;
    }
    return f;
}
inline int l8Active (const Lfo* c) { if (c == nullptr || ! c->present) return 0; return l8Scan (c->osc, c->hasOsc) | l8Scan (c->routes, c->hasRoutes) | (c->hasSeq && c->seq.on ? 16 : 0); }
} // namespace

uint64_t VoiceEngine::hashId (const char* s, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char) s[i]; h *= 1099511628211ull; }
    return h == 0 ? 1 : h;
}

VoiceEngine::VoiceEngine()
{
    for (auto& v : voices) v = std::make_unique<Voice>();
    seed (0x9e3779b9u);
}

void VoiceEngine::seed (uint32_t s)
{
    rng.seed (s); l8VoiceSeed = 0;
    for (auto& v : voices) { *v = Voice(); makeVoice (*v); }
    evCount = 0; bend = bendTarget = wheel = wheelTarget = 0; sustain = false; dcInL = dcOutL = dcInR = dcOutR = 0;
    std::fill (std::begin (scope), std::end (scope), 0.f); scopePos = 0; activeCount = 0;
    l8anchor = 0; std::fill (std::begin (ug), std::end (ug), 0.0); uAbs = -1e12; uMask = uRise = 0; uK = 1;
    std::fill (std::begin (sAbs), std::end (sAbs), -1e12); std::fill (std::begin (sCoef), std::end (sCoef), 1.0);
    std::fill (std::begin (sK), std::end (sK), 0); std::fill (std::begin (sOk), std::end (sOk), 0);
    std::fill (std::begin (sRand), std::end (sRand), 0.0); std::fill (std::begin (sFrac), std::end (sFrac), 0.0);
    for (int i = 0; i < 2; ++i) for (int k = 0; k < 3; ++k) oscRatios[i][k] = 1;
    if (state) cacheRatios (*state);
}

void VoiceEngine::noisePattern (double* a, bool pink)
{
    for (int c = 0; c < 12; ++c)
    {
        double v[64]; double prev = 0, sq = 0;
        for (int i = 0; i < 64; ++i) { const double x = rng.next() * 2 - 1; prev = pink ? prev * 0.7 + x * 0.54 : x; v[i] = prev; sq += prev * prev; }
        const double g = 0.577 / std::sqrt (sq / 64 + 1e-9);
        for (int i = 0; i < 64; ++i) a[c * 64 + i] = std::max (-1.0, std::min (1.0, v[i] * g));
    }
}

void VoiceEngine::makeVoice (Voice& v)
{
    // the same order the prototype's object literal calls Math.random()
    v.fmPhase = rng.next();
    v.driftPhase = rng.next() * 7;
    noisePattern (v.nw, false);
    noisePattern (v.npk, true);
    v.lfo[0] = rng.next(); v.lfo[1] = rng.next();
    v.sh[0] = rng.next() * 2 - 1; v.sh[1] = rng.next() * 2 - 1;
    l8VoiceSeed++;
    for (int i = 2; i < 16; ++i) { v.lfo[i] = std::fmod (l8VoiceSeed * 0.6180339887 + i * 0.3819660113, 1.0); v.sh[i] = 0; }
}

void VoiceEngine::setState (std::shared_ptr<const State> s) { state = std::move (s); if (state) cacheRatios (*state); }

void VoiceEngine::cacheRatios (const State& s)
{
    for (int li = 0; li < 2; ++li)
    {
        const Layer* L = s.hasLayers ? (li ? &s.B : &s.A) : nullptr;
        for (int oi = 0; oi < 3; ++oi)
        {
            double semis = 0;
            if (L && L->present) { const Osc& o = L->osc[(size_t) oi]; semis = OR (o.octave, 0) * 12 + OR (o.semi, 0) + OR (o.fine, 0) / 100; }
            oscRatios[li][oi] = std::pow (2.0, semis / 12);
        }
    }
    const double detune = OR (s.detune, 0);
    for (int copies = 1; copies <= 4; ++copies)
        for (int u = 0; u < copies; ++u)
        {
            double spread = 0;
            if (copies == 2) spread = u == 0 ? -detune : detune;
            else if (copies == 3) spread = (u - 1) * detune;
            else if (copies == 4) spread = (u - 1.5) * detune * 0.7;
            unisonRatios[copies][u] = std::pow (2.0, spread / 1200);
        }
}

void VoiceEngine::post (const Event& e)
{
    if (evCount >= (int) ev.size()) evCount = (int) ev.size() - 1;
    ev[(size_t) evCount++] = e;
}

void VoiceEngine::panic()
{
    evCount = 0;
    for (auto& v : voices) silence (*v);
    bendTarget = 0; wheelTarget = 0;
}

void VoiceEngine::silence (Voice& v)
{
    v.active = false; v.gate = false; v.fading = false; v.hasPend = false; v.sustainHold = false;
    v.amp = 0; v.fenv = 0; v.ampStage = Stage::idle; v.filtStage = Stage::idle; v.ampWait = 0; v.filtWait = 0;
    v.id = 0; v.ic1 = 0; v.ic2 = 0; v.combDamp = 0;
}

VoiceEngine::Voice* VoiceEngine::findVoice (uint64_t id)
{
    for (auto& v : voices) if (v->active && v->id == id) return v.get();
    return nullptr;
}

VoiceEngine::Voice& VoiceEngine::alloc()
{
    Voice* oldest = voices[0].get(); Voice* quiet = nullptr;
    for (auto& vp : voices)
    {
        Voice& v = *vp;
        if (! v.active) return v;
        if (v.age >= oldest->age) oldest = &v;
        if (v.ampStage == Stage::release || v.ampStage == Stage::idle) if (quiet == nullptr || v.amp < quiet->amp) quiet = &v;
    }
    return quiet != nullptr ? *quiet : *oldest;
}

const Layer* VoiceEngine::layerState (int i) const
{
    if (! state || ! state->hasLayers) return nullptr;
    const Layer& L = i == 1 ? state->B : state->A;
    return L.present ? &L : nullptr;
}

void VoiceEngine::arm (Voice& v, int note, double vel, int layer, double glide, bool legato)
{
    const double prev = v.freq; const bool was = v.active && prev > 20;
    v.active = true; v.note = note; v.vel = clamp (vel, 0.02, 1); v.layer = layer;
    v.gate = true; v.sustainHold = false; v.fading = false; v.hasPend = false; v.age = 0;
    v.target = mtof (note);
    if ((legato || glide > 0) && was) { v.freq = prev; v.glide = glide; }
    else { v.freq = v.target; v.glide = 0; }
    if (legato) return;
    const Layer* ls = layerState (layer);
    const double filterDelay = ls ? std::max (0.0, OR (ls->filt.delay, 0)) : 0;
    const double ampDelay = ls ? std::max (0.0, OR (ls->amp.delay, 0)) : 0;
    v.ampStage = ampDelay > 0 ? Stage::del : Stage::attack; v.ampWait = ampDelay;
    v.filtStage = filterDelay > 0 ? Stage::del : Stage::attack; v.filtWait = filterDelay;
    v.ic1 = v.ic2 = v.ic1b = v.ic2b = 0;
    v.dripPh = rng.next();
    v.dripWait = 16 + (int) (rng.next() * 90);
    v.dripAge = 0; v.dripHit = 0; v.dripSize = 0.5; v.dripJit = 0; v.dripRatio = 0; v.dripAmp = 0; v.dripRate = 1; v.dripT0 = 0; v.dripEnv = 0; v.dripKick = 0;
    std::fill (std::begin (v.dtrig), std::end (v.dtrig), (uint8_t) 0);
    v.ftick = 0;
    v.fc = ls ? ls->filter.cutoff : 1000;
    if (! state) return;
    const State& S = *state;
    const Lfo* lfos = S.hasLfo ? S.lfo.data() : nullptr;
    const Lfo* lfosB = layer == 1 && S.hasLfoB ? S.lfoB.data() : nullptr;
    auto P = [] (const Lfo* arr, int i) -> const Lfo* { return arr && arr[i].present ? &arr[i] : nullptr; };
    const Lfo* r0 = P (lfosB, 0) ? P (lfosB, 0) : P (lfos, 0);
    const Lfo* r1 = P (lfosB, 1) ? P (lfosB, 1) : P (lfos, 1);
    if (r0 && r0->retrig) v.lfo[0] = 0;
    if (r1 && r1->retrig) v.lfo[1] = 0;
    for (int li = 0; li < 8; ++li)
    {
        const Lfo* own = P (lfosB, li) ? P (lfosB, li) : P (lfos, li);
        if (li > 1 && own && own->retrig) v.lfo[li] = 0;
        const Lfo* other = layer == 1 ? P (lfos, li) : (S.hasLfoB ? P (S.lfoB.data(), li) : nullptr);
        if (other && other->retrig) v.lfo[8 + li] = 0;
    }
}

void VoiceEngine::queueRetrigger (Voice& v, const Event& m, int layer)
{
    v.fading = true; v.hasPend = true; v.pendNote = m.note; v.pendVel = OR (m.vel, 100) / 127; v.pendLayer = layer; v.pendGlide = OR (m.glide, 0);
    v.gate = true; v.id = m.id;
}

void VoiceEngine::applyEvent (const Event& m)
{
    if (m.type == EvType::bend) { bendTarget = clamp (m.value, -1, 1); return; }
    if (m.type == EvType::mod) { wheelTarget = clamp (m.value, 0, 1); return; }
    if (m.type == EvType::sustain)
    {
        sustain = m.on;
        if (! sustain) for (auto& v : voices) if (v->sustainHold) { v->sustainHold = false; v->gate = false; }
        return;
    }
    if (m.type == EvType::off)
    {
        Voice* v = findVoice (m.id); if (! v) return;
        if (v->fading) v->hasPend = false;
        if (sustain) v->sustainHold = true; else v->gate = false;
        return;
    }
    const int layer = m.layer == 1 ? 1 : 0;
    const double vel = OR (m.vel, 100) / 127, glide = OR (m.glide, 0);
    Voice* v = findVoice (m.id);
    if (! v)
    {
        v = &alloc();
        if (v->active && v->amp > 0.03 && ! m.legato) { queueRetrigger (*v, m, layer); return; }
        v->id = m.id; arm (*v, m.note, vel, layer, glide, false); return;
    }
    if (m.legato) { v->id = m.id; arm (*v, m.note, vel, layer, glide, true); return; }
    if (v->amp > 0.03) { queueRetrigger (*v, m, layer); return; }
    v->id = m.id; arm (*v, m.note, vel, layer, glide, false);
}

double VoiceEngine::envShapeCoef (const Env& p, double time, double sr, double distance) const
{
    const double k = envCoef (time, sr);
    const double curve = OR (p.curve, 0);
    if (curve == 0) return k;
    return k * clamp (1 + curve * (2 * clamp (distance, 0, 1) - 1) * 1.4, 0.18, 2.4);
}

void VoiceEngine::stepEnv (Stage stage, double value, const Env& p, bool gate, double sr, double wait)
{
    if (! gate && stage != Stage::release && stage != Stage::idle) stage = Stage::release;
    if (stage == Stage::del) { value = 0; wait -= 1 / sr; if (wait <= 0) { wait = 0; stage = Stage::attack; } }
    else if (stage == Stage::attack)
    {
        value += (1 - value) * envShapeCoef (p, p.a, sr, 1 - value);
        if (value >= 0.999) { value = 1; if (OR (p.hold, 0) > 0) { stage = Stage::hold; wait = std::max (0.0, p.hold); } else stage = Stage::decay; }
    }
    else if (stage == Stage::hold) { value = 1; wait -= 1 / sr; if (wait <= 0) { wait = 0; stage = Stage::decay; } }
    else if (stage == Stage::decay)
    {
        value += (p.s - value) * envShapeCoef (p, p.d, sr, std::abs (p.s - value));
        if (std::abs (value - p.s) < 0.002) { value = p.s; stage = Stage::sustain; }
    }
    else if (stage == Stage::sustain)
    {
        const double fade = OR (p.fade, 0);
        if (fade == 0) value += (p.s - value) * 0.02;
        else { const double dest = fade > 0 ? 0 : 1; const double tau = 0.12 + (1 - std::abs (fade)) * 8; value += (dest - value) * (1 - std::exp (-1 / (tau * sr))); }
    }
    else if (stage == Stage::release)
    {
        value += (0 - value) * envShapeCoef (p, p.r, sr, std::abs (value));
        if (value < 0.0006) { value = 0; stage = Stage::idle; }
    }
    envStage = stage; envValue = value; envWait = wait;
}

double VoiceEngine::sourceValue (const ModSrc& src, const Voice& v, double l1, double l2, double wh, const double* lv) const
{
    if (lv && src.lfoN >= 2 && src.lfoN <= 7) return lv[src.lfoN];
    switch (src.code)
    {
        case 1: return l1;  case 2: return l2;  case 3: return v.amp;  case 4: return v.fenv;  case 5: return v.vel * 2 - 1;
        case 6: return wh * 2 - 1;  case 7: return (v.note - 60) / 48.0;  case 8: return v.dripEnv;  case 9: return v.dripJit * v.dripEnv;
        default: return 0;
    }
}

void VoiceEngine::scopeCopy (float* out) const { for (int i = 0; i < 160; ++i) out[i] = scope[(scopePos + i) % 160]; }

void VoiceEngine::process (float* Lch, float* Rch, int frames, double now, double sr)
{
    const double end = now + frames / sr;
    int dueCount = 0, keep = 0;
    for (int i = 0; i < evCount; ++i)
    {
        const Event& e = ev[(size_t) i];
        if (! (e.time > 0) || e.time <= end) due[(size_t) dueCount++] = e; else ev[(size_t) keep++] = e;
    }
    evCount = keep;
    for (int i = 1; i < dueCount; ++i)   // insertion sort by time (stable, as the prototype)
    {
        const Event item = due[(size_t) i]; int j = i - 1;
        const double it = item.time > 0 ? item.time : 0;
        while (j >= 0 && (due[(size_t) j].time > 0 ? due[(size_t) j].time : 0) > it) { due[(size_t) j + 1] = due[(size_t) j]; j--; }
        due[(size_t) j + 1] = item;
    }
    const std::shared_ptr<const State> stHold = state; const State* S = stHold.get();
    int ei = 0;
    // tempo-synced 1/4-note sidechain pump, as the page's worklet (off at 0)
    const double duck = (S && S->duck == S->duck && S->duck > 0.001) ? std::min (1.0, S->duck) : 0.0;
    const double beatDur = 60.0 / ((S && S->bpm == S->bpm && S->bpm > 0) ? S->bpm : 120.0);
    const double duckRel = (S && S->duckRel == S->duckRel && S->duckRel > 0) ? std::min (0.96, std::max (0.12, S->duckRel)) : 0.55;
    const double duckExp = 0.25 + std::min (1.0, std::max (0.0, (S && S->duckShape == S->duckShape) ? S->duckShape : 0.35)) * 1.75;
    const bool cbOn = S && S->combOn;
    if (cbOn && cbBuf[0].empty()) { for (auto& b : cbBuf) b.assign (4096, 0.f); cbW = 0; cbLp[0] = cbLp[1] = 0; }
    double cbDL = 0, cbDR = 0, cbFb = 0, cbMix = 0, cbA = 0;
    if (cbOn) { const double hz = 55 + std::pow (std::min (1.0, std::max (0.0, S->combTune)), 1.8) * 650; cbDL = sr / hz; cbDR = sr / (hz * 1.015);
                cbFb = std::min (1.0, std::max (0.0, S->combFb)) * 0.88; cbMix = std::min (1.0, std::max (0.0, S->combMix)); cbA = 1 - std::exp (-2 * 3.141592653589793 * 2400 / sr); }
    const bool efOn = S && S->envOn;
    if (efOn && ! efInit) { efInit = true; efE = 0; efN = 0; for (auto& z : efZ) z = 0; }
    if (! efOn) efInit = false;
    const double efAtt = 1 - std::exp (-1 / (0.005 * sr)), efRel = 1 - std::exp (-1 / (0.12 * sr));
    double efGain = 0, efSpan = 0, efQ = 0.8;
    if (efOn) { efGain = 1.2 + std::min (1.0, std::max (0.0, S->envSens)) * 5; efSpan = 400 + std::min (1.0, std::max (0.0, S->envRange)) * 2600; efQ = 0.8 + std::min (1.0, std::max (0.0, S->envRes)) * 3.2; }
    auto lpCoef = [sr] (double* c, int o, double f, double q) { const double w0 = 2 * 3.141592653589793 * std::min (f, sr * 0.45) / sr, cs = std::cos (w0), al = std::sin (w0) / (2 * q), a0 = 1 + al;
                                                                c[o] = (1 - cs) / 2 / a0; c[o + 1] = (1 - cs) / a0; c[o + 2] = (1 - cs) / 2 / a0; c[o + 3] = -2 * cs / a0; c[o + 4] = (1 - al) / a0; };
    auto lpTick = [] (const double* c, int o, double* z, int zo, double x) { const double y = c[o] * x + z[zo]; z[zo] = c[o + 1] * x - c[o + 3] * y + z[zo + 1]; z[zo + 1] = c[o + 2] * x - c[o + 4] * y; return y; };
    const bool fgOn = S && S->fgOn;
    if (fgOn && ! fgInit) { fgInit = true; fgF = 1000; fgG = 1; fgN = 0; for (auto& z : fgZ) z = 0; }
    const double fgKf = 1 - std::exp (-1 / (0.008 * sr)), fgKg = 1 - std::exp (-1 / (0.006 * sr));
    double fgOpen = 0, fgClosed = 0, fgLow = 1, fgQ = 0.70710678;
    if (fgOn) { const double cut = std::min (1.0, std::max (0.0, S->fgCut)), dep = std::min (1.0, std::max (0.0, S->fgDepth)), base = 180 + std::pow (cut, 1.8) * 3800;
                fgOpen = std::min (4800.0, base * (1.2 + dep * 2.4)); fgClosed = std::max (110.0, base * (1 - dep * 0.72)); fgLow = std::max (0.05, 1 - dep * 0.92);
                fgQ = 0.70710678 + std::min (1.0, std::max (0.0, S->fgRes)) * 2.5; }
    auto fgCoef = [this, sr] (int o, double f, double q) { const double w0 = 2 * 3.141592653589793 * std::min (f, sr * 0.45) / sr, cs = std::cos (w0), al = std::sin (w0) / (2 * q), a0 = 1 + al;
                                                       fgC[o] = (1 - cs) / 2 / a0; fgC[o + 1] = (1 - cs) / a0; fgC[o + 2] = (1 - cs) / 2 / a0; fgC[o + 3] = -2 * cs / a0; fgC[o + 4] = (1 - al) / a0; };
    auto fgTick = [this] (int o, int zo, double x) { const double y = fgC[o] * x + fgZ[zo]; fgZ[zo] = fgC[o + 1] * x - fgC[o + 3] * y + fgZ[zo + 1]; fgZ[zo + 1] = fgC[o + 2] * x - fgC[o + 4] * y; return y; };
    for (int i = 0; i < frames; ++i)
    {
        const double t = now + i / sr;
        while (ei < dueCount && (due[(size_t) ei].time > 0 ? due[(size_t) ei].time : 0) <= t) applyEvent (due[(size_t) ei++]);
        bend += (bendTarget - bend) * 0.02;
        wheel += (wheelTarget - wheel) * 0.01;
        double busL = 0, busR = 0;

        if (S && S->hasLayers)
        {
            const double bendMul = bend != 0 ? std::pow (2.0, (bend * OR (S->bendRange, 2)) / 12) : 1;
            const Lfo* lfos = S->hasLfo ? S->lfo.data() : nullptr;
            const Lfo* lfo0 = lfos && lfos[0].present ? &lfos[0] : nullptr;
            const Lfo* lfo1 = lfos && lfos[1].present ? &lfos[1] : nullptr;
            const LfoWave wave0 = lfo0 && lfo0->wave != LfoWave::missing ? lfo0->wave : LfoWave::sine;
            const LfoWave wave1 = lfo1 && lfo1->wave != LfoWave::missing ? lfo1->wave : LfoWave::triangle;
            const double rate0 = lfo0 ? OR (lfo0->rate, 0.2) : 0.2;
            const double rate1 = lfo1 ? OR (lfo1->rate, 3) : 3;
            const Lfo* lfoB8 = S->hasLfoB ? S->lfoB.data() : nullptr;
            const Lfo* lfo0B = lfoB8 && lfoB8[0].present ? &lfoB8[0] : lfo0;
            const Lfo* lfo1B = lfoB8 && lfoB8[1].present ? &lfoB8[1] : lfo1;
            const LfoWave wave0B = lfo0B && lfo0B->wave != LfoWave::missing ? lfo0B->wave : LfoWave::sine;
            const LfoWave wave1B = lfo1B && lfo1B->wave != LfoWave::missing ? lfo1B->wave : LfoWave::triangle;
            const double rate0B = lfo0B ? OR (lfo0B->rate, 0.2) : 0.2;
            const double rate1B = lfo1B ? OR (lfo1B->rate, 3) : 3;
            const double un = NUL (S->unison, 0);
            const int unison = un > 1 ? (un > 4 ? 4 : (int) un) : 1;
            const int modCount = S->hasMod ? (int) S->mod.size() : 0;
            // ---- LFO x16 pre-pass (global, once per sample) ----
            int matUse = 0, anyCf = 0; bool xAny = false, gateUsed = false;
            for (int s = 0; s < modCount; ++s)
            {
                const ModSlot& slot = S->mod[(size_t) s];
                if (! slot.present || ! (slot.amount != 0 && slot.amount == slot.amount)) continue;
                const int n = slot.src.lfoN; if (n >= 0 && n < 8) matUse |= 1 << n;
                if (slot.dest >= 1 && slot.dest <= 39) xAny = true; else if (slot.dest == D_gate) gateUsed = true;
            }
            const double bpm = OR (S->bpm, 120);
            for (int g = 0; g < 16; ++g)
            {
                const Lfo* c = g < 8 ? (lfos && lfos[g].present ? &lfos[g] : nullptr) : (lfoB8 && lfoB8[g - 8].present ? &lfoB8[g - 8] : nullptr);
                const int f = l8Active (c); cf[g] = (uint8_t) f; anyCf |= f;
                if (f & 32) xAny = true;
                if (f & 64) gateUsed = true;
                if (f & 128) xAny = true;
                if (f & 16)
                {
                    const LfoSeq& q = c->seq;
                    const int len = q.length > 0 ? std::min (64, (int) q.length) : 16;
                    const double sd = (60 / bpm) * q.beats;
                    const double pos = (t - l8anchor) / sd; const long long abs = (long long) std::floor (pos); const double frac = pos - (double) abs;
                    int k = (int) (((abs % len) + len) % len); long long absK = abs; int back = 0;
                    while (back < len - 1) { const int pk = (k - 1 + len) % len; const bool tied = pk < (int) q.steps.size() && q.steps[(size_t) pk].t; if (tied) { k = pk; absK--; back++; } else break; }
                    sFrac[g] = back > 0 ? (back + frac) / (back + 1) : frac;
                    if (sAbs[g] != (double) absK)
                    {
                        static const SeqStep empty;
                        const SeqStep& st = k < (int) q.steps.size() ? q.steps[(size_t) k] : empty;
                        sAbs[g] = (double) absK; sK[g] = k;
                        sOk[g] = l8hash (absK, g) < NUL (st.p, 1) ? 1 : 0;
                        sRand[g] = l8hash (absK, g + 64) * 2 - 1;
                        const double sl = st.s == st.s ? st.s : OR (q.slew, 0);
                        sCoef[g] = sl > 0.001 ? 1 - std::exp (-1 / (std::max (0.0005, sl * sd * (back + 1) / 3) * sr)) : 1;
                    }
                }
            }
            const bool l8On = matUse > 3 || anyCf != 0;
            const bool xGate = xAny || gateUsed;
            const USeq& U = S->lfoSeq;
            const bool uOn = U.present && U.on && U.hasGrid;
            if (uOn)
            {
                const double usd = (60 / bpm) * U.beats;
                const int ulen = U.steps > 0 ? std::min (64, (int) U.steps) : 16;
                const double upos = (t - l8anchor) / usd; const double uabs = std::floor (upos);
                if (uabs != uAbs)
                {
                    const long long ua = (long long) uabs; const int gi = (int) (((ua % ulen) + ulen) % ulen);
                    const int mask = (gi < (int) U.grid.size() ? U.grid[(size_t) gi] : 0) & 0xffff;
                    uRise = mask & ~uMask; uMask = mask; uAbs = uabs;
                    const double xf = std::max (0.0005, NUL (U.xfade, 0.08) * usd);
                    uK = 1 - std::exp (-1 / (xf * sr));
                }
                else uRise = 0;
                for (int row = 0; row < 16; ++row) ug[row] += (((uMask >> row) & 1) - ug[row]) * uK;
            }

            for (auto& vp : voices)
            {
                Voice& voice = *vp;
                if (! voice.active) continue;
                const Layer* layer = voice.layer == 1 ? (S->B.present ? &S->B : nullptr) : (S->A.present ? &S->A : nullptr);
                if (! layer) { silence (voice); continue; }
                if (layer->disabled) voice.gate = false;
                voice.age++;
                if (voice.fading)
                {
                    const double fade = std::exp (-1 / (0.0035 * sr));
                    voice.amp *= fade; voice.fenv *= fade;
                    if (voice.amp < 0.0008)
                    {
                        voice.fading = false;
                        if (voice.hasPend) arm (voice, voice.pendNote, voice.pendVel, voice.pendLayer, voice.pendGlide, false);
                        else if (! voice.gate) { silence (voice); continue; }
                    }
                }
                else
                {
                    stepEnv (voice.ampStage, voice.amp, layer->amp, voice.gate, sr, voice.ampWait);
                    voice.ampStage = envStage; voice.amp = envValue; voice.ampWait = envWait;
                    stepEnv (voice.filtStage, voice.fenv, layer->filt, voice.gate, sr, voice.filtWait);
                    voice.filtStage = envStage; voice.fenv = envValue; voice.filtWait = envWait;
                    if (voice.ampStage == Stage::idle) { silence (voice); continue; }
                }

                const bool lfoIsB = voice.layer == 1;
                const double prev0 = voice.lfo[0];
                voice.lfo[0] = wrap01 (prev0 + (lfoIsB ? rate0B : rate0) / sr);
                if (voice.lfo[0] < prev0) voice.sh[0] = rng.next() * 2 - 1;
                const double prev1 = voice.lfo[1];
                voice.lfo[1] = wrap01 (prev1 + (lfoIsB ? rate1B : rate1) / sr);
                if (voice.lfo[1] < prev1) voice.sh[1] = rng.next() * 2 - 1;
                const Lfo* cfg0 = lfoIsB ? lfo0B : lfo0;
                const Lfo* cfg1 = lfoIsB ? lfo1B : lfo1;
                double l1 = lfoSample (lfoIsB ? wave0B : wave0, voice.lfo[0], voice.sh[0], cfg0 ? OR (cfg0->morph, 0) : 0, cfg0 ? OR (cfg0->fold, 0) : 0);
                double l2 = lfoSample (lfoIsB ? wave1B : wave1, voice.lfo[1], voice.sh[1], cfg1 ? OR (cfg1->morph, 0) : 0, cfg1 ? OR (cfg1->fold, 0) : 0);
                double* lv = voice.lv;
                double gateMul = 1;
                if (xGate) std::fill (std::begin (xd), std::end (xd), 0.0);
                bool routeAdd = false;
                if (l8On)
                {
                    lv[0] = l1; lv[1] = l2;
                    for (int p = 0; p < 16; ++p)
                    {
                        const int li = p & 7; const bool other = p > 7;
                        const bool linked = ! (lfoB8 && lfoB8[li].present);
                        if (other && linked) continue;
                        const int g = other ? (lfoIsB ? li : 8 + li) : (lfoIsB && ! linked ? 8 + li : li);
                        const Lfo* c = g < 8 ? (lfos && lfos[g].present ? &lfos[g] : nullptr) : (lfoB8 && lfoB8[g - 8].present ? &lfoB8[g - 8] : nullptr);
                        if (! c) continue;
                        const int f = cf[g];
                        const int mine = lfoIsB ? 4 : 2;
                        const int hits = other ? (f & (mine | 8)) : (f & (1 | mine | 8));
                        const bool need = other ? hits != 0 : (p < 2 || (matUse & (1 << p)) != 0 || hits != 0 || (f & 16) != 0);
                        if (! need) continue;
                        if (other || p > 1)
                        {
                            const double prev = voice.lfo[p];
                            voice.lfo[p] = wrap01 (prev + OR (c->rate, 1) / sr);
                            if (voice.lfo[p] < prev) voice.sh[p] = rng.next() * 2 - 1;
                            lv[p] = lfoSample (c->wave != LfoWave::missing ? c->wave : LfoWave::sine, voice.lfo[p], voice.sh[p], OR (c->morph, 0), OR (c->fold, 0));
                        }
                        voice.luni[p] = 0;
                        if (f & 16)
                        {
                            const LfoSeq& q = c->seq; static const SeqStep empty;
                            const SeqStep& st = sK[g] < (int) q.steps.size() ? q.steps[(size_t) sK[g]] : empty;
                            const bool layerOk = st.l == StepLayer::any || st.l == StepLayer::AB || (st.l == StepLayer::A ? ! lfoIsB : lfoIsB);
                            const StepMode mode = st.m;
                            double y = 0;
                            const bool stepLike = mode == StepMode::step || mode == StepMode::rand;
                            if (sOk[g] && layerOk && mode != StepMode::off)
                            {
                                const int rat = st.r > 1 ? std::min (8, (int) st.r) : 1;
                                const double sub = sFrac[g] * rat, subFrac = sub - std::floor (sub);
                                const bool open = rat > 1 && ! st.t ? subFrac < 0.7 : true;
                                const double v = NUL (st.v, 1);
                                if (mode == StepMode::wave || mode == StepMode::mul)
                                {
                                    const LfoWave w = st.w != LfoWave::missing ? st.w : (c->wave != LfoWave::missing ? c->wave : LfoWave::sine);
                                    const double mo = st.mo == st.mo ? st.mo : OR (c->morph, 0);
                                    const double wv = (st.w != LfoWave::missing || st.mo == st.mo || rat > 1) ? lfoSample (w, rat > 1 ? subFrac : voice.lfo[p], voice.sh[p], mo, OR (c->fold, 0)) : lv[p];
                                    y = mode == StepMode::mul ? wv * v : wv;
                                }
                                else if (mode == StepMode::step) y = open ? (q.bi ? v * 2 - 1 : v) : (q.bi ? -1 : 0);
                                else y = open ? (q.bi ? sRand[g] : (sRand[g] + 1) * 0.5) : 0;
                            }
                            if (stepLike || mode == StepMode::off || ! sOk[g] || ! layerOk) voice.luni[p] = q.bi ? 0 : 1;
                            const double cfo = sCoef[g];
                            voice.lsm[p] = cfo >= 1 ? y : voice.lsm[p] + (y - voice.lsm[p]) * cfo;
                            lv[p] = voice.lsm[p];
                            if (p == 0) l1 = lv[0]; else if (p == 1) l2 = lv[1];
                        }
                        if (hits) routeAdd = true;
                    }
                }

                double pitchAdd = 0, cutOct = 0, resAdd = 0, pwmAdd = 0, fmAdd = 0, panAdd = 0, lev0 = 0, lev1 = 0, lev2 = 0;
                const int layerCode = voice.layer == 1 ? 2 : 1;
                double* ugp = voice.ugp;
                if (uOn)
                {
                    const int ownBase = lfoIsB ? 8 : 0, othBase = lfoIsB ? 0 : 8;
                    for (int p = 0; p < 16; ++p)
                    {
                        const int row = p < 8 ? ownBase + p : othBase + p - 8;
                        ugp[p] = ug[row];
                        if (uRise && U.retrig && (uRise & (1 << row))) voice.lfo[p] = 0;
                    }
                }
                auto applyDest = [&] (int dest, double a)
                {
                    if (dest == D_pitch) pitchAdd += a * 12; else if (dest == D_cutoff) cutOct += a * 4; else if (dest == D_res) resAdd += a;
                    else if (dest == D_pwm) pwmAdd += a * 0.48; else if (dest == D_fm) fmAdd += a * 5; else if (dest == D_pan) panAdd += a;
                    else if (dest == D_osc1) lev0 += a; else if (dest == D_osc2) lev1 += a; else if (dest == D_osc3) lev2 += a;
                    else return false;
                    return true;
                };
                for (int s = 0; s < modCount; ++s)
                {
                    const ModSlot& slot = S->mod[(size_t) s];
                    if (! slot.present || ! (slot.amount != 0 && slot.amount == slot.amount)) continue;
                    if (slot.target != 3 && slot.target != layerCode) continue;
                    double sg = 1;
                    if (uOn) { const int n = slot.src.lfoN; if (n >= 0 && n < 8) sg = ugp[n]; }
                    const double a = slot.amount * sg * sourceValue (slot.src, voice, l1, l2, wheel, l8On ? lv : nullptr);
                    const int dest = slot.dest;
                    if (applyDest (dest, a)) {}
                    else if (xGate)
                    {
                        if (dest >= 1 && dest <= 39) xd[dest] += dest >= 15 && dest <= 17 ? a * 12 : a;
                        else if (dest == D_gate) gateMul *= 1 - std::min (1.0, std::abs (slot.amount * sg)) * (1 - clamp ((sourceValue (slot.src, voice, l1, l2, wheel, lv) + 1) * 0.5, 0, 1));
                    }
                }
                if (routeAdd)
                {
                    const int mineT = lfoIsB ? 2 : 1;
                    for (int p = 0; p < 16; ++p)
                    {
                        const int li = p & 7; const bool other = p > 7;
                        const bool linked = ! (lfoB8 && lfoB8[li].present);
                        if (other && linked) continue;
                        const int g = other ? (lfoIsB ? li : 8 + li) : (lfoIsB && ! linked ? 8 + li : li);
                        const int f = cf[g];
                        if (! (f & 15)) continue;
                        const Lfo* c = g < 8 ? &lfos[g] : &lfoB8[g - 8];
                        const double y = lv[p]; const int uni = voice.luni[p];
                        for (int k = 0; k < 2; ++k)
                        {
                            if (! (k ? c->hasRoutes : c->hasOsc)) continue;
                            const auto& rs = k ? c->routes : c->osc;
                            for (const Route& ro : rs)
                            {
                                if (! ro.valid) continue;
                                if (ro.t == 0 ? other : (ro.t != 3 && ro.t != mineT)) continue;
                                const double ra = uOn ? ro.a * ugp[p] : ro.a;
                                const double a = ra * y; const int dest = ro.d;
                                if (applyDest (dest, a)) {}
                                else if (dest == D_gate) { const double u = uni ? clamp (y, 0, 1) : clamp ((y + 1) * 0.5, 0, 1); gateMul *= 1 - std::min (1.0, std::abs (ra)) * (1 - u); }
                                else if (dest == D_drip)
                                {
                                    const double u = (uni ? clamp (y, 0, 1) : clamp ((y + 1) * 0.5, 0, 1)) * std::min (1.0, std::abs (ra));
                                    const double sq = (f & 16) ? sAbs[g] : -1;
                                    if (u >= 0.5 && (voice.dtrig[p] == 0 || (sq != -1 && voice.dstep[p] != sq))) { voice.dripKick = 1; voice.dtrig[p] = 1; voice.dstep[p] = sq; }
                                    else if (u < 0.35) voice.dtrig[p] = 0;
                                }
                                else if (dest >= 1 && dest <= 39) xd[dest] += dest >= 15 && dest <= 17 ? a * 12 : a;
                            }
                        }
                    }
                }
                if (gateUsed) { voice.gs += (gateMul - voice.gs) * (1 - std::exp (-1 / (0.0015 * sr))); gateMul = voice.gs; } else voice.gs = 1;

                if (voice.glide > 0.0005) { const double coef = 1 - std::exp (-1 / (voice.glide * sr)); voice.freq += (voice.target - voice.freq) * coef; }
                else voice.freq = voice.target;

                const double character = OR (layer->character, 0);
                voice.driftPhase += (0.15 + character) / sr;
                if (character > 0) { voice.drift += (rng.next() * 2 - 1) * character * 0.00002; voice.drift *= 0.9992; }
                else voice.drift = 0;
                const double driftMul = 1 + std::sin (voice.driftPhase * TAU) * character * 0.004 + voice.drift;
                const double base = std::max (20.0, voice.freq * (pitchAdd != 0 ? std::pow (2.0, pitchAdd / 12) : 1) * bendMul * driftMul);

                const Filter& filter = layer->filter;
                const double filterEnvDepth = layer->filt.depth != layer->filt.depth ? 1 : clamp (layer->filt.depth, -1, 1);
                const double filtVel = clamp (OR (layer->filt.velocity, 0), 0, 1);
                const double filtVelGain = 1 - filtVel + filtVel * voice.vel;
                double oct = (OR (filter.env, 0) + (xGate ? xd[33] : 0)) * filterEnvDepth * filtVelGain * ENV_OCTAVES * voice.fenv;
                oct += OR (filter.lfo, 0) * 2.5 * l1 * (uOn ? ugp[0] : 1);
                oct += ((voice.note - 60) / 12.0) * (OR (filter.key, 0) + (xGate ? xd[34] : 0));
                oct += cutOct;
                const double fcTarget = clamp (OR (filter.cutoff, 1000) * std::pow (2.0, oct), 28, sr * 0.45);
                voice.fc += (fcTarget - voice.fc) * 0.05;

                if (voice.lastType != (int) filter.type)
                {
                    std::fill (std::begin (voice.comb), std::end (voice.comb), 0.f);
                    voice.combDamp = 0; voice.ic1 = voice.ic2 = voice.ic1b = voice.ic2b = 0;
                    voice.lastType = (int) filter.type; voice.ftick = 0;
                }

                const bool doFm = layer->fmMode == FmMode::fm || layer->fmMode == FmMode::both;
                const bool doPd = layer->fmMode == FmMode::pd || layer->fmMode == FmMode::both;
                double mod = 0;
                if (doFm)
                {
                    double ratio = OR (layer->fmRatio, 2);
                    if (xGate && xd[31] != 0) ratio *= std::pow (2.0, xd[31] * 2);
                    const double index = OR (layer->fmIndex, 0) + fmAdd;
                    voice.fmPhase = wrap01 (voice.fmPhase + (base * ratio) / sr);
                    mod = std::sin (TAU * voice.fmPhase) * (index > 0 ? index : 0);
                }
                double pdDepth = doPd ? OR (layer->pdDepth, 0) : 0;
                if (xGate && doPd && xd[12] != 0) pdDepth = clamp (pdDepth + xd[12], 0, 1);
                const PdCurve pdCurve = layer->pdCurve;

                const double* oscRat = oscRatios[voice.layer];
                double mix = 0;
                voice.wrap0 = false; voice.s0 = 0;
                for (int oi = 0; oi < 3; ++oi)
                {
                    const Osc& osc = layer->osc[(size_t) oi];
                    const double baseLevel = OR (osc.level, 0);
                    double level = baseLevel * (1 + (oi == 0 ? lev0 : oi == 1 ? lev1 : lev2));
                    if (level < 0) level = 0; else if (level > 1.6) level = 1.6;
                    if (level < 0.0008) { if (oi == 1) voice.s2prev = 0; continue; }
                    const double freq = clamp (base * oscRat[oi] * (xGate && xd[15 + oi] != 0 ? std::pow (2.0, xd[15 + oi] / 12) : 1), 15, sr * 0.45);
                    const double pwm = clamp (NUL (osc.pwm, 0.5) + pwmAdd + (xGate ? xd[18 + oi] * 0.48 : 0), 0.05, 0.95);
                    const int copies = oi == 0 ? unison : 1;
                    const OscWave wave = osc.wave != OscWave::missing ? osc.wave : OscWave::saw;
                    double fold = OR (osc.fold, 0), ring = OR (osc.ring, 0), sync = OR (osc.sync, 0), xmod = OR (osc.xmod, 0), subAmt = OR (osc.sub, 0);
                    if (xGate)
                    {
                        if (oi == 0) { if (xd[1] != 0) fold = clamp (fold + xd[1], 0, 1); if (xd[4] != 0) xmod = clamp (xmod + xd[4], 0, 1); if (xd[9] != 0) subAmt = clamp (subAmt + xd[9], 0, 1); }
                        else if (oi == 1) { if (xd[2] != 0) fold = clamp (fold + xd[2], 0, 1); if (xd[5] != 0) sync = clamp (sync + xd[5], 0, 1); if (xd[7] != 0) ring = clamp (ring + xd[7], 0, 1); if (xd[10] != 0) subAmt = clamp (subAmt + xd[10], 0, 1); }
                        else { if (xd[3] != 0) fold = clamp (fold + xd[3], 0, 1); if (xd[6] != 0) sync = clamp (sync + xd[6], 0, 1); if (xd[8] != 0) ring = clamp (ring + xd[8], 0, 1); if (xd[11] != 0) subAmt = clamp (subAmt + xd[11], 0, 1); }
                    }
                    double sum = 0;
                    const double* uniRatios = unisonRatios[copies];
                    for (int u = 0; u < copies; ++u)
                    {
                        const double uf = freq * uniRatios[u];
                        double inc = uf / sr;
                        if (sync > 0.001) inc *= 1 + sync * 7;
                        if (xmod > 0.001) inc *= 1 + xmod * 1.2 * (oi == 0 ? voice.s2prev : voice.s0);
                        if (oi == 0 && doFm) inc += mod * inc;
                        if (inc > 0.45) inc = 0.45; else if (inc < -0.45) inc = -0.45;
                        const bool uniVoice = oi == 0 && u > 0;
                        double* store = uniVoice ? voice.uni : voice.phase;
                        const int idx = uniVoice ? u - 1 : oi;
                        if (sync > 0.001 && (oi == 0 ? voice.wrap2 : voice.wrap0)) store[idx] = 0;
                        const double phase = store[idx];
                        const double read = oi == 0 && pdDepth > 0 ? warpPhase (phase, pdDepth, pdCurve) : phase;
                        const double adt = inc < 0 ? -inc : inc;
                        if (wave == OscWave::noise || wave == OscWave::pink)
                        {
                            const double wn = rng.next() * 2 - 1; double nv = wn;
                            if (wave == OscWave::pink) { double* pk = voice.pk; pk[0] = 0.99765 * pk[0] + wn * 0.099046; pk[1] = 0.963 * pk[1] + wn * 0.2965164; pk[2] = 0.57 * pk[2] + wn * 1.0526913; nv = (pk[0] + pk[1] + pk[2] + wn * 0.1848) * 0.22; }
                            const double hold = (pwm - 0.05) / 0.9; const int hi = oi * 4 + u;
                            const double tone = (wave == OscWave::pink ? voice.npk : voice.nw)[hi * 64 + (((int) (phase * 64)) & 63)];
                            sum += nv * (1 - hold) + tone * hold;
                        }
                        else sum += oscSample (wave, read, adt < 1e-5 ? 1e-5 : adt, pwm);
                        store[idx] = wrap01 (phase + inc);
                        if (oi == 0 && u == 0) voice.wrap0 = phase + inc >= 1;
                        if (oi == 1 && u == 0) voice.wrap2 = phase + inc >= 1;
                    }
                    double os = sum / copies;
                    if (oi == 0) voice.s0 = os;
                    if (oi == 1) voice.s2prev = os;
                    if (ring > 0.001) { const double oth = oi == 0 ? voice.s2prev : voice.s0; os = os * (1 - ring) + os * oth * ring * 1.6; }
                    if (fold > 0.001) { const double fg = 1 + fold * 5; os = os * (1 - fold) + fold * std::sin (os * fg * 1.5707963); }
                    if (subAmt > 0.001)
                    {
                        if (oi == 0) { voice.subPh = wrap01 (voice.subPh + freq * 0.5 / sr); os += std::sin (TAU * voice.subPh) * subAmt; }
                        else if (oi == 1) { voice.subPh2 = wrap01 (voice.subPh2 + freq * 0.5 / sr); os += std::sin (TAU * voice.subPh2) * subAmt; }
                        else { voice.subPh3 = wrap01 (voice.subPh3 + freq * 0.5 / sr); os += std::sin (TAU * voice.subPh3) * subAmt; }
                    }
                    mix += os * level;
                }
                mix *= 0.5;

                const double q = resonanceQ (OR (filter.res, 0) + resAdd);
                double fdrive = OR (filter.drive, 0);
                if (xGate && xd[13] != 0) fdrive = clamp (fdrive + xd[13], 0, 1);
                if (fdrive > 0.001) { const double dg = 1 + fdrive * 7; mix = std::tanh (mix * dg) / std::sqrt (dg); }
                double shaped = mix, dripL = 0, dripR = 0; bool dripSt = false;
                if (filter.type == FiltType::comb)
                {
                    const int size = 4096;
                    double d = sr / clamp (voice.fc, 30, 8000);
                    if (d > size - 3) d = size - 3;
                    if (d < 2) d = 2;
                    double rp = voice.combPos - d; if (rp < 0) rp += size;
                    const int i0 = (int) rp; const double frac = rp - i0; const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
                    const double delayed = voice.comb[i0] * (1 - frac) + voice.comb[i1] * frac;
                    const double combRaw = NUL (filter.comb, 0);
                    double fb = combRaw > 0.92 ? 0.92 : combRaw > 0 ? combRaw : 0;
                    if (xGate && xd[26] != 0) fb = clamp (fb + xd[26] * 0.92, 0, 0.92);
                    voice.combDamp += (delayed - voice.combDamp) * 0.35;
                    voice.comb[voice.combPos] = (float) (mix + voice.combDamp * fb);
                    voice.combPos++; if (voice.combPos >= size) voice.combPos = 0;
                    shaped = delayed;
                }
                else if (filter.type == FiltType::formant)
                {
                    voice.ftick--;
                    const double qf = 5 + OR (filter.res, 0) * 14;
                    if (voice.ftick <= 0)
                    {
                        const double vw = clamp (NUL (filter.vowel, 0) + (xGate ? xd[14] : 0), 0, 1) * 4; const int vi = std::min (3, (int) std::floor (vw)); const double vf = vw - vi;
                        const double ratio = clamp (voice.fc, 40, 14000) / 1000;
                        for (int f = 0; f < 3; ++f)
                        {
                            const double fr = clamp ((FORMANTS[vi][f] + (FORMANTS[vi + 1][f] - FORMANTS[vi][f]) * vf) * ratio, 60, sr * 0.45);
                            const double g = std::tan (PI * fr / sr), k = 1 / qf, a1 = 1 / (1 + g * (g + k)), a2 = g * a1;
                            voice.fcoef[f * 3] = a1; voice.fcoef[f * 3 + 1] = a2; voice.fcoef[f * 3 + 2] = g * a2;
                        }
                        voice.ftick = 8;
                    }
                    double acc = 0;
                    for (int f = 0; f < 3; ++f)
                    {
                        const double a1 = voice.fcoef[f * 3], a2 = voice.fcoef[f * 3 + 1], a3 = voice.fcoef[f * 3 + 2];
                        const double fv3 = mix - voice.fic2[f], fv1 = a1 * voice.fic1[f] + a2 * fv3, fv2 = voice.fic2[f] + a2 * voice.fic1[f] + a3 * fv3;
                        voice.fic1[f] = 2 * fv1 - voice.fic1[f]; voice.fic2[f] = 2 * fv2 - voice.fic2[f];
                        if (! finite (voice.fic1[f]) || voice.fic1[f] > 6 || voice.fic1[f] < -6) { voice.fic1[f] = 0; voice.fic2[f] = 0; }
                        acc += fv1 * FORMANT_GAIN[f];
                    }
                    shaped = acc * FORMANT_TRIM / std::sqrt (1 + qf * 0.22);
                }
                else if (filter.type == FiltType::drip)
                {
                    const double dripRes = clamp (OR (filter.res, 0) + resAdd, 0, 1);
                    const double dripQ = 6 + dripRes * dripRes * 480;
                    double ping = filter.ping != filter.ping ? 0.85 : clamp (filter.ping, 0, 1);
                    double sweep = filter.sweep != filter.sweep ? 2.2 : clamp (filter.sweep, -4, 4);
                    double fallT = filter.fall != filter.fall ? 0.055 : clamp (filter.fall, 0.02, 0.45);
                    double bubble = filter.bubble != filter.bubble ? 0.55 : clamp (filter.bubble, 0, 1);
                    double wob = filter.fm != filter.fm ? 0.4 : clamp (filter.fm, 0, 1);
                    double chirp = filter.chirp != filter.chirp ? 0 : clamp (filter.chirp, 0, 1);
                    double dwidth = filter.dwidth != filter.dwidth ? 0 : clamp (filter.dwidth, 0, 1);
                    double density = filter.density != filter.density ? 0.5 : clamp (filter.density, 0, 1);
                    double dsize = filter.dsize != filter.dsize ? 0.5 : clamp (filter.dsize, 0, 1);
                    double dguard = filter.dguard != filter.dguard ? 0 : clamp (filter.dguard, 0, 1);
                    if (xGate)
                    {
                        if (xd[21] != 0) ping = clamp (ping + xd[21], 0, 1);
                        if (xd[22] != 0) sweep = clamp (sweep + xd[22] * 4, -4, 4);
                        if (xd[23] != 0) fallT = clamp (fallT + xd[23] * 0.2, 0.02, 0.45);
                        if (xd[24] != 0) bubble = clamp (bubble + xd[24], 0, 1);
                        if (xd[25] != 0) wob = clamp (wob + xd[25], 0, 1);
                        if (xd[35] != 0) chirp = clamp (chirp + xd[35], 0, 1);
                        if (xd[36] != 0) dwidth = clamp (dwidth + xd[36], 0, 1);
                        if (xd[37] != 0) density = clamp (density + xd[37], 0, 1);
                        if (xd[38] != 0) dsize = clamp (dsize + xd[38], 0, 1);
                        if (xd[39] != 0) dguard = clamp (dguard + xd[39], 0, 1);
                    }
                    const bool styleStream = filter.dripStyleStream, styleSplash = filter.dripStyleSplash, hasStyle = styleStream || styleSplash;
                    const DripMode dripMode = filter.dripMode;
                    const bool storm = filter.dripStorm;
                    if (filter.drip3d) { chirp = 1; if (dwidth < 0.8) dwidth = 0.8; }
                    if (voice.dripKick)
                    {
                        voice.dripKick = 0;
                        if (dripMode != DripMode::even)
                        {
                            voice.dripWait = 1; voice.splashN = 3 + (int) (rng.next() * 4);
                            if (storm) { voice.stW = 1 + rng.next() * fallT * sr; voice.spW = fallT * sr * (0.5 + rng.next() * 2); voice.dripKind = 0.55; }
                        }
                        if (dripMode != DripMode::rain) voice.dripT0 = (double) voice.age;
                    }
                    const double dAge = (double) voice.age - voice.dripT0;
                    if (storm && voice.gate)
                    {
                        const double dens = density == 0.5 ? 1 : std::pow (2.0, (0.5 - density) * 4);
                        voice.dripWait -= 1; voice.stW -= 1; voice.spW -= 1;
                        int kind = 0;
                        if (voice.dripWait <= 0) { kind = 1; const double r = rng.next(); voice.dripWait = std::max (12.0, fallT * sr * (0.28 + r * r * 7.4) * dens * 1.6); }
                        else if (voice.spW <= 0)
                        {
                            kind = 2; const double r = rng.next();
                            if (voice.splashN > 0) { voice.splashN -= 1; voice.spW = std::max (12.0, fallT * sr * (0.1 + r * 0.28) * dens); }
                            else { voice.splashN = 2 + (int) (rng.next() * 4); voice.spW = std::max (12.0, fallT * sr * (3 + r * 10) * dens); }
                        }
                        else if (voice.stW <= 0) { kind = 3; const double r = rng.next(); voice.stW = std::max (12.0, fallT * sr * (0.1 + r * 0.4) * dens * 1.5); }
                        if (kind)
                        {
                            voice.dripAge = 0; voice.dripHit = 3;
                            const double sizeRoll = rng.next();
                            voice.dripSize = (0.12 + sizeRoll * sizeRoll * 0.88) * (kind == 3 ? 0.4 : kind == 2 ? 0.8 : 1);
                            if (dsize != 0.5) voice.dripSize = clamp (voice.dripSize * std::pow (2.0, (dsize - 0.5) * 2.4), 0.04, 1.5);
                            voice.dripJit = rng.next() * 2 - 1; voice.dripRatio = rng.next() * 0.7 - 0.22;
                            voice.dripAmp = rng.next() * 2 - 1; voice.dripRate = 0.25 + rng.next() * 0.9;
                            voice.dripKind = kind == 1 ? 0.55 : kind == 2 ? 0.45 : 0.28;
                        }
                    }
                    if (dripMode != DripMode::even && voice.gate && ! storm)
                    {
                        voice.dripWait -= 1;
                        if (voice.dripWait <= 0)
                        {
                            const double gapRoll = rng.next();
                            const double dens = density == 0.5 ? 1 : std::pow (2.0, (0.5 - density) * 4);
                            double gap = fallT * sr * (0.28 + gapRoll * gapRoll * 7.4) * dens;
                            if (hasStyle && styleStream) gap = fallT * sr * (0.06 + gapRoll * 0.24) * dens;
                            else if (hasStyle && styleSplash)
                            {
                                if (voice.splashN > 0) { voice.splashN -= 1; gap = fallT * sr * (0.1 + gapRoll * 0.28) * dens; }
                                else { voice.splashN = 2 + (int) (rng.next() * 5); gap = fallT * sr * (2.5 + gapRoll * 9) * dens; }
                            }
                            voice.dripWait = gap < 12 ? 12 : gap;
                            voice.dripAge = 0; voice.dripHit = 3;
                            const double sizeRoll = rng.next();
                            voice.dripSize = 0.12 + sizeRoll * sizeRoll * 0.88;
                            if (dsize != 0.5) voice.dripSize = clamp (voice.dripSize * std::pow (2.0, (dsize - 0.5) * 2.4), 0.04, 1.5);
                            if (hasStyle && styleStream) voice.dripSize *= 0.45;
                            voice.dripJit = rng.next() * 2 - 1; voice.dripRatio = rng.next() * 0.7 - 0.22;
                            voice.dripAmp = rng.next() * 2 - 1; voice.dripRate = 0.25 + rng.next() * 0.9;
                        }
                    }
                    if (voice.dripHit > 0) voice.dripHit -= 1;
                    voice.dripAge += 1;
                    double fall, swept, wobble, dripRatio;
                    if (dripMode == DripMode::rain)
                    {
                        fall = std::exp (-voice.dripAge / (fallT * sr * (0.45 + voice.dripSize * 0.9)));
                        if (chirp > 0) fall *= std::pow (fall, chirp);
                        const double scatter = voice.dripJit * (0.35 + std::abs (sweep) * 0.2);
                        swept = voice.fc * std::pow (2.0, sweep * fall + scatter * (0.45 + 0.55 * fall));
                        wobble = std::sin (TAU * voice.dripPh) * wob * (0.1 + 0.32 * voice.dripSize);
                        dripRatio = 1.28 + voice.dripRatio + 0.22;
                    }
                    else if (dripMode == DripMode::both)
                    {
                        fall = std::exp (-dAge / (fallT * sr));
                        double dropFall = std::exp (-voice.dripAge / (fallT * sr * (0.5 + voice.dripSize)));
                        if (chirp > 0) { fall *= std::pow (fall, chirp); dropFall *= std::pow (dropFall, chirp); }
                        const double scatter = voice.dripJit * dropFall * (0.22 + std::abs (sweep) * 0.12);
                        swept = voice.fc * std::pow (2.0, sweep * fall + scatter);
                        wobble = std::sin (TAU * voice.dripPh) * wob * 0.28 * (1 + 0.55 * voice.dripAmp * dropFall);
                        dripRatio = 1.51 + voice.dripRatio * dropFall * 0.45;
                    }
                    else
                    {
                        fall = std::exp (-dAge / (fallT * sr));
                        if (chirp > 0) fall *= std::pow (fall, chirp);
                        swept = voice.fc * std::pow (2.0, sweep * fall);
                        wobble = std::sin (TAU * voice.dripPh) * wob * 0.28;
                        dripRatio = 1.51;
                    }
                    voice.dripEnv = dripMode == DripMode::both ? std::max (fall, std::exp (-voice.dripAge / (fallT * sr * (0.5 + voice.dripSize)))) : fall;
                    const double fc = clamp (swept * (1 + wobble), 32, sr * 0.45);
                    const double rateMul = dripMode == DripMode::rain ? voice.dripRate : 1;
                    voice.dripPh = wrap01 (voice.dripPh + (fc * (0.42 + dripRes * 0.5) * rateMul) / sr);
                    const double g = std::tan (PI * fc / sr), k = 1 / dripQ, a1 = 1 / (1 + g * (g + k)), a2 = g * a1;
                    voice.fg = g; voice.fk = k; voice.fa1 = a1; voice.fa2 = a2; voice.fa3 = g * a2;
                    const double gb = std::tan (PI * clamp (fc * dripRatio, 40, sr * 0.45) / sr);
                    const double kb = 1 / std::max (1.6, dripQ * 0.62);
                    const double b1 = 1 / (1 + gb * (gb + kb)), b2 = gb * b1;
                    voice.fcoef[0] = b1; voice.fcoef[1] = b2; voice.fcoef[2] = gb * b2;
                    double exc = mix;
                    if (dripMode != DripMode::rain && dAge < 5 && ping > 0.001) exc += (dAge < 2 ? 0.9 : 0.16) * ping * (storm ? 0.6 : 1);
                    if (dripMode != DripMode::even && voice.dripHit > 0 && ping > 0.001) exc += (voice.dripHit > 1 ? 0.8 : 0.18) * ping * voice.dripSize * (storm ? voice.dripKind : 1) * voice.dGuard;
                    const double v3 = exc - voice.ic2, v1 = voice.fa1 * voice.ic1 + voice.fa2 * v3, v2 = voice.ic2 + voice.fa2 * voice.ic1 + voice.fa3 * v3;
                    voice.ic1 = 2 * v1 - voice.ic1; voice.ic2 = 2 * v2 - voice.ic2;
                    if (! finite (voice.ic1) || voice.ic1 > 6 || voice.ic1 < -6) { voice.ic1 = 0; voice.ic2 = 0; }
                    const double w3 = exc - voice.ic2b, w1 = b1 * voice.ic1b + b2 * w3, w2 = voice.ic2b + b2 * voice.ic1b + voice.fcoef[2] * w3;
                    voice.ic1b = 2 * w1 - voice.ic1b; voice.ic2b = 2 * w2 - voice.ic2b;
                    if (! finite (voice.ic1b) || voice.ic1b > 6 || voice.ic1b < -6) { voice.ic1b = 0; voice.ic2b = 0; }
                    shaped = (v1 * 0.74 + v2 * 0.28 + w1 * bubble) / std::sqrt (1 + dripQ * 0.012);
                    {
                        const double pk = std::abs (shaped);
                        voice.dgLvl = pk > voice.dgLvl ? pk : voice.dgLvl * 0.9997;
                        if (dguard > 0.001)
                        {
                            if (voice.dgLvl > 2.6 - dguard * 1.4) voice.dGuard = std::max (0.4 - dguard * 0.2, voice.dGuard * 0.997);
                            else if (voice.dGuard < 1) voice.dGuard = std::min (1.0, voice.dGuard + 0.00002);
                        }
                        else voice.dGuard = 1;
                        if (voice.dgLvl > 6) { voice.ic1 *= 0.5; voice.ic2 *= 0.5; voice.ic1b *= 0.5; voice.ic2b *= 0.5; voice.dgLvl *= 0.5; }
                        if (voice.dGuard != 1) shaped *= voice.dGuard;
                    }
                    if (dwidth > 0.001)
                    {
                        const double norm = 1 / std::sqrt (1 + dripQ * 0.012), pri = (v1 * 0.74 + v2 * 0.28) * norm, sec = w1 * bubble * norm;
                        float* dl = voice.dripDelay; dl[voice.dripDPos] = (float) sec;
                        const int tap = (voice.dripDPos - (2 + (int) (dwidth * (6 + bubble * 14 + std::abs (voice.dripJit) * 5))) + 32) & 31;
                        voice.dripDPos = (voice.dripDPos + 1) & 31;
                        dripL = pri + sec * 0.9; dripR = pri + (sec * (1 - dwidth) + dl[tap] * dwidth) * 0.9;
                        if (voice.dGuard != 1) { dripL *= voice.dGuard; dripR *= voice.dGuard; }
                        if (finite (dripL) && finite (dripR)) dripSt = true; else std::fill (dl, dl + 32, 0.f);
                    }
                }
                else
                {
                    voice.ftick--;
                    if (voice.ftick <= 0)
                    {
                        const double g = std::tan (PI * clamp (voice.fc, 20, sr * 0.45) / sr), k = 1 / q, a1 = 1 / (1 + g * (g + k)), a2 = g * a1;
                        voice.fg = g; voice.fk = k; voice.fa1 = a1; voice.fa2 = a2; voice.fa3 = g * a2; voice.ftick = 8;
                    }
                    const double v0 = mix, v3 = v0 - voice.ic2, v1 = voice.fa1 * voice.ic1 + voice.fa2 * v3, v2 = voice.ic2 + voice.fa2 * voice.ic1 + voice.fa3 * v3;
                    voice.ic1 = 2 * v1 - voice.ic1; voice.ic2 = 2 * v2 - voice.ic2;
                    if (! finite (voice.ic1) || voice.ic1 > 6 || voice.ic1 < -6) { voice.ic1 = 0; voice.ic2 = 0; }
                    const FiltType type = filter.type;
                    if (type == FiltType::highpass) shaped = v0 - voice.fk * v1 - v2;
                    else if (type == FiltType::bandpass) shaped = v1;
                    else if (type == FiltType::notch) shaped = v0 - voice.fk * v1;
                    else shaped = v2;
                    double slope = OR (filter.slope, 0);
                    if (xGate && xd[32] != 0) slope = clamp (slope + xd[32], 0, 1);
                    if (slope > 0.001)
                    {
                        const double s1 = shaped, w0 = s1, w3 = w0 - voice.ic2b, w1 = voice.fa1 * voice.ic1b + voice.fa2 * w3, w2 = voice.ic2b + voice.fa2 * voice.ic1b + voice.fa3 * w3;
                        voice.ic1b = 2 * w1 - voice.ic1b; voice.ic2b = 2 * w2 - voice.ic2b;
                        if (! finite (voice.ic1b) || voice.ic1b > 6 || voice.ic1b < -6) { voice.ic1b = 0; voice.ic2b = 0; }
                        double s2;
                        if (type == FiltType::highpass) s2 = w0 - voice.fk * w1 - w2;
                        else if (type == FiltType::bandpass) s2 = w1;
                        else if (type == FiltType::notch) s2 = w0 - voice.fk * w1;
                        else s2 = w2;
                        shaped = s1 + (s2 - s1) * slope;
                    }
                    shaped *= 1 / std::sqrt (1 + q * 0.22);
                }

                if (! finite (shaped)) shaped = 0;
                const double level = NUL (layer->level, 0.8);
                const double velocitySensitivity = clamp (OR (layer->amp.velocity, 0), 0, 1);
                const double velocityGain = (0.25 + 0.75 * voice.vel) * (1 - velocitySensitivity + velocitySensitivity * voice.vel);
                const double ampDepth = layer->amp.depth != layer->amp.depth ? 1 : clamp (layer->amp.depth, -1, 1);
                const double ampEnv = ampDepth >= 0 ? voice.amp * ampDepth : (1 - voice.amp) * -ampDepth;
                const double amp = ampEnv * velocityGain * level * gateMul;
                const double pan = clamp ((voice.note - 60) / 72.0 * 0.4 + panAdd, -1, 1);
                const double angle = (pan + 1) * 0.25 * PI;
                if (dripSt)
                {
                    busL += std::tanh (dripL * amp * 1.35) * std::cos (angle);
                    busR += std::tanh (dripR * amp * 1.35) * std::sin (angle);
                }
                else
                {
                    shaped = std::tanh (shaped * amp * 1.35);
                    busL += shaped * std::cos (angle);
                    busR += shaped * std::sin (angle);
                }
            }
        }

        if (duck > 0)
        {
            const double bp = std::fmod (std::fmod (t - l8anchor, beatDur) + beatDur, beatDur) / beatDur;
            const double g = 1 - duck * (1 - (bp < 0.03 ? 1 - bp / 0.03 : std::pow (std::min (1.0, (bp - 0.03) / duckRel), duckExp)));
            busL *= g; busR *= g;
        }
        if (cbOn)
        {
            auto rd = [this] (int c, double d) { double r = cbW - d; if (r < 0) r += 4096; const int i0 = (int) r; const double f = r - i0; const float a = cbBuf[c][(size_t) i0], b = cbBuf[c][(size_t) ((i0 + 1) & 4095)]; return a + (b - a) * f; };
            const double yl = rd (0, cbDL), yr = rd (1, cbDR);
            cbLp[0] += (yl - cbLp[0]) * cbA; cbLp[1] += (yr - cbLp[1]) * cbA;
            cbBuf[0][(size_t) cbW] = (float) (busL + cbLp[0] * cbFb); cbBuf[1][(size_t) cbW] = (float) (busR + cbLp[1] * cbFb); cbW = (cbW + 1) & 4095;
            busL = (float) (busL * (1 - cbMix * 0.4) + cbLp[0] * cbMix); busR = (float) (busR * (1 - cbMix * 0.4) + cbLp[1] * cbMix);
        }
        if (efOn)
        {
            const double lv = std::max (std::abs ((double) busL), std::abs ((double) busR)); efE += (lv - efE) * (lv > efE ? efAtt : efRel);
            if ((efN++ & 15u) == 0) { const double f = 180 + std::min (1.0, efE * efGain) * efSpan; lpCoef (efC, 0, f, efQ); lpCoef (efC, 5, f * 1.15, 0.9); }
            busL = (float) lpTick (efC, 5, efZ, 4, lpTick (efC, 0, efZ, 0, busL)); busR = (float) lpTick (efC, 5, efZ, 6, lpTick (efC, 0, efZ, 2, busR));
        }
        if (fgOn)
        {
            const double k16 = std::floor (((t - l8anchor) / beatDur) * 4); const int idx = (((long long) k16 % 16) + 16) % 16; const bool act = S->fgSteps[idx] != 0;
            fgF += ((act ? fgOpen : fgClosed) - fgF) * fgKf; fgG += ((act ? 1.0 : fgLow) - fgG) * fgKg;
            if ((fgN++ & 15u) == 0) { fgCoef (0, fgF, fgQ); fgCoef (5, fgF, 0.70710678); }
            busL = (float) (fgTick (5, 4, fgTick (0, 0, busL)) * fgG); busR = (float) (fgTick (5, 6, fgTick (0, 2, busR)) * fgG);
        }
        else fgInit = false;
        busL *= 0.72; busR *= 0.72;
        const double dcL = busL - dcInL + 0.995 * dcOutL; dcInL = busL; dcOutL = dcL;
        const double dcR = busR - dcInR + 0.995 * dcOutR; dcInR = busR; dcOutR = dcR;
        Lch[i] = (float) (dcL == dcL ? dcL : 0);
        Rch[i] = (float) (dcR == dcR ? dcR : 0);
        scope[scopePos++] = (float) ((Lch[i] + Rch[i]) * 0.5);
        if (scopePos >= 160) scopePos = 0;
    }
    int alive = 0;
    for (auto& v : voices) if (v->active) alive++;
    activeCount = alive;
}
} // namespace mosaic
