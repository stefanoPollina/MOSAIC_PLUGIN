#pragma once
/* The TABLE engine's wavetable maker (the page's wt-json core, WT.renderFrame / renderStereo / waveCoefficients),
   ported line for line so every frame matches the browser's. Runs off the audio thread. */
#include <map>
#include <string>
#include <vector>
#include "MiniJson.h"

namespace mosaic::wt
{
constexpr int SIZE = 2048, MAXH = 512;

struct Bank
{
    json::Value json;                                   // the page's wt-bank-data
    std::map<std::string, const json::Value*> byId;
    std::vector<std::string> order;                     // table ids in bank order
    void load (const std::string& text);
    const json::Value* find (const std::string& id) const { auto it = byId.find (id); return it == byId.end() ? nullptr : it->second; }
};

struct Macros { double M1 = 0, M2 = 0, M3 = 0, M4 = 0; bool given = false; };

std::vector<float> renderFrame (const json::Value& def, double m, const Bank& bank, const Macros& mv, const char* side = nullptr);
struct Stereo { std::vector<float> L, R; bool same = true; };
Stereo renderStereo (const json::Value& def, double m, const Bank& bank, const Macros& mv);
// createPeriodicWave coefficients: waveform = Σ real[k]·cos(k t) + imag[k]·sin(k t), k = 1..maxH (index 0 unused)
void waveCoefficients (const std::vector<float>& frame, int maxH, std::vector<float>& real, std::vector<float>& imag);
} // namespace mosaic::wt
