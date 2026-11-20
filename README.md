# MOSAIC PLUGIN

Continuum Mosaic as a VST3 instrument (and standalone app) for Windows, built from the browser version at
https://stefanopollina.github.io/mosaic_subtractive_fm/.

Work in progress. Done so far:

- **Voice engine** (`Source/dsp/MosaicVoice.*`): the page's `mosaic-voice` AudioWorklet ported to C++. Both layers,
  three oscillators each, every filter type (LP/HP/BP/notch/comb/formant/drip), amp and filter envelopes, FM / phase
  distortion, 16 LFOs with their step sequencers, the LFO-SEQ umbrella sequencer and the mod matrix.
  `tests/compare_voice.py` renders the browser worklet and the C++ engine with the same seed, state and timed notes:
  sample-identical on all 70 presets tested.
- **State reader** (`Source/dsp/MosaicState.*`, `MiniJson.h`): the page's sound state, read exactly the way the
  worklet reads it (including its defaults).

Next: the effects chain, the TABLE wavetable engine, sequencers and recorder on the DAW clock, the plugin shell
(Mosaic's own interface in the plugin window), and the Windows build.
