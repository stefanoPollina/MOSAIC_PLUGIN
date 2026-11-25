/* Mosaic's C++ engines (voices, TABLE, the FX chain) for the browser, as WebAssembly.
   The page's AudioWorklet calls these exports exactly where the plugin's processor (PluginProcessor.cpp) would:
   the page sends the same messages (state, on, off, bend, mod, sustain, panic, gate, clock) and the worklet renders
   128-frame quanta. Single-threaded: a state is parsed and installed when it arrives, between quanta. */
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include "../dsp/MiniJson.h"
#include "../dsp/MosaicState.h"
#include "../dsp/MosaicVoice.h"
#include "../dsp/MosaicFx.h"
#include "../dsp/TableEngine.h"
#include "../dsp/WtCore.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT extern "C"
#endif

using mosaic::json::Value;

namespace
{
double sr = 48000;
mosaic::VoiceEngine voice;
mosaic::TableEngine table;
mosaic::MosaicFx fx;
std::shared_ptr<mosaic::wt::Bank> bank;
std::shared_ptr<const mosaic::State> liveVoice;
std::string lastWaveKey, lastMotionKey; std::shared_ptr<mosaic::TableWaves> lastWaves;
bool aOn = true, bOn = true; int polyphony = 8; double glideTime = 0, bendRange = 2;
bool prepared = false;
long long frame = 0;
float outL[128], outR[128], scope[160];

std::string waveKeyFor (const mosaic::TableSettings& S)
{
    char b[256]; std::snprintf (b, sizeof b, "%s|%.17g|%.17g|%.17g|%.17g|%.17g", S.table.c_str(), S.scan, S.M1, S.M2, S.M3, S.M4); return b;
}
double nowSec() { return (double) frame / sr; }
}

EXPORT void* mw_alloc (int n) { return std::malloc ((size_t) n); }
EXPORT void mw_free (void* p) { std::free (p); }
EXPORT float* mw_out (int ch) { return ch ? outR : outL; }
EXPORT float* mw_send (int ch) { return mosaic::wasmrev::send[ch ? 1 : 0]; }
EXPORT float* mw_ret (int ch) { return mosaic::wasmrev::ret[ch ? 1 : 0]; }
EXPORT float* mw_scope() { voice.scopeCopy (scope); return scope; }
EXPORT int mw_voices() { return voice.activeVoices() + table.activeVoices(); }

// TABLE's bank (the page's wt-bank-data JSON)
EXPORT void mw_bank (const char* json, int len)
{
    bank = std::make_shared<mosaic::wt::Bank>();
    bank->load (std::string (json, (size_t) len));
    table.setBank (bank);
}

EXPORT void mw_init (double sampleRate, uint32_t seed)
{
    sr = sampleRate; frame = 0;
    voice.seed (seed);
    fx.prepare (sr, seed * 2654435761u + 1);
    fx.externalImpulses = true;   // the page's ConvolverNode holds the impulse (see juce_shim.h)
    table.prepare (sr);
    mosaic::FxState d; d.reverbWet = 0.12; d.reverbDecay = 2.1; fx.apply (d, 0);
    prepared = true;
}

// the page's state message (as the plugin's installState): { slim, fx, master, bpm, name, wtLab, aOn, bOn, polyphony, glide }
EXPORT void mw_state (const char* json, int len)
{
    if (! prepared) return;
    const std::string text (json, (size_t) len);
    const Value m = mosaic::json::Parser (text).parse();
    const double now = nowSec();
    const Value& slim = m["slim"];
    liveVoice = std::make_shared<mosaic::State> (mosaic::stateFromJson (slim));
    voice.setState (liveVoice);
    bendRange = slim["bendRange"].orNum (2);
    {
        // FxState::fromJson reads state.fx, state.master and state.seq.bpm
        Value full; full.type = Value::Object; full.obj["fx"] = m["fx"]; full.obj["master"] = m["master"];
        Value seq; seq.type = Value::Object; seq.obj["bpm"] = m["bpm"]; full.obj["seq"] = seq;
        fx.apply (mosaic::FxState::fromJson (full), now);
    }
    if (bank)
    {
        Value ts; ts.type = Value::Object; ts.obj["name"] = m["name"]; ts.obj["wtLab"] = m["wtLab"];
        const auto S = mosaic::TableEngine::parseSettings (ts, *bank);
        const bool wasReplacing = table.replaces();
        table.applySettings (S, now);
        if (! wasReplacing && table.replaces()) voice.panic();   // TABLE ONLY: Mosaic's own voices fall silent
        const auto key = waveKeyFor (S);
        if (key != lastWaveKey || ! lastWaves) { lastWaves = mosaic::buildTableWaves (*bank, S, sr); lastWaveKey = key; table.setWaves (lastWaves, now); }
        if (S.motion.on)
        {
            const auto mk = mosaic::TableEngine::motionKeyFor (S);
            if (mk != lastMotionKey)
            {
                std::map<int, std::shared_ptr<mosaic::TableWaves>> frames;
                for (int i = 0; i <= 16; ++i) frames[i] = mosaic::TableEngine::buildMotionFrame (*bank, S, i, sr);
                lastMotionKey = mk; table.setMotionFrames (std::move (frames), mk);
            }
        }
    }
    aOn = m["aOn"].type == Value::Bool ? m["aOn"].b : true; bOn = m["bOn"].type == Value::Bool ? m["bOn"].b : true;
    polyphony = std::max (1, (int) m["polyphony"].orNum (8)); glideTime = m["glide"].numOr (0);
}

EXPORT void mw_on (const char* id, int idLen, int note, double vel, double glide, int legato, int layer, double time, int cancel)
{
    using mosaic::Event; using mosaic::EvType;
    const uint64_t h = mosaic::VoiceEngine::hashId (id, (size_t) idLen);
    if (! table.replaces()) { Event e; e.type = EvType::on; e.id = h; e.note = note; e.vel = vel; e.glide = glide; e.legato = legato != 0; e.layer = layer; e.time = time; voice.post (e); }
    if (! cancel) table.noteOn ((uint32_t) h, note, vel, layer ? "B" : "A", time, aOn, nowSec());
}
EXPORT void mw_off (const char* id, int idLen, double time)
{
    using mosaic::Event; using mosaic::EvType;
    const uint64_t h = mosaic::VoiceEngine::hashId (id, (size_t) idLen);
    Event e; e.type = EvType::off; e.id = h; e.time = time; voice.post (e); table.noteOff ((uint32_t) h, time, nowSec());
}
EXPORT void mw_bend (double v, double t) { mosaic::Event e; e.type = mosaic::EvType::bend; e.value = v; e.time = t; voice.post (e); table.bend (v, bendRange, nowSec()); }
EXPORT void mw_mod (double v, double t) { mosaic::Event e; e.type = mosaic::EvType::mod; e.value = v; e.time = t; voice.post (e); }
EXPORT void mw_sustain (int on, double t) { mosaic::Event e; e.type = mosaic::EvType::sustain; e.on = on != 0; e.time = t; voice.post (e); table.sustain (on != 0, nowSec()); }
EXPORT void mw_panic() { voice.panic(); table.panic (nowSec()); }
EXPORT void mw_gate (double v, double t, int cancel) { fx.setGate (v, t, cancel != 0); }
EXPORT void mw_clock (double anchor) { voice.setClockAnchor (anchor); }

// one 128-frame quantum at the page's audio frame `at` (the worklet's currentFrame)
EXPORT void mw_render (double at)
{
    frame = (long long) at;
    const double now = nowSec();
    float fm[128];
    voice.process (outL, outR, 128, now, sr);
    for (int i = 0; i < 128; ++i) fm[i] = 0.5f * (outL[i] + outR[i]);
    table.process (outL, outR, fm, 128, frame);
    mosaic::wasmrev::sentAny = false;
    fx.process (outL, outR, 128, now);
    if (! mosaic::wasmrev::sentAny) { std::memset (mosaic::wasmrev::send, 0, sizeof mosaic::wasmrev::send); std::memset (mosaic::wasmrev::ret, 0, sizeof mosaic::wasmrev::ret); }
}
