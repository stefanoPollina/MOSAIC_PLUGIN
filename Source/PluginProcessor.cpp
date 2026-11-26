#include "PluginProcessor.h"
#include "BrowserHost.h"
#include "PluginEditor.h"
#include "BinaryData.h"

using mosaic::json::Value;

namespace
{
Value toValue (const juce::var& v) { return mosaic::json::parse (juce::JSON::toString (v, true).toStdString()); }
uint64_t idHash (const juce::String& s) { const auto u = s.toStdString(); return mosaic::VoiceEngine::hashId (u.data(), u.size()); }
std::string waveKeyFor (const mosaic::TableSettings& S)
{
    char b[256]; std::snprintf (b, sizeof b, "%s|%.17g|%.17g|%.17g|%.17g|%.17g", S.table.c_str(), S.scan, S.M1, S.M2, S.M3, S.M4); return b;
}
}

juce::File MosaicProcessor::dataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")   // ~/Library/Application Support/Mosaic
#endif
        .getChildFile ("Mosaic");
}

MosaicProcessor::MosaicProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    wtBank = std::make_shared<mosaic::wt::Bank>();
    wtBank->load (std::string (BinaryData::wtbank_json, (size_t) BinaryData::wtbank_jsonSize));
    table.setBank (wtBank);
    outBuf.assign ((size_t) outFifo.getTotalSize() * 2, 0.f);
    recBuf.assign ((size_t) recFifo.getTotalSize() * 2, 0.f);
    for (int i = 0; i < numSlots; ++i) { auto* p = new SlotParam (i); addParameter (p); slots[(size_t) i] = p; }
}

juce::Array<juce::var> MosaicProcessor::slotValues() const
{
    juce::Array<juce::var> a; for (auto* p : slots) a.add ((double) p->get()); return a;
}

void MosaicProcessor::setSlotNames (const juce::var& names)
{
    bool changed = false;
    for (int i = 0; i < numSlots && i < names.size(); ++i) changed |= slots[(size_t) i]->setLabel (names[i].toString());
    if (changed) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withParameterInfoChanged (true));
}

void MosaicProcessor::slotFromPage (int i, float v)
{
    if (i < 0 || i >= numSlots) return;
    auto* p = slots[(size_t) i]; p->beginChangeGesture(); p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v)); p->endChangeGesture();
}

MosaicProcessor::~MosaicProcessor()
{
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}


bool MosaicProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MosaicProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    voice.seed ((uint32_t) juce::Random::getSystemRandom().nextInt());
    fx.prepare (sr, (uint32_t) juce::Random::getSystemRandom().nextInt());
    fx.externalImpulses = false;
    table.prepare (sr);
    ringL.assign (1 << 16, 0.f); ringR.assign (1 << 16, 0.f);
    renderedFrames = 0; hostFrames = 0; ringRead = 0;
    setLatencySamples (128);
    // the first state: whatever the page sent last, or Mosaic's defaults until it does
    if (auto* p = pending.exchange (nullptr)) { if (live) delete retired.exchange (live.release()); live.reset (p); }
    if (live && wavesRate != sr)
    {
        // TABLE's waves are built for one sample rate: rebuild them for this one
        live->waves = lastWaves = mosaic::buildTableWaves (*wtBank, live->tableSettings, sr); lastWaveKey = waveKeyFor (live->tableSettings);
        lastMotion.clear(); lastMotionKey.clear(); live->motion.clear(); live->motionKey.clear();
        if (live->tableSettings.motion.on)
        {
            for (int i = 0; i <= 16; ++i) lastMotion[i] = mosaic::TableEngine::buildMotionFrame (*wtBank, live->tableSettings, i, sr);
            live->motion = lastMotion; live->motionKey = lastMotionKey = mosaic::TableEngine::motionKeyFor (live->tableSettings);
        }
    }
    wavesRate = sr;
    if (live && live->voice) voice.setState (live->voice);
    if (live && live->hasFx) { fx.apply (live->fx, 0); }
    else { mosaic::FxState d; d.reverbWet = 0.12; d.reverbDecay = 2.1; fx.apply (d, 0); }   // also loads the first impulse, before any audio
    fx.externalImpulses = true;
    if (live) { table.applySettings (live->tableSettings, 0); if (live->waves) table.setWaves (live->waves, 0); if (! live->motionKey.empty()) table.setMotionFrames (live->motion, live->motionKey); aOn = live->aOn; bOn = live->bOn; polyphony = live->polyphony; glideTime = live->glide; }
    lastDecay = mosaic::MosaicFx::decayFor (live && live->hasFx ? live->fx : mosaic::FxState());
}

// ---------------------------------------------------------------- page → engines
void MosaicProcessor::push (const Msg& m)
{
    const auto w = fifo.write (1);
    if (w.blockSize1 > 0) q[(size_t) w.startIndex1] = m; else if (w.blockSize2 > 0) q[(size_t) w.startIndex2] = m;
}

void MosaicProcessor::installState (const juce::var& m)
{
    auto* p = new Pack();
    const Value slim = toValue (m["slim"]);
    p->voice = std::make_shared<mosaic::State> (mosaic::stateFromJson (slim));
    bendRange = slim["bendRange"].orNum (2);
    juce::DynamicObject::Ptr full = new juce::DynamicObject(), seq = new juce::DynamicObject();
    full->setProperty ("fx", m["fx"]); full->setProperty ("master", m["master"]); seq->setProperty ("bpm", m["bpm"]); full->setProperty ("seq", juce::var (seq.get()));
    p->fx = mosaic::FxState::fromJson (toValue (juce::var (full.get()))); p->hasFx = true;
    const double decay = mosaic::MosaicFx::decayFor (p->fx);
    if (std::abs (decay - lastDecay) > 0.25)
    {
        p->impulse = mosaic::MosaicFx::makeImpulse (sr, decay, [this] { return irRandom.nextDouble(); });
        p->impulseSeconds = decay; p->hasImpulse = true; lastDecay = decay;
    }
    juce::DynamicObject::Ptr ts = new juce::DynamicObject(); ts->setProperty ("name", m["name"]); ts->setProperty ("wtLab", m["wtLab"]);
    p->tableSettings = mosaic::TableEngine::parseSettings (toValue (juce::var (ts.get())), *wtBank);
    const auto key = waveKeyFor (p->tableSettings);
    if (key != lastWaveKey || ! lastWaves || wavesRate != sr) { wavesRate = sr; lastWaves = mosaic::buildTableWaves (*wtBank, p->tableSettings, sr); lastWaveKey = key; p->waves = lastWaves; }
    if (p->tableSettings.motion.on)
    {
        const auto mk = mosaic::TableEngine::motionKeyFor (p->tableSettings);
        if (mk != lastMotionKey)
        {
            lastMotion.clear();
            for (int i = 0; i <= 16; ++i) lastMotion[i] = mosaic::TableEngine::buildMotionFrame (*wtBank, p->tableSettings, i, sr);
            lastMotionKey = mk; p->motion = lastMotion; p->motionKey = mk;
        }
    }
    p->aOn = (bool) m["aOn"]; p->bOn = (bool) m["bOn"]; p->polyphony = juce::jmax (1, (int) m["polyphony"]); p->glide = (double) m["glide"];
    if (auto* old = pending.exchange (p))
    {
        // not installed yet: keep what it carried that this one does not repeat
        if (! p->waves && old->waves) p->waves = old->waves;
        if (! p->hasImpulse && old->hasImpulse) { p->impulse = std::move (old->impulse); p->impulseSeconds = old->impulseSeconds; p->hasImpulse = true; }
        if (p->motionKey.empty() && ! old->motionKey.empty()) { p->motion = old->motion; p->motionKey = old->motionKey; }
        delete old;
    }
    delete retired.exchange (nullptr);
}

void MosaicProcessor::applyFullState (const juce::String& json)
{
    // what the page's Engine.setState sends, built here from a saved state
    const auto s = juce::JSON::parse (json); if (! s.isObject()) return;
    juce::DynamicObject::Ptr slim = new juce::DynamicObject(), m = new juce::DynamicObject();
    for (auto* k : { "bendRange", "detune", "unison", "lfo", "mod", "layers", "lfoSeq", "lfoB" }) if (s.hasProperty (k)) slim->setProperty (k, s[k]);
    const auto bpm = s["seq"].isObject() && (double) s["seq"]["bpm"] > 0 ? (double) s["seq"]["bpm"] : 120.0;
    slim->setProperty ("bpm", bpm);
    const auto& L = s["layers"];
    m->setProperty ("type", "state"); m->setProperty ("slim", juce::var (slim.get())); m->setProperty ("fx", s["fx"]); m->setProperty ("master", s["master"]);
    m->setProperty ("bpm", bpm); m->setProperty ("name", s["name"]); m->setProperty ("wtLab", s["wtLab"]);
    m->setProperty ("aOn", ! (L["A"].isObject() && L["A"]["enabled"].isBool() && ! (bool) L["A"]["enabled"]));
    m->setProperty ("bOn", ! (L["B"].isObject() && L["B"]["enabled"].isBool() && ! (bool) L["B"]["enabled"]));
    m->setProperty ("polyphony", s.hasProperty ("polyphony") ? s["polyphony"] : juce::var (8)); m->setProperty ("glide", s.hasProperty ("glide") ? s["glide"] : juce::var (0));
    installState (juce::var (m.get()));
}

void MosaicProcessor::fromPage (const juce::var& list)
{
    if (! list.isArray()) return;
    for (auto& m : *list.getArray())
    {
        const auto type = m["type"].toString();
        if (type == "state") { installState (m); continue; }
        Msg x;
        if (type == "on" || type == "off")
        {
            x.kind = type == "on" ? Msg::On : Msg::Off; const auto id = m["id"].toString();
            x.id = idHash (id); x.tableId = (uint32_t) x.id; x.cancel = id == "boot";
            x.note = (int) m["note"]; x.vel = m["vel"].isVoid() ? 100.0 : (double) m["vel"]; x.glide = (double) m["glide"]; x.legato = (bool) m["legato"];
            x.layer = m["layer"].toString() == "B" ? 1 : 0; x.time = (double) m["time"];
        }
        else if (type == "bend" || type == "mod") { x.kind = type == "bend" ? Msg::Bend : Msg::Mod; x.value = (double) m["value"]; x.time = (double) m["time"]; }
        else if (type == "sustain") { x.kind = Msg::Sustain; x.on = (bool) m["on"]; x.time = (double) m["time"]; }
        else if (type == "panic") x.kind = Msg::Panic;
        else if (type == "gate") { x.kind = Msg::Gate; x.value = (double) m["value"]; x.time = (double) m["time"]; x.cancel = (bool) m["cancel"]; }
        else if (type == "clock") { x.kind = Msg::Clock; x.value = (double) m["anchor"]; }
        else if (type == "arp") { arpActive = (bool) m["on"]; continue; }
        else if (type == "rec")
        {
            const auto& r = m["msg"]; const auto cmd = r["cmd"].toString();
            if (cmd == "record") { x.kind = Msg::RecStart; x.time = (double) r["start"]; x.t2 = (double) r["end"]; }
            else if (cmd == "stop") x.kind = Msg::RecStop; else continue;
        }
        else continue;
        push (x);
    }
}

void MosaicProcessor::handle (const Msg& m, double now)
{
    using mosaic::Event; using mosaic::EvType;
    switch (m.kind)
    {
        case Msg::On:
        {
            if (! table.replaces()) { Event e; e.type = EvType::on; e.id = m.id; e.note = m.note; e.vel = m.vel; e.glide = m.glide; e.legato = m.legato; e.layer = m.layer; e.time = m.time; voice.post (e); }
            if (! m.cancel) table.noteOn (m.tableId, m.note, m.vel, m.layer ? "B" : "A", m.time, aOn, now);
            break;
        }
        case Msg::Off: { Event e; e.type = EvType::off; e.id = m.id; e.time = m.time; voice.post (e); table.noteOff (m.tableId, m.time, now); break; }
        case Msg::Bend: { Event e; e.type = EvType::bend; e.value = m.value; e.time = m.time; voice.post (e); table.bend (m.value, bendRange, now); break; }
        case Msg::Mod: { Event e; e.type = EvType::mod; e.value = m.value; e.time = m.time; voice.post (e); break; }
        case Msg::Sustain: { Event e; e.type = EvType::sustain; e.on = m.on; e.time = m.time; voice.post (e); table.sustain (m.on, now); break; }
        case Msg::Panic: voice.panic(); table.panic (now); break;
        case Msg::Gate: fx.setGate (m.value, m.time, m.cancel); break;
        case Msg::Clock: voice.setClockAnchor (m.value); break;
        case Msg::RecStart: recBegin = (long long) m.time; recEnd = (long long) m.t2; recStarted = false; recActive = true; break;
        case Msg::RecStop: if (recActive) { recActive = false; recDoneFlag = true; } break;
    }
}

// ---------------------------------------------------------------- host MIDI
void MosaicProcessor::midiNoteOn (int note, int vel, double t)
{
    if (downCount[(size_t) note] > 0) { downCount[(size_t) note]++; return; }
    bool legato = false;
    while ((int) order.size() >= polyphony && ! order.empty())
    {
        legato = polyphony == 1 && glideTime > 0;
        const int old = order.front(); order.erase (order.begin()); downCount[(size_t) old] = 0;
        for (int l = 0; l < 2; ++l) { Msg m; m.kind = Msg::Off; const auto id = juce::String ("key-") + juce::String (old) + (l ? "B" : "A"); m.id = idHash (id); m.tableId = (uint32_t) m.id; m.time = t; handle (m, t); }
    }
    downCount[(size_t) note] = 1; order.push_back (note);
    for (int l = 0; l < 2; ++l)
    {
        if (l == 0 ? ! aOn : ! bOn) continue;
        Msg m; m.kind = Msg::On; const auto id = juce::String ("key-") + juce::String (note) + (l ? "B" : "A");
        m.id = idHash (id); m.tableId = (uint32_t) m.id; m.note = note; m.vel = vel; m.glide = legato ? glideTime : 0; m.legato = legato; m.layer = l; m.time = t;
        handle (m, t);
    }
}

void MosaicProcessor::midiNoteOff (int note, double t)
{
    if (downCount[(size_t) note] <= 0) return;
    if (--downCount[(size_t) note] > 0) return;
    order.erase (std::remove (order.begin(), order.end(), note), order.end());
    for (int l = 0; l < 2; ++l) { Msg m; m.kind = Msg::Off; const auto id = juce::String ("key-") + juce::String (note) + (l ? "B" : "A"); m.id = idHash (id); m.tableId = (uint32_t) m.id; m.time = t; handle (m, t); }
}

bool MosaicProcessor::popMidiForPage (uint8_t* m3)
{
    const auto r = midiOutFifo.read (1);
    if (r.blockSize1 > 0) { std::memcpy (m3, midiOut[(size_t) r.startIndex1].data(), 3); return true; }
    return false;
}

// ---------------------------------------------------------------- audio
void MosaicProcessor::renderQuantum()
{
    const long long frame = renderedFrames.load(); const double now = frame / sr;
    if (auto* p = pending.exchange (nullptr))
    {
        if (p->voice) voice.setState (p->voice);
        if (p->hasFx) fx.apply (p->fx, now);
        if (p->hasImpulse) { fx.installImpulse (std::move (p->impulse), p->impulseSeconds); p->hasImpulse = false; }
        const bool wasReplacing = table.replaces();
        table.applySettings (p->tableSettings, now);
        if (! wasReplacing && table.replaces()) voice.panic();   // TABLE ONLY: Mosaic's own voices fall silent
        if (p->waves) table.setWaves (p->waves, now);
        if (! p->motionKey.empty()) table.setMotionFrames (p->motion, p->motionKey);
        aOn = p->aOn; bOn = p->bOn; polyphony = p->polyphony; glideTime = p->glide;
        // the previous pack goes back to the message thread to be freed there
        if (live) delete retired.exchange (live.release());
        live.reset (p);
    }
    float L[128], R[128], fm[128];
    voice.process (L, R, 128, now, sr);
    for (int i = 0; i < 128; ++i) fm[i] = 0.5f * (L[i] + R[i]);
    table.process (L, R, fm, 128, frame);
    fx.process (L, R, 128, now);
    const size_t mask = ringL.size() - 1;
    for (int i = 0; i < 128; ++i) { ringL[(size_t) (frame + i) & mask] = L[i]; ringR[(size_t) (frame + i) & mask] = R[i]; }
    // the recorder's capture (as Mosaic's capture worklet: the main output, by frame)
    for (int i = 0; i < 128; ++i)
    {
        const float x = L[i], y = R[i]; const long long f = frame + i;
        recPeak = std::max (recPeak, std::max (std::abs (x), std::abs (y))); recSq += (x * x + y * y) * .5; ++recN;
        if (recActive && f >= recBegin && f < recEnd)
        {
            if (! recStarted) { recStarted = true; recStartedFlag = true; }
            const auto w = recFifo.write (1);
            if (w.blockSize1 > 0) { recBuf[(size_t) w.startIndex1 * 2] = x; recBuf[(size_t) w.startIndex1 * 2 + 1] = y; }
        }
        if (recActive && f >= recEnd) { recActive = false; recDoneFlag = true; }
    }
    if (recN >= 2048) { recMeterPeak = recPeak; recMeterRms = (float) std::sqrt (recSq / recN); recMeterFlag = true; recPeak = 0; recSq = 0; recN = 0; }
    renderedFrames = frame + 128;
}

void MosaicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int N = buffer.getNumSamples(); const long long H = hostFrames;

    // the DAW's tempo and transport, for the page
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const std::lock_guard<std::mutex> l (trLock);
            tr.hasTempo = pos->getBpm().hasValue(); if (tr.hasTempo) tr.bpm = *pos->getBpm();
            tr.hasTransport = true; tr.playing = pos->getIsPlaying(); tr.ppq = pos->getPpqPosition().orFallback (0); tr.time = H / sr;
        }

    // offline renders: wait (briefly) for the page to schedule what this block plays
    const juce::SpinLock::ScopedTryLockType hl (hostLock);
    if (isNonRealtime() && hl.isLocked() && host)
    {
        const double until = (H + N) / sr + 0.12;
        if (pumped.load() < (H + N) / sr)
        {
            wantPump = until; pumpedEvent.reset(); host->requestPump();
            pumpedEvent.wait (1500);
        }
    }

    // messages from the page
    {
        const auto r = fifo.read (fifo.getNumReady()); const double now = renderedFrames.load() / sr;
        for (int i = 0; i < r.blockSize1; ++i) handle (q[(size_t) (r.startIndex1 + i)], now);
        for (int i = 0; i < r.blockSize2; ++i) handle (q[(size_t) (r.startIndex2 + i)], now);
    }

    // host MIDI: sample-accurate notes played here; every message also goes to the page (BANK, recorder, and the arp)
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage(); const double t = (H + meta.samplePosition) / sr;
        if (msg.getRawDataSize() <= 3) { auto w = midiOutFifo.write (1); if (w.blockSize1 > 0) { auto& d = midiOut[(size_t) w.startIndex1]; d = { 0, 0, 0 }; std::memcpy (d.data(), msg.getRawData(), (size_t) msg.getRawDataSize()); } }
        if (arpActive) continue;   // BANK's arpeggiator plays these notes from the page
        using mosaic::Event; using mosaic::EvType;
        if (msg.isNoteOn()) midiNoteOn (msg.getNoteNumber(), msg.getVelocity(), t);
        else if (msg.isNoteOff()) midiNoteOff (msg.getNoteNumber(), t);
        else if (msg.isPitchWheel()) { Msg m; m.kind = Msg::Bend; m.value = (msg.getPitchWheelValue() - 8192) / 8192.0; m.time = t; handle (m, t); }
        else if (msg.isControllerOfType (1)) { Msg m; m.kind = Msg::Mod; m.value = msg.getControllerValue() / 127.0; m.time = t; handle (m, t); }
        else if (msg.isControllerOfType (64)) { Msg m; m.kind = Msg::Sustain; m.on = msg.getControllerValue() >= 64; m.time = t; handle (m, t); }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { for (int n = 0; n < 128; ++n) while (downCount[(size_t) n] > 0) midiNoteOff (n, t); }
    }

    // render whole quanta up to this block's end; output runs one quantum behind
    while (renderedFrames.load() + 128 <= H + N) renderQuantum();
    auto* outL = buffer.getWritePointer (0); auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const size_t mask = ringL.size() - 1;
    for (int i = 0; i < N; ++i)
    {
        const long long f = H + i - 128;
        const float l = f >= 0 ? ringL[(size_t) f & mask] : 0.f, r = f >= 0 ? ringR[(size_t) f & mask] : 0.f;
        outL[i] = l; if (outR) outR[i] = r;
        const auto w = outFifo.write (1); if (w.blockSize1 > 0) { outBuf[(size_t) w.startIndex1 * 2] = l; outBuf[(size_t) w.startIndex1 * 2 + 1] = r; }
    }
    hostFrames = H + N;
    midi.clear();

    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuLoad = (float) (0.9 * cpuLoad + 0.1 * (secs / (N / sr)));
    if (meterLock.try_lock())
    {
        meterSnap.voices = voice.activeVoices() + table.activeVoices(); meterSnap.cpu = cpuLoad; voice.scopeCopy (meterSnap.scope.data());
        meterLock.unlock();
    }
}

MosaicProcessor::Meter MosaicProcessor::meter() const { const std::lock_guard<std::mutex> l (meterLock); return meterSnap; }

int MosaicProcessor::readOutput (float* dst, int maxFrames)
{
    const int ready = outFifo.getNumReady();
    if (ready > maxFrames) { const auto skip = outFifo.read (ready - maxFrames); (void) skip; }   // only the newest frames matter
    const auto r = outFifo.read (std::min (maxFrames, outFifo.getNumReady())); int n = 0;
    for (int i = 0; i < r.blockSize1; ++i, ++n) { dst[n * 2] = outBuf[(size_t) (r.startIndex1 + i) * 2]; dst[n * 2 + 1] = outBuf[(size_t) (r.startIndex1 + i) * 2 + 1]; }
    for (int i = 0; i < r.blockSize2; ++i, ++n) { dst[n * 2] = outBuf[(size_t) (r.startIndex2 + i) * 2]; dst[n * 2 + 1] = outBuf[(size_t) (r.startIndex2 + i) * 2 + 1]; }
    return n;
}

MosaicProcessor::RecEvents MosaicProcessor::takeRec()
{
    RecEvents e; e.started = recStartedFlag.exchange (false);
    const auto r = recFifo.read (recFifo.getNumReady());
    for (int i = 0; i < r.blockSize1; ++i) { e.l.push_back (recBuf[(size_t) (r.startIndex1 + i) * 2]); e.r.push_back (recBuf[(size_t) (r.startIndex1 + i) * 2 + 1]); }
    for (int i = 0; i < r.blockSize2; ++i) { e.l.push_back (recBuf[(size_t) (r.startIndex2 + i) * 2]); e.r.push_back (recBuf[(size_t) (r.startIndex2 + i) * 2 + 1]); }
    e.done = recDoneFlag.exchange (false);
    if (recMeterFlag.exchange (false)) { e.meter = true; e.peak = recMeterPeak; e.rms = recMeterRms; }
    return e;
}

// ---------------------------------------------------------------- project state
void MosaicProcessor::setUiState (const juce::String& json) { const std::lock_guard<std::mutex> l (uiLock); uiState = json; }

void MosaicProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream s (dest, false);
    s.writeString ("MOSAIC1"); s.writeString (getUiState());
}

void MosaicProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream s (data, (size_t) size, false);
    if (s.readString() != "MOSAIC1") return;
    const auto json = s.readString();
    setUiState (json);
    ++stateVersion;
    // the engines take the project's sound now, whether or not the window is open; an open window reloads its page with it
    juce::MessageManager::callAsync ([this, json] { applyFullState (json); const juce::SpinLock::ScopedLockType l (hostLock); if (host) host->restoreFromProcessor(); });
}

juce::AudioProcessorEditor* MosaicProcessor::createEditor() { return new MosaicEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MosaicProcessor(); }
