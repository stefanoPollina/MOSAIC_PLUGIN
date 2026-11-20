# Mosaic — VST3 + standalone

Continuum Mosaic (https://stefanopollina.github.io/mosaic_subtractive_fm/) as a Windows VST3 instrument and standalone app.

**Download:** https://github.com/stefanoPollina/MOSAIC_PLUGIN/releases/latest
(`Mosaic-Windows-Setup.exe`, or `Mosaic-Windows-Plugins.zip` to copy by hand into `C:\Program Files\Common Files\VST3`).

## How it is built

- **The interface is Mosaic's own page**, unchanged except for a small bridge (`ui/bridge.js`, injected by `tools/build_ui.py`).
  Presets, the factory and user banks, the sequencer, BANK, automation lanes, MOTION and every editor work as in the browser.
  The page stays running (in a hidden window) when the plugin window is closed.
- **The sound is made in C++**, ported from the page and checked against it in Chrome:
  - `Source/dsp/MosaicVoice` — the voice engine (both layers, LFOs, sequencers): sample-identical (`tests/compare_voice.py`, 70/70).
  - `Source/dsp/MosaicFx` + `WebAudioParts` — the whole effects chain, rebuilt with Chrome's own Web Audio behaviour
    (`tests/compare_fx.py`, 29/29; 72–122 dB, AIR 47 dB against a browser that does not repeat itself).
  - `Source/dsp/TableEngine`, `WtCore`, `PeriodicOsc` — TABLE, the wavetable engine: all 134 tables bit-exact
    (`tests/compare_wt.py`), voices 81–125 dB (`tests/compare_table.py`; self-FM 35–39 dB).
- **Host MIDI** plays notes sample-accurately in C++ (the page still hears it for BANK and the recorder). While BANK's
  arpeggiator is on, the page plays the notes.
- **DAW sync:** the host tempo drives Mosaic's tempo; host play/stop starts and stops Mosaic's sequencer on the host's step grid.
  Offline (faster than real time) renders wait for the page to schedule each block.
- **Recorder:** captures the plugin's output; exports open a save dialog.
- One quantum (128 samples) of latency, reported to the host.

## Building

Push to GitHub: the workflow builds the VST3, the standalone app and the installer, and publishes a Release.
Locally (Windows, Visual Studio 2022): `cmake -S . -B build -A x64 && cmake --build build --config Release`.
To pick up a newer Mosaic page: `python3 tools/build_ui.py path/to/mosaic_subtractive_fm/index.html`.

## Known differences from the browser

- Changing reverb decay crossfades the impulse over 0.05 s (the browser: 0.18 s).
- The recorder's PLAY (audition) is silent in the plugin; export the take instead.
- External (microphone) input to the recorder is not available.
