#pragma once
/* The browser build (WebAssembly) has no JUCE. MosaicFx uses JUCE only for its reverb (juce::dsp::Convolution and the buffers around it),
   so this stands in for exactly those pieces. The convolution itself is done by the browser's own ConvolverNode: process() hands this
   quantum's reverb input to the page (send) and returns the convolver's output for the previous quantum (ret), the one quantum that
   Web Audio's cycle rule puts between them. */
#include <algorithm>
#include <cstring>
#include <vector>

namespace mosaic::wasmrev
{
inline float send[2][128] {}, ret[2][128] {};
inline bool sentAny = false;   // false: this quantum sent silence (the reverb was resting)
}

namespace juce
{
template <typename T> class AudioBuffer
{
public:
    AudioBuffer() = default;
    AudioBuffer (int ch, int n) { setSize (ch, n); }
    void setSize (int ch, int n, bool = false, bool = false, bool = false) { nc = ch; ns = n; for (int c = 0; c < 2; ++c) d[c].resize ((size_t) std::max (n, 1)); ptr[0] = d[0].data(); ptr[1] = d[1].data(); }
    int getNumSamples() const { return ns; }
    int getNumChannels() const { return nc; }
    T getSample (int c, int i) const { return d[(size_t) c][(size_t) i]; }
    void setSample (int c, int i, T v) { d[(size_t) c][(size_t) i] = v; }
    T* getWritePointer (int c) { return d[(size_t) c].data(); }
    T* const* getArrayOfWritePointers() { ptr[0] = d[0].data(); ptr[1] = d[1].data(); return ptr; }
    void clear() { for (auto& v : d) std::fill (v.begin(), v.end(), T()); }
    void applyGain (T g) { for (auto& v : d) for (auto& x : v) x *= g; }
private:
    int nc = 0, ns = 0; std::vector<T> d[2]; T* ptr[2] {};
};

namespace dsp
{
struct ProcessSpec { double sampleRate; unsigned maximumBlockSize, numChannels; };
template <typename T> struct AudioBlock { T* const* ch; size_t nch, n; AudioBlock (T* const* c, size_t a, size_t b) : ch (c), nch (a), n (b) {}
    explicit AudioBlock (AudioBuffer<T>& buf) : ch (buf.getArrayOfWritePointers()), nch (2), n ((size_t) buf.getNumSamples()) {} };
template <typename T> struct ProcessContextReplacing { AudioBlock<T>& b; explicit ProcessContextReplacing (AudioBlock<T>& x) : b (x) {} };
class Convolution
{
public:
    struct NonUniform { int headSizeInSamples; };
    enum class Stereo { no, yes }; enum class Trim { no, yes }; enum class Normalise { no, yes };
    explicit Convolution (NonUniform) {}
    void prepare (const ProcessSpec&) {}
    void reset() {}
    template <typename B> void loadImpulseResponse (B&&, double, Stereo, Trim, Normalise) {}
    int getCurrentIRSize() const { return 0; }
    void process (const ProcessContextReplacing<float>& c)
    {
        using namespace mosaic::wasmrev;
        const size_t n = std::min<size_t> (c.b.n, 128);
        for (int k = 0; k < 2; ++k) { std::memcpy (send[k], c.b.ch[k], n * sizeof (float)); std::memcpy (c.b.ch[k], ret[k], n * sizeof (float)); }
        sentAny = true;
    }
};
} // namespace dsp
struct Thread { static void sleep (int) {} };
} // namespace juce
