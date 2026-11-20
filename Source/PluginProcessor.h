#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include "dsp/MosaicFx.h"
#include "dsp/MosaicState.h"
#include "dsp/MosaicVoice.h"
#include "dsp/TableEngine.h"

class BrowserHost;

/* Continuum Mosaic as a plugin. The C++ engines make every sound (Mosaic's voices, TABLE, the effects);
   Mosaic's own page (kept running even while the window is closed) is the brain: presets, banks, sequencer, BANK,
   automation and the editors. The page sends the same audio messages it sends its Web Audio engine in a browser. */
class MosaicProcessor : public juce::AudioProcessor
{
public:
    MosaicProcessor();
    ~MosaicProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Mosaic"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- from the page (message thread) ----
    void fromPage (const juce::var& list);              // a batch of Mosaic audio messages
    void setUiState (const juce::String& json);          // the full Mosaic state, saved with the project
    juce::String getUiState() const { const std::lock_guard<std::mutex> l (uiLock); return uiState; }
    std::atomic<int> stateVersion { 0 };                 // bumped when the host restores a project
    BrowserHost& browserHost();                          // the page, alive while the plugin exists

    // ---- to the page (read by the BrowserHost timer, message thread) ----
    struct Meter { int voices = 0; float cpu = 0; std::array<float, 160> scope {}; };
    Meter meter() const;
    double clockSeconds() const { return (double) renderedFrames.load() / sr; }
    double sampleRate() const { return sr; }
    struct Transport { bool hasTempo = false, hasTransport = false, playing = false; double bpm = 120, ppq = 0, time = 0; };
    Transport transport() const { const std::lock_guard<std::mutex> l (trLock); return tr; }
    int readOutput (float* interleaved, int maxFrames);  // the newest output frames since the last call (for the page's scopes)
    struct RecEvents { bool started = false, done = false; std::vector<float> l, r; float peak = 0, rms = 0; bool meter = false; };
    RecEvents takeRec();
    bool offline() const { return isNonRealtime(); }
    std::atomic<double> wantPump { -1 }, pumped { -1 };  // offline renders: how far the page's sequencer must have scheduled
    juce::WaitableEvent pumpedEvent;

    static juce::File dataDir();

private:
    // ---- the engines (audio thread) ----
    double sr = 48000; mosaic::VoiceEngine voice; mosaic::MosaicFx fx; mosaic::TableEngine table;
    std::shared_ptr<mosaic::wt::Bank> wtBank;

    // parsed state, handed from the message thread to the audio thread
    struct Pack
    {
        std::shared_ptr<const mosaic::State> voice; mosaic::FxState fx; bool hasFx = false;
        mosaic::TableSettings tableSettings; std::shared_ptr<mosaic::TableWaves> waves; std::map<int, std::shared_ptr<mosaic::TableWaves>> motion; std::string motionKey;
        juce::AudioBuffer<float> impulse; double impulseSeconds = 0; bool hasImpulse = false;
        bool aOn = true, bOn = true; int polyphony = 8; double glide = 0;
    };
    std::atomic<Pack*> pending { nullptr }, retired { nullptr };
    std::unique_ptr<Pack> live;            // owned by the audio thread once installed
    std::string lastWaveKey, lastMotionKey; double lastDecay = -1; std::shared_ptr<mosaic::TableWaves> lastWaves;
    std::map<int, std::shared_ptr<mosaic::TableWaves>> lastMotion;

    // timed messages, message thread → audio thread
    struct Msg { enum Kind : uint8_t { On, Off, Bend, Mod, Sustain, Panic, Gate, Clock, RecStart, RecStop } kind = On;
                 uint64_t id = 0; uint32_t tableId = 0; int note = 60; double vel = 100, glide = 0, value = 0, time = 0, t2 = 0; int layer = 0; bool legato = false, on = false, cancel = false; };
    static constexpr int QN = 4096; std::array<Msg, QN> q; juce::AbstractFifo fifo { QN };
    void push (const Msg& m);
    void handle (const Msg& m, double now);

    // host MIDI, played here exactly as Mosaic's keyboard code plays a note (or handed to the page while BANK's arp runs)
    std::array<int, 128> downCount {}; std::vector<int> order; double bendRange = 2;
    std::atomic<bool> arpActive { false };
    void midiNoteOn (int note, int vel, double t); void midiNoteOff (int note, double t);
    juce::AbstractFifo midiOutFifo { 1024 }; std::array<std::array<uint8_t, 3>, 1024> midiOut {};
    friend class BrowserHost;
    bool popMidiForPage (uint8_t* m3);

    // rendering in 128-frame quanta (one quantum of latency, reported to the host)
    std::atomic<long long> renderedFrames { 0 }; long long hostFrames = 0;
    std::vector<float> ringL, ringR; long long ringRead = 0;
    void renderQuantum();
    bool aOn = true, bOn = true; int polyphony = 8; double glideTime = 0;

    // outputs for the page
    mutable std::mutex meterLock; Meter meterSnap; float cpuLoad = 0;
    mutable std::mutex trLock; Transport tr; bool wasPlaying = false;
    juce::AbstractFifo outFifo { 1 << 15 }; std::vector<float> outBuf;
    // recorder (the page's capture worklet)
    std::atomic<bool> recActive { false }; std::atomic<long long> recBegin { 0 }, recEnd { 0 }; bool recStarted = false;
    std::atomic<bool> recStartedFlag { false }, recDoneFlag { false };
    juce::AbstractFifo recFifo { 1 << 20 }; std::vector<float> recBuf;
    float recPeak = 0; double recSq = 0; int recN = 0; std::atomic<bool> recMeterFlag { false }; std::atomic<float> recMeterPeak { 0 }, recMeterRms { 0 };

    mutable std::mutex uiLock; juce::String uiState;
    std::unique_ptr<BrowserHost> host;
    juce::Random irRandom;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicProcessor)
};
