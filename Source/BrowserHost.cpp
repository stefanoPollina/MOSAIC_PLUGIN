#include "BrowserHost.h"
#include "PluginProcessor.h"
#include "BinaryData.h"

BrowserHost::BrowserHost (MosaicProcessor& p) : proc (p)
{
    using Opts = juce::WebBrowserComponent::Options;
    auto options = Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     // a lasting profile: Mosaic's user presets and banks live in the page's own storage
                                     .withUserDataFolder (MosaicProcessor::dataDir().getChildFile ("webview"))
                                     .withBackgroundColour (juce::Colour (0xff0d0e10))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withInitialisationData ("mosaicState", p.getUiState())
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return resource (url); })
        .withEventListener ("ready", [this] (const juce::var&) { pageReady = true; })
        .withEventListener ("msg", [this] (const juce::var& v) { proc.fromPage (v["list"]); })
        .withEventListener ("ui", [this] (const juce::var& v) { proc.setUiState (v["state"].toString()); })
        .withEventListener ("pumped", [this] (const juce::var& v) { proc.pumped = (double) v["until"]; proc.pumpedEvent.signal(); })
        .withEventListener ("save", [this] (const juce::var& v)
        {
            // Mosaic's exports (WAV, MIDI, presets, banks): a save dialog instead of a browser download
            const auto name = v["name"].toString(); juce::MemoryBlock data; data.fromBase64Encoding (v["data"].toString());
            chooser = std::make_unique<juce::FileChooser> ("Save " + name, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name));
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [data] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File{}) f.replaceWithData (data.getData(), data.getSize()); });
        });
    web = std::make_unique<juce::WebBrowserComponent> (options);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (60);
}

BrowserHost::~BrowserHost() { stopTimer(); cancelPendingUpdate(); web.reset(); }

void BrowserHost::restoreFromProcessor()
{
    // the page swaps in the project's state and reloads itself
    auto* o = new juce::DynamicObject(); o->setProperty ("state", proc.getUiState());
    pageReady = false; web->emitEventIfBrowserIsVisible ("restore", juce::var (o));
}

std::optional<juce::WebBrowserComponent::Resource> BrowserHost::resource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
    juce::WebBrowserComponent::Resource r;
    auto bytes = [&r] (const void* d, size_t n, const char* mime) { r.data.resize (n); std::memcpy (r.data.data(), d, n); r.mimeType = mime; };
    if (path == "/" || path == "/index.html") { bytes (BinaryData::index_html, (size_t) BinaryData::index_htmlSize, "text/html"); return r; }
    return std::nullopt;
}

juce::String BrowserHost::b64 (const float* data, size_t count) { return juce::Base64::toBase64 (data, count * sizeof (float)); }

void BrowserHost::handleAsyncUpdate()
{
    auto* o = new juce::DynamicObject(); o->setProperty ("now", proc.clockSeconds()); o->setProperty ("until", proc.wantPump.load());
    if (pageReady) emit ("pump", juce::var (o)); else proc.pumpedEvent.signal();
}

void BrowserHost::timerCallback()
{
    delete proc.retired.exchange (nullptr);   // packs the audio thread has finished with
    if (! pageReady) return;
    // clock
    { auto* o = new juce::DynamicObject(); o->setProperty ("t", proc.clockSeconds()); o->setProperty ("sr", proc.sampleRate()); o->setProperty ("offline", proc.offline()); emit ("clock", juce::var (o)); }
    // the DAW's tempo and transport
    {
        const auto t = proc.transport(); auto* o = new juce::DynamicObject();
        o->setProperty ("hasTempo", t.hasTempo); o->setProperty ("hasTransport", t.hasTransport); o->setProperty ("playing", t.playing);
        o->setProperty ("bpm", t.bpm); o->setProperty ("ppq", t.ppq); o->setProperty ("time", t.time); emit ("transport", juce::var (o));
    }
    // meter (voices, cpu, waveform) every other frame, the output for scopes and spectra every frame
    static int n = 0;
    if (++n % 2 == 0)
    {
        const auto m = proc.meter(); auto* o = new juce::DynamicObject(); juce::Array<juce::var> sc; for (float v : m.scope) sc.add (v);
        o->setProperty ("voices", m.voices); o->setProperty ("cpu", m.cpu); o->setProperty ("scope", sc); emit ("meter", juce::var (o));
    }
    scratch.resize (8192 * 2); const int got = proc.readOutput (scratch.data(), 4096);
    if (got > 0) { auto* o = new juce::DynamicObject(); o->setProperty ("data", b64 (scratch.data(), (size_t) got * 2)); emit ("audio", juce::var (o)); }
    // the recorder's capture
    auto rec = proc.takeRec();
    auto recMsg = [this] (juce::DynamicObject* o) { emit ("rec", juce::var (o)); };
    if (rec.started) { auto* o = new juce::DynamicObject(); o->setProperty ("type", "started"); recMsg (o); }
    if (! rec.l.empty()) { auto* o = new juce::DynamicObject(); o->setProperty ("type", "chunk"); o->setProperty ("a", b64 (rec.l.data(), rec.l.size())); o->setProperty ("b", b64 (rec.r.data(), rec.r.size())); recMsg (o); }
    if (rec.meter) { auto* o = new juce::DynamicObject(); o->setProperty ("type", "meter"); o->setProperty ("peak", rec.peak); o->setProperty ("rms", rec.rms); recMsg (o); }
    if (rec.done) { auto* o = new juce::DynamicObject(); o->setProperty ("type", "done"); recMsg (o); }
    // host MIDI, for BANK and the recorder (and the arp, which then plays the notes)
    juce::Array<juce::var> midi; uint8_t m3[3];
    while (proc.popMidiForPage (m3)) { juce::Array<juce::var> b; b.add ((int) m3[0]); b.add ((int) m3[1]); b.add ((int) m3[2]); midi.add (b); }
    if (! midi.isEmpty()) { auto* o = new juce::DynamicObject(); o->setProperty ("list", midi); emit ("midi", juce::var (o)); }
}
