#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "BrowserHost.h"

// The window: it borrows the plugin's page while open and gives it back when closed.
class MosaicEditor : public juce::AudioProcessorEditor
{
public:
    explicit MosaicEditor (MosaicProcessor& p) : AudioProcessorEditor (&p), proc (p)
    {
        setResizable (true, true);
        setResizeLimits (960, 640, 3200, 2000);
        setSize (1440, 900);
        proc.browserHost().attachTo (this);
    }
    ~MosaicEditor() override { proc.browserHost().attachTo (nullptr); }
    void resized() override { proc.browserHost().browser().setBounds (getLocalBounds()); }
private:
    MosaicProcessor& proc;
};
