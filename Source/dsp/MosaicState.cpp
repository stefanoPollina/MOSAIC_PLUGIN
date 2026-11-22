#include "MosaicState.h"
#include <map>

namespace mosaic
{
namespace
{
double num (const json::Value& v) { return v.type == json::Value::Number ? v.num : v.type == json::Value::Bool ? (v.b ? 1.0 : 0.0) : NaN; }
LfoWave lfoWave (const json::Value& v)
{
    if (v.type != json::Value::String || v.str.empty()) return LfoWave::missing;
    const auto& s = v.str;
    return s == "sine" ? LfoWave::sine : s == "square" ? LfoWave::square : s == "saw" ? LfoWave::saw : s == "sample" ? LfoWave::sample : s == "triangle" ? LfoWave::triangle : LfoWave::other;
}
OscWave oscWave (const json::Value& v)
{
    if (v.type != json::Value::String || v.str.empty()) return OscWave::missing;
    const auto& s = v.str;
    return s == "sine" ? OscWave::sine : s == "saw" ? OscWave::saw : s == "square" ? OscWave::square : s == "noise" ? OscWave::noise : s == "pink" ? OscWave::pink : OscWave::other;
}
double beatsOf (const json::Value& v)
{
    static const std::map<std::string, double> B { { "4/1", 16 }, { "2/1", 8 }, { "1/1", 4 }, { "1/2", 2 }, { "1/4", 1 }, { "1/8", 0.5 }, { "1/8d", 0.75 }, { "1/8t", 1.0 / 3 }, { "1/16", 0.25 }, { "1/16t", 1.0 / 6 }, { "1/32", 0.125 } };
    if (v.type != json::Value::String) return 0.25;
    auto it = B.find (v.str); return it == B.end() ? 0.25 : it->second;
}
int destOf (const json::Value& v)
{
    if (v.type != json::Value::String || v.str.empty()) return D_none;
    const auto& s = v.str;
    if (const int x = xDest (s)) return x;
    if (s == "pitch") return D_pitch; if (s == "cutoff") return D_cutoff; if (s == "res") return D_res; if (s == "pwm") return D_pwm;
    if (s == "fm") return D_fm; if (s == "pan") return D_pan; if (s == "osc1") return D_osc1; if (s == "osc2") return D_osc2; if (s == "osc3") return D_osc3;
    if (s == "gate") return D_gate; if (s == "drip") return D_drip; if (s == "off") return D_off;
    return D_none;
}
void env (Env& e, const json::Value& v)
{
    e.a = num (v["a"]); e.d = num (v["d"]); e.s = num (v["s"]); e.r = num (v["r"]); e.delay = num (v["delay"]); e.hold = num (v["hold"]);
    e.curve = num (v["curve"]); e.fade = num (v["fade"]); e.velocity = num (v["velocity"]); e.depth = num (v["depth"]);
}
void routes (std::vector<Route>& out, const json::Value& v)
{
    for (size_t i = 0; i < v.size(); ++i)
    {
        const auto& r = v[i]; Route ro;
        ro.a = num (r["a"]); ro.d = destOf (r["d"]);
        const auto& d = r["d"];
        ro.valid = r.isObject() && r["a"].truthy() && d.type == json::Value::String && ! d.str.empty() && d.str != "off";
        const auto& t = r["t"]; ro.t = t.type == json::Value::String ? (t.str == "A" ? 1 : t.str == "B" ? 2 : t.str == "both" ? 3 : 0) : 0;
        out.push_back (ro);
    }
}
void lfo (Lfo& l, const json::Value& v)
{
    l.present = v.isObject(); if (! l.present) return;
    l.wave = lfoWave (v["wave"]); l.rate = num (v["rate"]); l.morph = num (v["morph"]); l.fold = num (v["fold"]); l.retrig = v["retrig"].truthy();
    l.hasOsc = v["osc"].isArray(); routes (l.osc, v["osc"]);
    l.hasRoutes = v["routes"].isArray(); routes (l.routes, v["routes"]);
    const auto& q = v["seq"]; l.hasSeq = q.truthy();
    if (l.hasSeq)
    {
        l.seq.on = q["on"].truthy(); l.seq.length = num (q["length"]); l.seq.slew = num (q["slew"]); l.seq.beats = beatsOf (q["div"]); l.seq.bi = q["bi"].truthy();
        const auto& st = q["steps"];
        for (size_t i = 0; i < st.size(); ++i)
        {
            const auto& s = st[i]; SeqStep k;
            k.t = s["t"].truthy(); k.p = num (s["p"]); k.s = num (s["s"]); k.v = num (s["v"]); k.r = num (s["r"]); k.mo = num (s["mo"]);
            const auto& ly = s["l"]; k.l = ly.type == json::Value::String && ! ly.str.empty() ? (ly.str == "AB" ? StepLayer::AB : ly.str == "A" ? StepLayer::A : StepLayer::B) : StepLayer::any;
            const auto& m = s["m"]; const std::string ms = m.type == json::Value::String && ! m.str.empty() ? m.str : "wave";
            k.m = ms == "mul" ? StepMode::mul : ms == "step" ? StepMode::step : ms == "rand" ? StepMode::rand : ms == "off" ? StepMode::off : StepMode::wave;
            k.w = lfoWave (s["w"]);
            l.seq.steps.push_back (k);
        }
    }
}
void layer (Layer& L, const json::Value& v)
{
    L.present = v.isObject(); if (! L.present) return;
    L.disabled = v["enabled"].type == json::Value::Bool && ! v["enabled"].b;
    env (L.amp, v["amp"]); env (L.filt, v["filt"]);
    const auto& f = v["filter"]; auto& F = L.filter;
    {
        const auto& t = f["type"];
        if (t.type != json::Value::String || t.str.empty()) F.type = FiltType::missing;
        else { const auto& s = t.str; F.type = s == "lowpass" ? FiltType::lowpass : s == "highpass" ? FiltType::highpass : s == "bandpass" ? FiltType::bandpass : s == "notch" ? FiltType::notch : s == "comb" ? FiltType::comb : s == "formant" ? FiltType::formant : s == "drip" ? FiltType::drip : FiltType::other; }
    }
    F.cutoff = num (f["cutoff"]); F.res = num (f["res"]); F.env = num (f["env"]); F.lfo = num (f["lfo"]); F.key = num (f["key"]); F.drive = num (f["drive"]);
    F.slope = num (f["slope"]); F.comb = num (f["comb"]); F.vowel = num (f["vowel"]);
    F.ping = num (f["ping"]); F.sweep = num (f["sweep"]); F.fall = num (f["fall"]); F.bubble = num (f["bubble"]); F.fm = num (f["fm"]);
    F.chirp = num (f["chirp"]); F.dwidth = num (f["dwidth"]); F.density = num (f["density"]); F.dsize = num (f["dsize"]); F.dguard = num (f["dguard"]);
    {
        const auto& m = f["dripMode"]; const std::string s = m.type == json::Value::String ? m.str : "";
        F.dripStyleSplash = s == "splash"; F.dripStyleStream = s == "stream"; F.dripStorm = s == "storm"; F.drip3d = s == "3d";
        F.dripMode = (s == "even") ? DripMode::even : (s == "rain" || F.dripStyleSplash || F.dripStyleStream) ? DripMode::rain : DripMode::both;
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto& o = v["osc"][(size_t) i]; auto& O = L.osc[(size_t) i];
        O.wave = oscWave (o["wave"]); O.octave = num (o["octave"]); O.semi = num (o["semi"]); O.fine = num (o["fine"]); O.level = num (o["level"]);
        O.pwm = num (o["pwm"]); O.fold = num (o["fold"]); O.ring = num (o["ring"]); O.sync = num (o["sync"]); O.xmod = num (o["xmod"]); O.sub = num (o["sub"]);
    }
    L.character = num (v["character"]); L.fmRatio = num (v["fmRatio"]); L.fmIndex = num (v["fmIndex"]); L.pdDepth = num (v["pdDepth"]); L.level = num (v["level"]);
    { const std::string m = v["fmMode"].strOr ("off"); L.fmMode = m == "fm" ? FmMode::fm : m == "pd" ? FmMode::pd : m == "both" ? FmMode::both : FmMode::off; }
    { const std::string c = v["pdCurve"].strOr ("bend"); L.pdCurve = c == "sine" ? PdCurve::sine : c == "pulse" ? PdCurve::pulse : PdCurve::bend; }
}
} // namespace

int xDest (const std::string& n)
{
    static const std::map<std::string, int> X { { "fold1", 1 }, { "fold2", 2 }, { "fold3", 3 }, { "xmod1", 4 }, { "sync2", 5 }, { "sync3", 6 }, { "ring2", 7 }, { "ring3", 8 },
        { "sub1", 9 }, { "sub2", 10 }, { "sub3", 11 }, { "pd", 12 }, { "drive", 13 }, { "vowel", 14 }, { "pitch1", 15 }, { "pitch2", 16 }, { "pitch3", 17 }, { "pwm1", 18 },
        { "pwm2", 19 }, { "pwm3", 20 }, { "ping", 21 }, { "sweep", 22 }, { "fall", 23 }, { "bubble", 24 }, { "dwob", 25 }, { "comb", 26 }, { "xmod2", 27 }, { "xmod3", 28 },
        { "ring1", 29 }, { "sync1", 30 }, { "fmratio", 31 }, { "slope", 32 }, { "fenv", 33 }, { "fkey", 34 }, { "chirp", 35 }, { "dwidth", 36 }, { "density", 37 },
        { "dsize", 38 }, { "dguard", 39 } };
    auto it = X.find (n); return it == X.end() ? 0 : it->second;
}

State stateFromJson (const json::Value& v)
{
    State S; S.present = v.isObject(); if (! S.present) return S;
    S.bendRange = num (v["bendRange"]); S.detune = num (v["detune"]); S.unison = num (v["unison"]); S.bpm = num (v["bpm"]); S.duck = num (v["duck"]); S.duckRel = num (v["duckRel"]); S.duckShape = num (v["duckShape"]);
    S.hasLfo = v["lfo"].isArray(); for (size_t i = 0; i < 8; ++i) lfo (S.lfo[i], v["lfo"][i]);
    S.hasLfoB = v["lfoB"].truthy() && v["lfoB"].isArray(); if (S.hasLfoB) for (size_t i = 0; i < 8; ++i) lfo (S.lfoB[i], v["lfoB"][i]);
    S.hasMod = v["mod"].isArray();
    for (size_t i = 0; i < v["mod"].size(); ++i)
    {
        const auto& m = v["mod"][i]; ModSlot s; s.present = m.isObject();
        s.amount = num (m["amount"]); s.dest = destOf (m["dest"]);
        const auto& src = m["src"];
        if (src.type == json::Value::String)
        {
            const auto& n = src.str;
            s.src.code = n == "lfo1" ? 1 : n == "lfo2" ? 2 : n == "amp" ? 3 : n == "filter" ? 4 : n == "vel" ? 5 : n == "wheel" ? 6 : n == "key" ? 7 : n == "drip" ? 8 : n == "rain" ? 9 : 0;
            s.src.lfoN = (n.size() == 4 && n[0] == 'l') ? (int) (unsigned char) n[3] - 49 : -1000;
        }
        else s.src.lfoN = -1000;
        const auto& t = m["target"]; s.target = t.type == json::Value::String ? (t.str == "A" ? 1 : t.str == "B" ? 2 : t.str == "both" ? 3 : 0) : 0;
        S.mod.push_back (s);
    }
    const auto& L = v["layers"]; S.hasLayers = L.truthy();
    if (S.hasLayers) { layer (S.A, L["A"]); layer (S.B, L["B"]); }
    const auto& U = v["lfoSeq"];
    S.lfoSeq.present = U.isObject();
    if (S.lfoSeq.present)
    {
        S.lfoSeq.on = U["on"].truthy(); S.lfoSeq.retrig = U["retrig"].truthy(); S.lfoSeq.hasGrid = U["grid"].truthy();
        for (size_t i = 0; i < U["grid"].size(); ++i) S.lfoSeq.grid.push_back ((int) (long long) U["grid"][i].numOr (0));
        S.lfoSeq.beats = beatsOf (U["div"]); S.lfoSeq.steps = num (U["steps"]); S.lfoSeq.xfade = num (U["xfade"]);
    }
    return S;
}
} // namespace mosaic
