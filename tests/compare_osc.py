"""Chrome OscillatorNode + PeriodicWave vs PeriodicOsc, on real TABLE frames."""
import subprocess, numpy as np, json, http.server, threading, functools, os
from playwright.sync_api import sync_playwright
D = '/tmp/mzwt'
coef = np.fromfile(f'{D}/js.bin', dtype=np.float32).reshape(134, 6, -1)
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=D); h.log_message = lambda *a: None
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 8806), h); threading.Thread(target=srv.serve_forever, daemon=True).start()
open(f'{D}/osc.html', 'w').write("""<!doctype html><script>
window.render = async (re, im, H, f, det, fmHz, fmD) => { const ctx = new OfflineAudioContext(1, 24000, 48000);
 const w = ctx.createPeriodicWave(new Float32Array(re.slice(0, H + 1)), new Float32Array(im.slice(0, H + 1)));
 const o = ctx.createOscillator(); o.setPeriodicWave(w); o.frequency.value = f; o.detune.value = det;
 const m = ctx.createOscillator(); m.frequency.value = fmHz; const g = ctx.createGain(); g.gain.value = fmD; m.connect(g); g.connect(o.frequency);
 o.connect(ctx.destination); o.start(0); m.start(0); const r = await ctx.startRendering(); return Array.from(r.getChannelData(0)); };
</script>""")
cases = [(16, 512, 110, 0, 0, 0), (16, 512, 440, 14, 0, 0), (16, 64, 2500, -7, 0, 0), (60, 512, 3.3, 0, 0, 0), (60, 512, 6, 0, 0, 0), (100, 512, 55, 0, 3, 40), (5, 128, 7000, 0, 0, 0), (16, 512, 1200, -23, 0, 0), (30, 32, 20000, 0, 0, 0)]
with sync_playwright() as p:
    br = p.chromium.launch(); pg = br.new_page(); pg.goto('http://127.0.0.1:8806/osc.html')
    for ti, H, f, det, fmHz, fmD in cases:
        re = coef[ti, 0, 4096:4096 + 513]; im = coef[ti, 0, 4096 + 513:]
        np.concatenate([re, im]).astype(np.float32).tofile(f'{D}/co.bin')
        js = np.array(pg.evaluate('([a,b,c,d,e,f,g]) => render(a,b,c,d,e,f,g)', [re.tolist(), im.tolist(), H, f, det, fmHz, fmD]), dtype=np.float32)
        subprocess.run(['/tmp/osc_part', f'{D}/co.bin', str(H), str(f), str(det), str(fmHz), str(fmD), f'{D}/o.raw'], check=True)
        c = np.fromfile(f'{D}/o.raw', dtype=np.float32); d = np.abs(js - c)
        snr = 10 * np.log10((js ** 2).sum() / max(1e-30, ((js - c) ** 2).sum()))
        print(f'table {ti:3d} H {H:3d} f {f:6} det {det:3} fm {fmHz}/{fmD}:  maxdiff {d.max():.2e}  SNR {snr:6.1f} dB')
    br.close()
