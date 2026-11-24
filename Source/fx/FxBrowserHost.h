#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class MosaicFxProcessor;

/* Mosaic's page in FX-only mode, in Mosaic FX's window. Its storage lives in Mosaic FX's own data folder. */
class FxBrowserHost : private juce::Timer
{
public:
    explicit FxBrowserHost (MosaicFxProcessor&);
    ~FxBrowserHost() override;
    juce::WebBrowserComponent& browser() { return *web; }
    void restoreFromProcessor();

private:
    MosaicFxProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::unique_ptr<juce::FileChooser> chooser;
    bool pageReady = false; std::vector<float> scratch;
    std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url);
    void timerCallback() override;
    void emit (const juce::Identifier& ev, const juce::var& v) { if (web) web->emitEventIfBrowserIsVisible (ev, v); }
};
