#include "FxProcessor.h"
#include "FxEditor.h"
#include "FxBrowserHost.h"

namespace { mosaic::json::Value toValue (const juce::var& v) { return mosaic::json::parse (juce::JSON::toString (v, true).toStdString()); } }

juce::File MosaicFxProcessor::dataDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Mosaic FX"); }

MosaicFxProcessor::MosaicFxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    outBuf.assign ((size_t) outFifo.getTotalSize() * 2, 0.f);
}

MosaicFxProcessor::~MosaicFxProcessor() { delete pending.exchange (nullptr); delete retired.exchange (nullptr); }

bool MosaicFxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void MosaicFxProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate; frames = 0; appliedBpm = -1;
    fx.prepare (sr, (uint32_t) juce::Random::getSystemRandom().nextInt());
    fx.externalImpulses = false;
    if (auto* p = pending.exchange (nullptr)) { if (live) delete retired.exchange (live.release()); live.reset (p); }
    if (live) fx.apply (live->fx, 0);
    else { mosaic::FxState d; d.master = 1.0; d.reverbWet = 0; fx.apply (d, 0); }   // until the page sends its settings: a clean pass
    fx.externalImpulses = true;
    lastDecay = mosaic::MosaicFx::decayFor (live ? live->fx : mosaic::FxState());
    setLatencySamples (0);
}

void MosaicFxProcessor::installFx (const juce::var& m)
{
    auto* p = new Pack();
    juce::DynamicObject::Ptr full = new juce::DynamicObject(), seq = new juce::DynamicObject();
    full->setProperty ("fx", m["fx"]); full->setProperty ("master", 1.0); seq->setProperty ("bpm", m["bpm"]); full->setProperty ("seq", juce::var (seq.get()));
    p->fx = mosaic::FxState::fromJson (toValue (juce::var (full.get())));
    p->fx.master = 1.0;   // an effect on a track: unity, the track fader does the rest
    const double decay = mosaic::MosaicFx::decayFor (p->fx);
    if (std::abs (decay - lastDecay) > 0.25)
    {
        p->impulse = mosaic::MosaicFx::makeImpulse (sr, decay, [this] { return irRandom.nextDouble(); });
        p->impulseSeconds = decay; p->hasImpulse = true; lastDecay = decay;
    }
    if (auto* old = pending.exchange (p))
    {
        if (! p->hasImpulse && old->hasImpulse) { p->impulse = std::move (old->impulse); p->impulseSeconds = old->impulseSeconds; p->hasImpulse = true; }
        delete old;
    }
    delete retired.exchange (nullptr);
}

void MosaicFxProcessor::fromPage (const juce::var& list)
{
    if (! list.isArray()) return;
    for (auto& m : *list.getArray())
        if (m["type"].toString() == "state") installFx (m);   // notes, clock, gate and recorder messages have nothing to do in an effect
}

void MosaicFxProcessor::applyFullState (const juce::String& json)
{
    const auto s = juce::JSON::parse (json); if (! s.isObject()) return;
    juce::DynamicObject::Ptr m = new juce::DynamicObject();
    m->setProperty ("type", "state"); m->setProperty ("fx", s["fx"]);
    m->setProperty ("bpm", s["seq"].isObject() && (double) s["seq"]["bpm"] > 0 ? (double) s["seq"]["bpm"] : 120.0);
    installFx (juce::var (m.get()));
}

void MosaicFxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int N = buffer.getNumSamples(), chans = buffer.getNumChannels();
    if (N <= 0 || chans <= 0) return;

    double hostBpm = -1;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const std::lock_guard<std::mutex> l (trLock);
            tr.hasTempo = pos->getBpm().hasValue(); if (tr.hasTempo) { tr.bpm = *pos->getBpm(); hostBpm = tr.bpm; }
            tr.hasTransport = true; tr.playing = pos->getIsPlaying(); tr.ppq = pos->getPpqPosition().orFallback (0); tr.time = frames.load() / sr;
        }

    const double now = frames.load() / sr;
    if (auto* p = pending.exchange (nullptr))
    {
        if (hostBpm > 20 && hostBpm < 400) p->fx.bpm = hostBpm;
        fx.apply (p->fx, now); appliedBpm = p->fx.bpm;
        if (p->hasImpulse) { fx.installImpulse (std::move (p->impulse), p->impulseSeconds); p->hasImpulse = false; }
        if (live) delete retired.exchange (live.release());
        live.reset (p);
    }
    else if (live && hostBpm > 20 && hostBpm < 400 && std::abs (hostBpm - appliedBpm) > 1e-3)
    {   // the DAW's tempo moved: synced echoes, gates and motion follow it
        live->fx.bpm = hostBpm; fx.apply (live->fx, now); appliedBpm = hostBpm;
    }

    auto* L = buffer.getWritePointer (0); auto* R = chans > 1 ? buffer.getWritePointer (1) : nullptr;
    float tmpR[128];
    for (int off = 0; off < N; off += 128)
    {
        const int n = std::min (128, N - off);
        float* l = L + off; float* r = R ? R + off : tmpR;
        if (! R) std::memcpy (tmpR, l, sizeof (float) * (size_t) n);   // a mono track: the bus runs in stereo, L is written back
        fx.process (l, r, n, (frames.load() + off) / sr);
        for (int i = 0; i < n; ++i)
        {   // never hand the DAW a non-finite sample
            if (! std::isfinite (l[i])) l[i] = 0.f;
            if (! std::isfinite (r[i])) r[i] = 0.f;
        }
    }
    for (int i = 0; i < N; ++i)
    {
        const auto w = outFifo.write (1);
        if (w.blockSize1 > 0) { outBuf[(size_t) w.startIndex1 * 2] = L[i]; outBuf[(size_t) w.startIndex1 * 2 + 1] = R ? R[i] : L[i]; }
    }
    frames = frames.load() + N;
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuLoad = (float) (0.9 * cpuLoad.load() + 0.1 * (secs / (N / sr)));
}

int MosaicFxProcessor::readOutput (float* dst, int maxFrames)
{
    const int ready = outFifo.getNumReady();
    if (ready > maxFrames) { const auto skip = outFifo.read (ready - maxFrames); (void) skip; }
    const auto r = outFifo.read (std::min (maxFrames, outFifo.getNumReady())); int n = 0;
    for (int i = 0; i < r.blockSize1; ++i, ++n) { dst[n * 2] = outBuf[(size_t) (r.startIndex1 + i) * 2]; dst[n * 2 + 1] = outBuf[(size_t) (r.startIndex1 + i) * 2 + 1]; }
    for (int i = 0; i < r.blockSize2; ++i, ++n) { dst[n * 2] = outBuf[(size_t) (r.startIndex2 + i) * 2]; dst[n * 2 + 1] = outBuf[(size_t) (r.startIndex2 + i) * 2 + 1]; }
    return n;
}

void MosaicFxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream s (dest, false); s.writeString ("MOSAICFX1"); s.writeString (getUiState());
}

void MosaicFxProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream s (data, (size_t) size, false);
    if (s.readString() != "MOSAICFX1") return;   // only Mosaic FX's own projects (never a Mosaic synth state)
    const auto json = s.readString(); setUiState (json); ++stateVersion;
    juce::MessageManager::callAsync ([this, json] { applyFullState (json); const juce::SpinLock::ScopedLockType l (hostLock); if (host) host->restoreFromProcessor(); });
}

juce::AudioProcessorEditor* MosaicFxProcessor::createEditor() { return new MosaicFxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MosaicFxProcessor(); }
