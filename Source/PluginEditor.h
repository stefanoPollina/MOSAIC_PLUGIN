#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "BrowserHost.h"

// The window: Mosaic's page, talking to the engines through the BrowserHost.
class MosaicEditor : public juce::AudioProcessorEditor
{
public:
    explicit MosaicEditor (MosaicProcessor& p) : AudioProcessorEditor (&p), proc (p)
    {
        host = std::make_unique<BrowserHost> (proc);
        addAndMakeVisible (host->browser());
        proc.setHost (host.get());
        setResizable (true, true);
        setResizeLimits (960, 640, 3200, 2000);
        setSize (1440, 900);
    }
    ~MosaicEditor() override { proc.setHost (nullptr); host.reset(); }
    void resized() override { host->browser().setBounds (getLocalBounds()); }
private:
    MosaicProcessor& proc;
    std::unique_ptr<BrowserHost> host;
};
