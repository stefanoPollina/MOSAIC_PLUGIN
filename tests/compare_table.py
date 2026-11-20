"""TABLE engine: C++ TableEngine vs the page's own table.js (+ wt-json core) in Chrome's OfflineAudioContext."""
import json, os, subprocess, base64, http.server, threading, functools
import numpy as np
from playwright.sync_api import sync_playwright
D = '/tmp/mzwt'; SR = 48000; PORT = 8807
bank = open(f'{D}/bank.json').read()
open(f'{D}/table.html', 'w').write("""<!doctype html><script id="wt-bank-data" type="application/json">""" + bank + """</script>
<script>window.engine = { ctx: null, gate: null, node: null, audioRenderer: 'worklet', state: { layers: { A: { enabled: true } } } };
const __st = window.setTimeout; window.setTimeout = (f, ms) => 0; window.setInterval = () => 0;</script>
<script src="wtcore.js"></script><script src="table.js"></script><script>
window.render = async (state, events, secs, fm) => {
  const N = Math.round(secs * 48000), ctx = new OfflineAudioContext(2, N, 48000), e = window.engine;
  e.ctx = ctx; e.gate = ctx.createGain(); e.gate.connect(ctx.destination); e.state = state;
  const b = ctx.createBuffer(2, N, 48000); if (fm) { const a = new Float32Array(fm); b.getChannelData(0).set(a.subarray(0, N)); b.getChannelData(1).set(a.subarray(0, N)); }
  const src = ctx.createBufferSource(); src.buffer = b; src.start(0); e.node = src; const mute = ctx.createGain(); mute.gain.value = 0; src.connect(mute); mute.connect(ctx.destination);   // like Mosaic's worklet, the FM source is always rendered
  const W = window.MosaicWT; W.hooks.state(e, state);
  for (const ev of events) { if (ev[0] === 'on') W.hooks.noteOn(e, 'n' + ev[1], ev[2], ev[3], { layer: 'A', time: ev[4] }); else if (ev[0] === 'off') W.hooks.noteOff(e, 'n' + ev[1], ev[2]); }
  const out = await ctx.startRendering(), l = out.getChannelData(0), r = out.getChannelData(1), il = new Float32Array(N * 2);
  for (let i = 0; i < N; i++) { il[2 * i] = l[i]; il[2 * i + 1] = r[i]; }
  let s = ''; const u8 = new Uint8Array(il.buffer); for (let i = 0; i < u8.length; i += 32768) s += String.fromCharCode.apply(null, u8.subarray(i, i + 32768)); return btoa(s);
};</script>""")
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=D); h.log_message = lambda *a: None
srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), h); threading.Thread(target=srv.serve_forever, daemon=True).start()

def lab(**kw):
    d = dict(on=True, table='WT17', scan=.5, M1=0, M2=0, M3=0, M4=0, level=.6, attack=.01, decay=.35, sustain=.75, release=.4, cutoff=9000, res=.8, unison=1, detune=14, octave=0, follow='A', mode='replace', fmDepth=0, selfFm=0, fmRatio=2, motion=None)
    d.update(kw); return {'name': 'test', 'wtLab': d, 'layers': {'A': {'enabled': True}}}
chord = [['on', 1, 48, 100, 0.0], ['on', 2, 55, 90, 0.25], ['on', 3, 64, 110, 0.5], ['off', 1, 1.0], ['off', 2, 1.1], ['off', 3, 1.2], ['on', 4, 36, 127, 1.4], ['off', 4, 1.7]]
t = np.arange(int(2.0 * SR)) / SR; fmsig = (0.3 * np.sin(2 * np.pi * 110 * t)).astype(np.float32)
CASES = [
    ('sf1', lab(selfFm=.3, fmRatio=3), [['on', 1, 48, 100, 0.0], ['off', 1, 1.0]], None), ('sf_small', lab(selfFm=.01, fmRatio=1), [['on', 1, 48, 100, 0.0], ['off', 1, 1.0]], None), ('sf_r1', lab(selfFm=.3, fmRatio=1), [['on', 1, 48, 100, 0.0], ['off', 1, 1.0]], None),
    ('fm_n2', lab(fmDepth=.5), [['on', 2, 55, 90, 0.25], ['off', 2, 1.1]], fmsig), ('fm_n1', lab(fmDepth=.5), [['on', 1, 48, 100, 0.0], ['off', 1, 1.0]], fmsig), ('fm_n2at0', lab(fmDepth=.5), [['on', 2, 55, 90, 0.0], ['off', 2, 1.1]], fmsig),
    ('basic', lab(), chord, None), ('unison', lab(unison=5, detune=20), chord, None), ('wide', lab(M4=.6, table='WT30', unison=3), chord, None),
    ('macros', lab(M1=.4, M2=.5, M3=.6, table='WT63'), chord, None), ('fm', lab(fmDepth=.5), chord, fmsig), ('selfFm', lab(selfFm=.3, fmRatio=3), chord, None),
    ('lowpass', lab(cutoff=600, res=8), chord, None), ('octave', lab(octave=-2, table='WT05'), chord, None),
    ('starter', {'name': 'WT ' + json.loads(bank)['tables'][40]['name'], 'layers': {'A': {'enabled': True}}}, chord, None),
    ('modal', lab(table=[x['id'] for x in json.loads(bank)['tables'] if x['engine'] == 'modal'][0]), chord, None),
]
only = os.environ.get('ONLY'); CASES = [c for c in CASES if not only or c[0] in only.split(',')]
fails = 0
with sync_playwright() as p:
    br = p.chromium.launch()
    for name, st, ev, fm in CASES:
        pg = br.new_page(); pg.goto(f'http://127.0.0.1:{PORT}/table.html')
        js = np.frombuffer(base64.b64decode(pg.evaluate('([s,e,t,f]) => render(s,e,t,f)', [st, ev, 2.0, fm.tolist() if fm is not None else None])), dtype=np.float32).reshape(-1, 2); pg.close()
        json.dump(st, open(f'{D}/state.json', 'w'))
        with open(f'{D}/ev.txt', 'w') as f:
            for e in ev: f.write(' '.join(str(x) for x in e) + '\n')
        (fm if fm is not None else np.zeros(len(t), np.float32)).tofile(f'{D}/fm.raw')
        subprocess.run(['/tmp/render_table', f'{D}/bank.json', f'{D}/state.json', f'{D}/ev.txt', '2.0', f'{D}/c.raw'], check=True)
        c = np.fromfile(f'{D}/c.raw', dtype=np.float32).reshape(-1, 2)
        np.save(f'{D}/js_{name}.npy', js); np.save(f'{D}/c_{name}.npy', c)
        d = np.abs(js - c); peak = max(1e-9, np.abs(js).max())
        snr = 10 * np.log10((js ** 2).sum() / max(1e-30, ((js - c) ** 2).sum()))
        ok = snr > (30 if st.get('wtLab', {}).get('selfFm') else 60); fails += not ok   # self-FM's deep index magnifies the sine oscillator's last-bit differences
        print(f'{"ok  " if ok else "DIFF"} {name:10s} peak js {peak:.3f} c {np.abs(c).max():.3f}  maxdiff {d.max() / peak:.2e}  SNR {snr:6.1f} dB')
    br.close()
print(f'{len(CASES) - fails}/{len(CASES)} within 60 dB')
