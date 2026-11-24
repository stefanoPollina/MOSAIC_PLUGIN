#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FxProcessor.h"
#include "FxBrowserHost.h"

// Mosaic FX's window: Mosaic's FX page.
class MosaicFxEditor : public juce::AudioProcessorEditor
{
public:
    explicit MosaicFxEditor (MosaicFxProcessor& p) : AudioProcessorEditor (&p), proc (p)
    {
        host = std::make_unique<FxBrowserHost> (proc);
        addAndMakeVisible (host->browser());
        proc.setHost (host.get());
        setResizable (true, true);
        setResizeLimits (900, 560, 3200, 2000);
        setSize (1360, 860);
    }
    ~MosaicFxEditor() override { proc.setHost (nullptr); host.reset(); }
    void resized() override { host->browser().setBounds (getLocalBounds()); }
private:
    MosaicFxProcessor& proc;
    std::unique_ptr<FxBrowserHost> host;
};
