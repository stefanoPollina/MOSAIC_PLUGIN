#include "TableEngine.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace mosaic
{
namespace
{
using json::Value;
const int MIPS[10] = { 512, 256, 128, 64, 32, 16, 8, 4, 2, 1 };
const double WINDOWS[8][2] = { { 0, .12 }, { .13, .25 }, { .26, .37 }, { .38, .50 }, { .51, .62 }, { .63, .75 }, { .76, .87 }, { .88, 1 } };
const int JUMPS[7] = { -24, -12, 0, 7, 12, 19, 24 };
const int HIJAZ[8] = { 0, 1, 4, 5, 7, 8, 10, 12 };
double rateBeats (const std::string& r) { return r == "1/32" ? .125 : r == "1/8" ? .5 : r == "1/4" ? 1 : .25; }
inline double clampd (double x, double a, double b) { return x < a ? a : x > b ? b : x; }
double jsNumber (const Value& v, double ifNaN)   // Number(v) || fallback
{
    double x = NAN;
    if (v.type == Value::Number) x = v.num; else if (v.type == Value::Bool) x = v.b ? 1 : 0;
    else if (v.type == Value::String) { char* e; x = std::strtod (v.str.c_str(), &e); if (*e || v.str.empty()) x = v.str.empty() ? 0 : NAN; }
    else if (v.type == Value::Null) x = 0;
    return (x != x || x == 0) ? ifNaN : x;
}
double clampN (const Value& v, double a, double b, double d) { if (v.type != Value::Number && v.type != Value::Bool && v.type != Value::String) { if (v.isNull()) return clampd (0, a, b); return d; } const double x = v.type == Value::Bool ? (v.b ? 1 : 0) : v.type == Value::String ? std::strtod (v.str.c_str(), nullptr) : v.num; return std::isfinite (x) ? clampd (x, a, b) : d; }
uint32_t imul (uint32_t a, uint32_t b) { return a * b; }
double hash01 (long long a, long long b)
{
    uint32_t h = imul ((uint32_t) (int32_t) a, 0x9E3779B1u) ^ imul ((uint32_t) ((int32_t) b + 1), 0x85EBCA77u);
    h ^= h >> 15; h = imul (h, 0x2C1B3C6Du); h ^= h >> 12; return h / 4294967296.0;
}

Motion normMotion (const Value& m)
{
    Motion o;
    for (int i = 0; i < 8; ++i) o.lanes[(size_t) i].name = "Lane " + std::to_string (i + 1);
    if (! m.isObject()) return o;
    o.on = m["on"].truthy(); o.scene = clampN (m["scene"], 0, 1, .06); o.all = m["mode"].type == Value::String && m["mode"].str == "all";
    const std::string r = m["rate"].type == Value::String ? m["rate"].str : ""; o.rate = (r == "1/32" || r == "1/16" || r == "1/8" || r == "1/4") ? r : "1/16";
    o.swing = clampN (m["swing"], 0, .36, 0); o.slew = clampN (m["slew"], 0, 1, 0);
    for (int i = 0; i < 8; ++i)
    {
        const Value& L = m["lanes"].isArray() && m["lanes"][(size_t) i].truthy() ? m["lanes"][(size_t) i] : Value();
        MotionLane& ln = o.lanes[(size_t) i];
        ln.semis = L["unit"].type == Value::String && L["unit"].str == "st"; const double mx = ln.semis ? 24 : 10;
        for (int k = 0; k < 16; ++k)
        {
            const Value& s = L["steps"].isArray() ? L["steps"][(size_t) k] : Value();
            if (s.type == Value::String && (s.str == "R" || s.str == "Random")) { ln.rnd[(size_t) k] = true; ln.steps[(size_t) k] = 0; }
            else ln.steps[(size_t) k] = clampN (s, 0, mx, 0);
        }
        if (L["targets"].isArray())
            for (size_t k = 0; k < L["targets"].arr.size() && k < 3; ++k)
            {
                const Value& t = L["targets"].arr[k]; if (! t.isObject()) continue; const std::string d = t["dest"].strOr ("");
                if (! (d == "scan" || d == "cutoff" || d == "res" || d == "level" || d == "pitch" || d == "fm" || d == "pan")) continue;
                ln.targets.push_back ({ d, clampN (t["depth"], 0, d == "pitch" ? 24 : 1, 0), t["alt"].truthy() });
            }
        if (L["name"].type == Value::String && ! L["name"].str.empty()) ln.name = L["name"].str.substr (0, 32);
    }
    return o;
}

// the page's starterCfg (including its fall-through: non-Acid bass tables end in the last branch)
TableSettings starterFor (const Value& t)
{
    static const std::map<std::string, std::string> CAT { { "Neuro", "Bass" }, { "FM", "Bass" }, { "Reece", "Bass" }, { "Jungle", "Bass" }, { "Sub", "Bass" }, { "Meta", "Bass" },
        { "Formant", "Pad" }, { "Motion", "Atmosphere" }, { "Spectral", "Atmosphere" }, { "Chaos", "Atmosphere" }, { "Tribal", "Pluck" }, { "World", "Pad" }, { "Acid", "Bass" } };
    const std::string cat = t["category"].strOr (""); auto it = CAT.find (cat); const std::string d = it != CAT.end() ? it->second : "Wavetable";
    TableSettings c; c.on = true; c.table = t["id"].str; c.scan = .5; c.M1 = c.M2 = c.M3 = 0; c.M4 = .25; c.level = .6;
    c.unison = (int) t["voice"]["unison"].orNum (1); c.detune = t["voice"]["detuneCents"].orNum (12);
    if (d == "Bass") { c.octave = -1; c.attack = .004; c.decay = .28; c.sustain = .8; c.release = .18; c.cutoff = 5200; c.res = 1.4; c.M2 = .15; }
    if (cat == "Acid") { c.cutoff = 2600; c.res = 5; c.M2 = .1; c.M4 = 0; }
    else if (d == "Pad" || d == "Atmosphere") { c.octave = 0; c.attack = .7; c.decay = .6; c.sustain = .8; c.release = 1.8; c.cutoff = 7000; c.res = .8; c.unison = std::max (c.unison, 3); c.detune = std::max (c.detune, 14.0); c.M4 = .5; }
    else if (d == "Pluck") { c.octave = 0; c.attack = .002; c.decay = .7; c.sustain = 0; c.release = .6; c.cutoff = 12000; c.res = .7; c.M4 = 0; }
    else { c.octave = 0; c.attack = .01; c.decay = .3; c.sustain = .7; c.release = .35; c.cutoff = 9500; c.res = .9; }
    return c;
}

wa::WavePtr waveFrom (const std::vector<float>& re, const std::vector<float>& im, int H, double sr)
{
    return std::make_shared<wa::PeriodicWave> (sr, re.data(), im.data(), std::min (H + 1, (int) re.size()));
}
} // namespace

std::shared_ptr<TableWaves> buildTableWaves (const wt::Bank& bank, const TableSettings& S, double sr)
{
    auto w = std::make_shared<TableWaves>(); const Value* def = bank.find (S.table); if (! def) return nullptr;
    wt::Macros mv; mv.given = true; mv.M1 = S.M1; mv.M2 = S.M2; mv.M3 = S.M3; mv.M4 = S.M4;
    const auto st = wt::renderStereo (*def, S.scan, bank, mv);
    std::vector<float> re, im; wt::waveCoefficients (st.L, 512, re, im);
    for (int k = 0; k < 10; ++k) w->L[(size_t) k] = waveFrom (re, im, MIPS[k], sr);
    w->stereo = ! st.same;
    if (w->stereo) { wt::waveCoefficients (st.R, 512, re, im); for (int k = 0; k < 10; ++k) w->R[(size_t) k] = waveFrom (re, im, MIPS[k], sr); }
    else w->R = w->L;
    w->pitchDiv = (*def)["voice"]["pitchDiv"].orNum (1);
    return w;
}

void TableEngine::prepare (double sampleRate)
{
    sr = sampleRate; voices.clear(); held.clear(); bus.init (sr, S.level); nowFrame = 0;
    std::vector<float> re (2, 0.f), im (2, 0.f); im[1] = 1;   // Chrome's built-in sine is a PeriodicWave too
    sine = std::make_shared<wa::PeriodicWave> (sr, re.data(), im.data(), 2);
}

std::string TableEngine::waveKey() const
{
    char b[256]; std::snprintf (b, sizeof b, "%s|%.17g|%.17g|%.17g|%.17g|%.17g", S.table.c_str(), S.scan, S.M1, S.M2, S.M3, S.M4); return b;
}

std::vector<std::string> TableEngine::starterNames() const
{
    std::vector<std::string> n; if (bank) for (auto& t : bank->json["tables"].arr) n.push_back ("WT " + t["name"].str); return n;
}

TableSettings TableEngine::parseSettings (const Value& state, const wt::Bank& bk)
{
    TableSettings n; if (! state.isObject()) return n;
    std::string name = state["name"].type == Value::String ? state["name"].str : "";
    { const size_t p = name.rfind (" \xC2\xB7 "); if (p != std::string::npos) { bool ok = p + 4 < name.size(); for (size_t i = p + 4; i < name.size(); ++i) { const char c = name[i]; if (! (std::isalpha ((unsigned char) c) || c == ' ')) ok = false; } if (ok) name = name.substr (0, p); } }
    const Value* starter = nullptr; for (auto& t : bk.json["tables"].arr) if ("WT " + t["name"].str == name) { starter = &t; break; }
    // Object.assign(S, D, cfg): cfg is state.wtLab, or the starter preset this state is named after, or "off"
    const Value& w = state["wtLab"];
    if (w.isObject())
    {
        auto num = [&] (const char* k, double& dst) { const Value& x = w[k]; if (x.type == Value::Number) dst = x.num; else if (x.type == Value::Bool) dst = x.b; else if (x.type == Value::String) dst = std::strtod (x.str.c_str(), nullptr); };
        n.on = w["on"].truthy(); if (w["table"].type == Value::String) n.table = w["table"].str;
        num ("scan", n.scan); num ("M1", n.M1); num ("M2", n.M2); num ("M3", n.M3); num ("M4", n.M4); num ("level", n.level); num ("attack", n.attack); num ("decay", n.decay);
        num ("sustain", n.sustain); num ("release", n.release); num ("cutoff", n.cutoff); num ("res", n.res); num ("detune", n.detune); num ("fmDepth", n.fmDepth); num ("selfFm", n.selfFm); num ("fmRatio", n.fmRatio);
        double u = n.unison, o = n.octave; num ("unison", u); num ("octave", o); n.unison = (int) std::floor (u + .5); n.octave = (int) o;
        if (w["follow"].type == Value::String) n.follow = w["follow"].str;
        if (w["mode"].type == Value::String) n.mode = w["mode"].str;
        n.motion = normMotion (w["motion"]);
    }
    else if (starter) n = starterFor (*starter);
    else n.on = false;
    auto unit = [] (double x) { return clampd ((x != x || x == 0) ? 0 : x, 0, 1); };   // Math.max(0, Math.min(1, Number(x) || 0))
    n.scan = unit (n.scan); n.M1 = unit (n.M1); n.M2 = unit (n.M2); n.M3 = unit (n.M3); n.M4 = unit (n.M4); n.level = unit (n.level); n.fmDepth = unit (n.fmDepth);
    n.selfFm = unit (n.selfFm); n.fmRatio = clampd ((n.fmRatio != n.fmRatio || n.fmRatio == 0) ? 2 : n.fmRatio, .5, 16);
    n.unison = std::max (1, std::min (7, n.unison == 0 ? 1 : n.unison));
    if (! bk.find (n.table)) n.table = "WT17";
    if (! (n.follow == "AUTO" || n.follow == "A" || n.follow == "B" || n.follow == "A+B")) n.follow = "A";
    if (! (n.mode == "replace" || n.mode == "blend")) n.mode = "replace";
    n.poly = 12;
    return n;
}

void TableEngine::applySettings (const TableSettings& n, double now)
{
    const bool oldOn = S.on; const std::string oldRoute = S.follow, oldMode = S.mode;
    const double oldLevel = S.level, oldCut = S.cutoff, oldRes = S.res, oldFm = S.fmDepth;
    S = n;
    if ((oldOn && ! S.on) || oldRoute != S.follow || oldMode != S.mode) panic (now);
    if (oldLevel != S.level || oldCut != S.cutoff || oldRes != S.res || oldFm != S.fmDepth) applyLive (now);
}

void TableEngine::setState (const Value& state, double now) { if (bank) applySettings (parseSettings (state, *bank), now); }

bool TableEngine::follows (const std::string& lay, bool aOn) const
{
    if (S.follow == "A+B") return true;
    if (S.follow == "A" || S.follow == "B") return lay == S.follow;
    return lay == (aOn ? "A" : "B");
}

void TableEngine::applyLive (double now)
{
    bus.setTargetAt (S.level, now, .02);
    for (auto& v : voices) { v->lpF.setTargetAt (S.cutoff, now, .02); v->lpQ.setTargetAt (S.res, now, .02); v->fmGain.setTargetAt (v->f * S.fmDepth * .5, now, .02); }
}

double TableEngine::envAt (const Voice& v, double t) const
{
    const double tt = t - v.t0; if (tt <= 0) return 0; if (tt < v.a) return v.peak * tt / v.a;
    const double sus = v.peak * v.s; return sus + (v.peak - sus) * std::exp (-(tt - v.a) / std::max (.005, v.d / 3));
}

void TableEngine::doRelease (Voice& v, double time, bool quick, double now)
{
    if (v.rel) return; v.rel = true;
    const double t = std::max (time > 0 ? time : now, now), r = quick ? .03 : v.r;
    v.g.cancelFrom (t); v.g.setValueAt (envAt (v, t), t); v.g.setTargetAt (0, t, std::max (.004, r / 3));
    v.stopAt = t + r * 1.6 + .08;
    held.erase (std::remove (held.begin(), held.end(), &v), held.end());
}

void TableEngine::noteOn (uint32_t id, int note, double vel, const std::string& layer, double time, bool aOn, double now)
{
    if (! S.on || ! follows (layer, aOn) || ! waves) return;
    for (auto& v : voices) if (v->id == id && ! v->rel) doRelease (*v, 0, true, now);
    auto active = [&] { int c = 0; for (auto& v : voices) if (! v->rel) ++c; return c; };
    while (active() >= S.poly) { Voice* o = nullptr; for (auto& v : voices) if (! v->rel && (! o || v->t0 < o->t0)) o = v.get(); if (! o) break; doRelease (*o, 0, true, now); }
    auto vp = std::make_unique<Voice>(); Voice& v = *vp;
    const double f = 440 * std::pow (2.0, (note + S.octave * 12 - 69) / 12.0) / waves->pitchDiv, t = std::max (time > 0 ? time : now, now);
    const double allowed = std::floor (sr * .45 / (f * std::pow (2.0, (S.detune + std::abs (bendCents)) / 1200)));
    int mi = 0; while (mi < 9 && MIPS[mi] > allowed) ++mi;
    v.id = id; v.note = note; v.f = f; v.t0 = t; v.a = std::max (.002, S.attack); v.d = S.decay; v.s = S.sustain; v.r = S.release; v.peak = std::pow (std::max (.01, vel / 127), .8);
    v.mot = S.motion.on; v.mi = mi; v.U = (int) std::lround (S.unison); v.startFrame = (long long) std::ceil (t * sr - 1e-9);
    v.lp.type = wa::BiquadType::lowpass; v.lp.sr = sr;
    v.lpF.init (sr, S.cutoff); v.lpQ.init (sr, S.res); v.mg.init (sr, 1); v.pan.init (sr, 0); v.fmGain.init (sr, f * S.fmDepth * .5);
    v.g.init (sr, 1); v.g.setValueAt (0, t); v.g.linearRampTo (v.peak, t + v.a); v.g.setTargetAt (v.peak * v.s, t + v.a, std::max (.005, v.d / 3));
    for (int u = 0; u < v.U; ++u)
    {
        const double cents = v.U > 1 ? (-S.detune + 2 * S.detune * u / (v.U - 1)) : 0;
        v.cents.push_back (cents); v.phase.emplace_back(); v.detune.emplace_back(); v.detune.back().init (sr, cents + bendCents);
        for (int side = 0; side < 2; ++side)
        {
            Pair p; p.right = side == 1; p.wave[0] = p.wave[1] = side ? waves->R[(size_t) mi] : waves->L[(size_t) mi];
            p.gain[0].init (sr, 1); p.gain[1].init (sr, 0); v.pairs.push_back (std::move (p));
        }
    }
    if (v.mot || S.selfFm > 0) { v.selfFm = true; v.foFreq = f * S.fmRatio; v.foDetune.init (sr, 0); v.fg.init (sr, f * S.fmRatio * 8 * S.selfFm); }
    voices.push_back (std::move (vp));
}

void TableEngine::noteOff (uint32_t id, double time, double now)
{
    for (auto& v : voices) if (v->id == id && ! v->rel) { if (sustainOn) { held.push_back (v.get()); return; } doRelease (*v, time, false, now); return; }
}

void TableEngine::sustain (bool on, double now)
{
    sustainOn = on; if (! on) { auto h = held; for (auto* v : h) doRelease (*v, 0, false, now); held.clear(); }
}

void TableEngine::bend (double value, double range, double now)
{
    bendCents = value * (range > 0 ? range : 2) * 100;
    for (auto& v : voices)
    {
        for (int u = 0; u < v->U; ++u) v->detune[(size_t) u].setTargetAt (v->cents[(size_t) u] + bendCents, now, .01);
        if (v->selfFm) v->foDetune.setTargetAt (bendCents, now, .01);
    }
}

void TableEngine::panic (double now) { for (auto& v : voices) doRelease (*v, 0, true, now); held.clear(); sustainOn = false; }

void TableEngine::setWaves (std::shared_ptr<TableWaves> w, double now)
{
    const bool first = ! waves; waves = std::move (w); if (first || ! waves) return;
    const double t = now;
    for (auto& v : voices)
    {
        if (v->rel || (v->mot && v->frame >= 0)) continue;
        const double allowed = std::floor (sr * .45 / (v->f * std::pow (2.0, (S.detune + std::abs (bendCents)) / 1200)));
        int mi = 0; while (mi < 9 && MIPS[mi] > allowed) ++mi;
        const int next = 1 - v->pairSide;
        for (auto& p : v->pairs)
        {
            p.wave[(size_t) next] = p.right ? waves->R[(size_t) mi] : waves->L[(size_t) mi];
            for (int z = 0; z < 2; ++z) { auto& g = p.gain[(size_t) z]; g.cancelFrom (t); g.setValueAt (z == v->pairSide ? 1 : 0, t); g.linearRampTo (z == next ? 1 : 0, t + .025); }
        }
        v->pairSide = next;
    }
}

int TableEngine::activeVoices() const { int c = 0; for (auto& v : voices) if (! v->rel) ++c; return c; }

// ---------------- motion ----------------
std::array<double, 8> TableEngine::laneWeights() const
{
    std::array<double, 8> w {}; const double s = S.motion.scene, sh = .03;
    for (int i = 0; i < 8; ++i)
    {
        if (S.motion.all) { w[(size_t) i] = 1; continue; }
        const double lo = WINDOWS[i][0], hi = WINDOWS[i][1];
        w[(size_t) i] = s >= lo && s <= hi ? 1 : s < lo && s > lo - sh ? (s - (lo - sh)) / sh : s > hi && s < hi + sh ? (hi + sh - s) / sh : 0;
    }
    return w;
}

TableEngine::StepMods TableEngine::stepMods (long long n) const
{
    StepMods o; const auto w = laneWeights(); const int k = (int) (((n % 16) + 16) % 16);
    for (int i = 0; i < 8; ++i)
    {
        const double wi = w[(size_t) i]; if (wi <= 0) continue; const MotionLane& L = S.motion.lanes[(size_t) i];
        const bool rnd = L.rnd[(size_t) k]; const double s = L.steps[(size_t) k], r = rnd ? hash01 (n, i) * 2 - 1 : 0, v = rnd ? std::abs (r) : (L.semis ? s / 12 : s / 10);
        for (auto& t : L.targets)
        {
            const double d = t.depth * wi;
            if (t.dest == "scan") o.scan += d * v;
            else if (t.dest == "cutoff") o.cutoff += d * v;
            else if (t.dest == "res") o.res += d * v;
            else if (t.dest == "level") o.gate *= 1 - d * (1 - (rnd ? std::abs (r) : v));
            else if (t.dest == "pitch")
            {
                if (rnd) o.pitch += JUMPS[(int) std::floor (hash01 (n, i + 16) * 7)] * (d / 24);
                else if (L.semis)
                {
                    const double st = s * (t.depth / 12); double q = st;
                    if (L.name.find ("Hijaz") != std::string::npos) { double a = 0; for (int b : HIJAZ) if (std::abs (b - st) < std::abs (a - st)) a = b; q = a; }
                    o.pitch += wi * q;
                }
                else o.pitch += d * v;
            }
            else if (t.dest == "fm") o.fm += d * v;
            else if (t.dest == "pan") o.pan += d * (t.alt ? (k % 2 ? 1 : -1) : rnd ? r : 2 * v - 1);
        }
    }
    return o;
}

double TableEngine::stepDur() const { return 60 / motionBpm * rateBeats (S.motion.rate); }
double TableEngine::stepTime (long long n) const
{
    const double dur = stepDur(), sw = S.motion.swing; const long long pair = (long long) std::floor (n / 2.0);
    return motionAnchor + pair * 2 * dur + ((n % 2 + 2) % 2 ? dur * (1 + sw) : 0);
}
long long TableEngine::firstStep (double t) const
{
    long long n = (long long) std::floor ((t - motionAnchor) / stepDur()) - 1; while (stepTime (n) < t) ++n; return n;
}
bool TableEngine::usesScan() const
{
    const auto w = laneWeights();
    for (int i = 0; i < 8; ++i) if (w[(size_t) i] > 0) for (auto& t : S.motion.lanes[(size_t) i].targets) if (t.dest == "scan" && t.depth > 0) return true;
    return false;
}

std::string TableEngine::motionKeyFor (const TableSettings& s)
{
    char b[200]; std::snprintf (b, sizeof b, "%s|%.17g|%.17g|%.17g|%.17g", s.table.c_str(), s.M1, s.M2, s.M3, s.M4); return b;
}

std::shared_ptr<TableWaves> TableEngine::buildMotionFrame (const wt::Bank& bk, const TableSettings& s, int idx, double sampleRate)
{
    // one full-band wave per scan step (the browser band-limits it per pitch)
    auto w = std::make_shared<TableWaves>(); const Value* def = bk.find (s.table); if (! def) return nullptr;
    wt::Macros mv; mv.given = true; mv.M1 = s.M1; mv.M2 = s.M2; mv.M3 = s.M3; mv.M4 = s.M4;
    const auto st = wt::renderStereo (*def, idx / 16.0, bk, mv); std::vector<float> re, im;
    wt::waveCoefficients (st.L, 512, re, im); w->L[0] = waveFrom (re, im, 512, sampleRate);
    if (! st.same) { wt::waveCoefficients (st.R, 512, re, im); w->R[0] = waveFrom (re, im, 512, sampleRate); w->stereo = true; } else w->R[0] = w->L[0];
    return w;
}

std::shared_ptr<TableWaves> TableEngine::frameWave (int idx)
{
    const std::string key = motionKeyFor (S);
    if (key != motionKey) { motionFrames.clear(); motionKey = key; }
    auto it = motionFrames.find (idx); if (it != motionFrames.end()) return it->second;
    auto w = bank ? buildMotionFrame (*bank, S, idx, sr) : nullptr;   // the plugin builds these ahead, off the audio thread
    if (w) motionFrames[idx] = w; return w;
}

void TableEngine::motionTick (double now, double horizon)
{
    if (! S.motion.on) return;
    const bool scanOn = usesScan(); const double dur = stepDur();
    for (auto& vp : voices)
    {
        Voice& v = *vp; if (v.rel || ! v.mot) continue;
        if (v.mNext < 0 || std::abs (stepTime (v.mStep) - v.mNext) > .002) { v.mStep = firstStep (std::max (now, v.t0)) - 1; v.mNext = stepTime (v.mStep); }
        int guard = 0;
        while (v.mNext < horizon && guard++ < 16)
        {
            const double t = std::max (v.mNext, now); const StepMods o = stepMods (v.mStep); const double tc = .0015 + S.motion.slew * dur * .45;
            if (scanOn)
            {
                const int idx = (int) std::lround (clampd (S.scan + o.scan, 0, 1) * 16);
                if (idx != v.frame)
                {
                    if (now < v.xEnd) break;
                    auto fw = frameWave (idx); if (! fw) break; const double fade = std::max (.004, std::min (dur * .9, S.motion.slew * dur)); const int side = 1 - v.pairSide;
                    for (auto& p : v.pairs)
                    {
                        p.wave[(size_t) side] = p.right && fw->stereo ? fw->R[0] : fw->L[0];
                        for (int z = 0; z < 2; ++z) { auto& g = p.gain[(size_t) z]; g.cancelFrom (t); g.setValueAt (z == side ? 0 : 1, t); g.linearRampTo (z == side ? 1 : 0, t + fade); }
                    }
                    v.pairSide = side; v.frame = idx; v.xEnd = t + fade + .002;
                }
            }
            v.lpF.setTargetAt (std::min (18000.0, S.cutoff * std::pow (2.0, 7 * o.cutoff)), t, tc);
            v.lpQ.setTargetAt (std::min (30.0, S.res + 14 * o.res), t, tc);
            v.mg.setTargetAt (std::max (0.0, o.gate), t, std::max (.002, tc));
            v.pan.setTargetAt (clampd (o.pan, -1, 1), t, tc);
            for (int u = 0; u < v.U; ++u) v.detune[(size_t) u].setTargetAt (v.cents[(size_t) u] + bendCents + 100 * o.pitch, t, tc);
            if (v.selfFm) { v.foDetune.setTargetAt (bendCents + 100 * o.pitch, t, tc); v.fg.setTargetAt (v.f * S.fmRatio * 8 * std::min (1.0, S.selfFm + o.fm), t, tc); }
            ++v.mStep; v.mNext = stepTime (v.mStep);
        }
    }
}

// ---------------- audio ----------------
void TableEngine::process (float* L, float* R, const float* fmIn, int n, long long frame)
{
    nowFrame = frame; const double now = frame / sr;
    motionTick (now, (frame + n) / sr);
    const float nyq = (float) (sr / 2);
    float busL[128] = {}, busR[128] = {};
    for (auto& vp : voices)
    {
        Voice& v = *vp; const float amp = (float) (1 / std::sqrt ((double) v.U)); float fmBuf[128], detBuf[8][128];
        for (int i = 0; i < n; ++i)
        {
            const long long fr = frame + i;
            const double gEnv = v.g.next (fr), lpf = v.lpF.next (fr), lpq = v.lpQ.next (fr), mgv = v.mg.next (fr), panv = v.pan.next (fr), fmg = v.fmGain.next (fr);
            for (int u = 0; u < v.U; ++u) detBuf[u][i] = (float) v.detune[(size_t) u].next (fr);
            float gains[8][2][2]; for (int u = 0; u < v.U; ++u) for (int side = 0; side < 2; ++side) for (int z = 0; z < 2; ++z) gains[u][side][z] = (float) v.pairs[(size_t) u * 2 + (size_t) side].gain[(size_t) z].next (fr);
            const float foDet = v.selfFm ? (float) v.foDetune.next (fr) : 0.f, fgv = v.selfFm ? (float) v.fg.next (fr) : 0.f;
            const bool playing = fr >= v.startFrame && (double) fr < std::ceil (v.stopAt * sr);
            float foOut = 0;
            if (v.selfFm && playing)
            {
                const float fds = std::exp2 (foDet / 1200.0f), finc = wa::OscPhase::increment (*sine, (float) v.foFreq, fds, nyq);
                foOut = v.foPhase.tick (*sine, finc); v.foPhase.advance (*sine, finc);
            }
            fmBuf[i] = (float) (fmg * fmIn[i]) + fgv * foOut;   // the oscillators' frequency input at this frame
            if (! playing) continue;
            // Chrome quirk: in an oscillator's first, partial quantum its frequency and detune values are read from the start of the quantum
            const long long off = v.startFrame - frame; const int fi = (off > 0 && off < n) ? i - (int) off : i;
            const float fmHz = fmBuf[fi];
            float xl = 0, xr = 0;
            for (int u = 0; u < v.U; ++u)
            {
                const float ds = std::exp2 (detBuf[u][fi] / 1200.0f);
                auto& ph = v.phase[(size_t) u]; const wa::PeriodicWave& w0 = *v.pairs[(size_t) u * 2].wave[0];
                const float inc = wa::OscPhase::increment (w0, (float) v.f + fmHz, ds, nyq);
                for (int side = 0; side < 2; ++side)
                {
                    Pair& p = v.pairs[(size_t) u * 2 + (size_t) side]; float s = 0;
                    for (int z = 0; z < 2; ++z) { const float gz = gains[u][side][z]; if (gz != 0) s += gz * ph.tick (*p.wave[(size_t) z], inc); }
                    (side ? xr : xl) += s * amp;
                }
                ph.advance (w0, inc);
            }
            v.lp.coeffs (lpf, lpq);
            float yl = v.lp.tick (0, xl), yr = v.lp.tick (1, xr);
            if (v.mot) { yl *= (float) mgv; yr *= (float) mgv; }
            yl *= (float) gEnv; yr *= (float) gEnv;
            if (v.mot) wa::stereoPan (yl, yr, panv);
            busL[i] += yl; busR[i] += yr;
        }
    }
    for (int i = 0; i < n; ++i) { const float b = (float) bus.next (frame + i); L[i] += busL[i] * b; R[i] += busR[i] * b; }
    const double end = (frame + n) / sr;
    voices.erase (std::remove_if (voices.begin(), voices.end(), [end] (const std::unique_ptr<Voice>& v) { return v->rel && end >= v->stopAt; }), voices.end());
}
} // namespace mosaic
