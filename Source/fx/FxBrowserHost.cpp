#include "FxBrowserHost.h"
#include "FxProcessor.h"
#include "BinaryData.h"

FxBrowserHost::FxBrowserHost (MosaicFxProcessor& p) : proc (p)
{
    using Opts = juce::WebBrowserComponent::Options;
    auto options = Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (MosaicFxProcessor::dataDir().getChildFile ("webview"))   // Mosaic FX's own: never the synth's
                                     .withBackgroundColour (juce::Colour (0xff0d0e10))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withInitialisationData ("mosaicState", p.getUiState())
        .withInitialisationData ("mosaicFxOnly", true)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return resource (url); })
        .withEventListener ("ready", [this] (const juce::var&) { pageReady = true; })
        .withEventListener ("msg", [this] (const juce::var& v) { pageReady = true; proc.fromPage (v["list"]); })
        .withEventListener ("ui", [this] (const juce::var& v) { proc.setUiState (v["state"].toString()); })
        .withEventListener ("save", [this] (const juce::var& v)
        {
            const auto name = v["name"].toString(); juce::MemoryOutputStream mo; juce::Base64::convertFromBase64 (mo, v["data"].toString()); const auto data = mo.getMemoryBlock();
            chooser = std::make_unique<juce::FileChooser> ("Save " + name, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name));
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [data] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File{}) f.replaceWithData (data.getData(), data.getSize()); });
        })
        .withEventListener ("open", [this] (const juce::var& v)
        {
            const int id = (int) v["id"];
            chooser = std::make_unique<juce::FileChooser> ("Open", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, id] (const juce::FileChooser& fc)
            {
                juce::Array<juce::var> files;
                for (const auto& f : fc.getResults())
                {
                    juce::MemoryBlock mb; if (! f.loadFileAsData (mb)) continue;
                    auto* o = new juce::DynamicObject(); o->setProperty ("name", f.getFileName()); o->setProperty ("data", juce::Base64::toBase64 (mb.getData(), mb.getSize()));
                    files.add (juce::var (o));
                }
                auto* r = new juce::DynamicObject(); r->setProperty ("id", id); r->setProperty ("files", files);
                web->emitEventIfBrowserIsVisible ("opened", juce::var (r));
            });
        });
    web = std::make_unique<juce::WebBrowserComponent> (options);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "?fx=1&build=" + juce::String ((juce::int64) BinaryData::index_htmlSize)
                  + "-" + juce::String (__DATE__ " " __TIME__).removeCharacters (" :"));
    startTimerHz (60);
}

FxBrowserHost::~FxBrowserHost() { stopTimer(); web.reset(); }

void FxBrowserHost::restoreFromProcessor()
{
    auto* o = new juce::DynamicObject(); o->setProperty ("state", proc.getUiState());
    pageReady = false; web->emitEventIfBrowserIsVisible ("restore", juce::var (o));
}

std::optional<juce::WebBrowserComponent::Resource> FxBrowserHost::resource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
    juce::WebBrowserComponent::Resource r;
    if (path == "/" || path == "/index.html")
    {
        r.data.resize ((size_t) BinaryData::index_htmlSize); std::memcpy (r.data.data(), BinaryData::index_html, (size_t) BinaryData::index_htmlSize); r.mimeType = "text/html";
        return r;
    }
    return std::nullopt;
}

void FxBrowserHost::timerCallback()
{
    delete proc.retired.exchange (nullptr);
    if (! pageReady) return;
    { auto* o = new juce::DynamicObject(); o->setProperty ("t", proc.clockSeconds()); o->setProperty ("sr", proc.sampleRate()); o->setProperty ("offline", false); emit ("clock", juce::var (o)); }
    {
        const auto t = proc.transport(); auto* o = new juce::DynamicObject();
        o->setProperty ("hasTempo", t.hasTempo); o->setProperty ("hasTransport", t.hasTransport); o->setProperty ("playing", t.playing);
        o->setProperty ("bpm", t.bpm); o->setProperty ("ppq", t.ppq); o->setProperty ("time", t.time); emit ("transport", juce::var (o));
    }
    static int n = 0;
    if (++n % 2 == 0) { auto* o = new juce::DynamicObject(); o->setProperty ("voices", 0); o->setProperty ("cpu", proc.cpu()); o->setProperty ("scope", juce::Array<juce::var>()); emit ("meter", juce::var (o)); }
    scratch.resize (8192 * 2); const int got = proc.readOutput (scratch.data(), 4096);
    if (got > 0) { auto* o = new juce::DynamicObject(); o->setProperty ("data", juce::Base64::toBase64 (scratch.data(), (size_t) got * 2 * sizeof (float))); emit ("audio", juce::var (o)); }
}
