#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class MosaicProcessor;

/* Mosaic's page in the plugin window, and everything that passes between it and the engines. */
class BrowserHost : private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit BrowserHost (MosaicProcessor&);
    ~BrowserHost() override;
    juce::WebBrowserComponent& browser() { return *web; }
    void requestPump() { triggerAsyncUpdate(); }  // offline renders (any thread)
    void restoreFromProcessor();                 // the host loaded a project: reload the page with its state

private:
    MosaicProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::unique_ptr<juce::FileChooser> chooser;
    bool pageReady = false; std::vector<float> scratch;
    std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url);
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void emit (const juce::Identifier& ev, const juce::var& v) { if (web) web->emitEventIfBrowserIsVisible (ev, v); }
    static juce::String b64 (const float* data, size_t count);
};
