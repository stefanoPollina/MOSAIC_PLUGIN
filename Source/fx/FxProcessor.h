#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <mutex>
#include "../dsp/MosaicFx.h"

class FxBrowserHost;

/* Mosaic FX: Mosaic's whole FX bus (the same C++ engine as the Mosaic synth's) as a stereo audio effect.
   Mosaic's own FX page is its window; the page sends its FX settings exactly as it does to the synth's engines.
   A separate plugin with its own ID, data folder and installer: nothing here reads or writes the Mosaic synth's files. */
class MosaicFxProcessor : public juce::AudioProcessor
{
public:
    MosaicFxProcessor();
    ~MosaicFxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Mosaic FX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- from the page (message thread) ----
    void fromPage (const juce::var& list);
    void setUiState (const juce::String& json) { const std::lock_guard<std::mutex> l (uiLock); uiState = json; }
    juce::String getUiState() const { const std::lock_guard<std::mutex> l (uiLock); return uiState; }
    void setHost (FxBrowserHost* b) { const juce::SpinLock::ScopedLockType l (hostLock); host = b; }
    void applyFullState (const juce::String& json);

    // ---- to the page ----
    double clockSeconds() const { return (double) frames.load() / sr; }
    double sampleRate() const { return sr; }
    struct Transport { bool hasTempo = false, hasTransport = false, playing = false; double bpm = 120, ppq = 0, time = 0; };
    Transport transport() const { const std::lock_guard<std::mutex> l (trLock); return tr; }
    int readOutput (float* interleaved, int maxFrames);
    float cpu() const { return cpuLoad.load(); }
    std::atomic<int> stateVersion { 0 };

    static juce::File dataDir();   // %APPDATA%\Mosaic FX (never the synth's folder)

private:
    double sr = 48000; mosaic::MosaicFx fx;
    struct Pack { mosaic::FxState fx; juce::AudioBuffer<float> impulse; double impulseSeconds = 0; bool hasImpulse = false; };
    std::atomic<Pack*> pending { nullptr }, retired { nullptr };
    std::unique_ptr<Pack> live;
    double lastDecay = -1, appliedBpm = -1; juce::Random irRandom;
    std::atomic<long long> frames { 0 };
    mutable std::mutex trLock; Transport tr;
    juce::AbstractFifo outFifo { 1 << 15 }; std::vector<float> outBuf;
    std::atomic<float> cpuLoad { 0 };
    mutable std::mutex uiLock; juce::String uiState;
    FxBrowserHost* host = nullptr; juce::SpinLock hostLock;
    void installFx (const juce::var& m);
    friend class FxBrowserHost;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicFxProcessor)
};
