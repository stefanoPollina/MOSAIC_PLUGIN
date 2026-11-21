"""Builds the plugin's page from Mosaic's live page: ui/index.html (Mosaic + the bridge) and ui/wtbank.json (TABLE's bank).
   python3 tools/build_ui.py path/to/mosaic_subtractive_fm/index.html"""
import json, re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / 'ui' / 'mosaic.html').read_text(encoding='utf-8')
bridge = (root / 'ui' / 'bridge.js').read_text(encoding='utf-8')
PATCHES = [
    # Mosaic's engine plays through the plugin
    ('const engine = new Engine();', 'const engine = (window.MosaicPlugin ? window.MosaicPlugin.makeEngine(Engine) : new Engine());'),
    # the plugin follows the DAW's transport with Mosaic's own sequencer
    ('engine.beforeRender = (end) => sequencer.pump(end);', 'engine.beforeRender = (end) => sequencer.pump(end); if (window.MosaicPlugin) window.__mosaicSequencer = sequencer;'),
    # host MIDI: the plugin plays the notes; the page still hears them for BANK, the recorder and the keyboard display
    ('  engine.onMeter = (meter) => {',
     '  if (window.MosaicPlugin) { MosaicPlugin.onMidi = onMidi; MosaicPlugin.observeMidi = (d) => { try { recorder.captureMidi(d); } catch (e) { /* recorder not ready */ } '
     'const s = d[0] & 0xf0, d1 = d[1], d2 = d[2] || 0; bank.midi(s, d1, d2); '
     'if (s === 0x90 && d2 > 0) { bank.noteOn(d1, d2); try { xyAttack(d2); } catch (e) { /* no XY */ } paintKey(d1, true); } '
     'else if (s === 0x80 || (s === 0x90 && d2 === 0)) { bank.noteOff(d1); paintKey(d1, false); } '
     'else if (s === 0xb0 && window.__mosaicCcIn) window.__mosaicCcIn(d1, d2); }; }\n  engine.onMeter = (meter) => {'),
]
for a, b in PATCHES:
    n = src.count(a)
    if n != 1: sys.exit(f'patch anchor found {n} times (expected 1): {a[:60]}')
    src = src.replace(a, b)
assert src.count('<head>') == 1
src = src.replace('<head>', '<head>\n<script>\n' + bridge.replace('</script>', '<\\/script>') + '\n</script>', 1)
(root / 'ui' / 'index.html').write_text(src, encoding='utf-8')
m = re.search(r'<script[^>]*id="wt-bank-data"[^>]*>(.*?)</script>', src, re.S)
json.loads(m.group(1)); (root / 'ui' / 'wtbank.json').write_text(m.group(1), encoding='utf-8')
print('ui/index.html', len(src), 'bytes; ui/wtbank.json', len(m.group(1)), 'bytes')
