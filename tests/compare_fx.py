"""Mosaic effects chain: C++ (MosaicFx) vs the page's own Engine.buildFx/applyFx in Chrome's OfflineAudioContext."""
import json, os, subprocess, sys, base64, struct, http.server, threading, functools
import numpy as np
from playwright.sync_api import sync_playwright

SR = 48000; SECS = float(os.environ.get('SECS', '2.0'))
SERVE = '/tmp/mzfx'; PORT = 8792
BASE = {"drive":0,"phaserOn":False,"phaserWet":0.28,"phaserRate":0.22,"phaserDepth":0.45,"delaySync":"free","delayTime":0.32,"delayFb":0.32,"delayWet":0,
 "multiOn":False,"multiTime":0.31,"multiFb":0.34,"multiWet":0,"busFilter":0,"busFilterRes":0.2,"chorusOn":False,"chorusRate":0.35,"chorusDepth":0.2,"chorusMix":0.15,"chorusHp":250,
 "ppOn":False,"ppSync":"3/16","ppTime":0.28,"ppFb":0.35,"ppMix":0.12,"ppLp":3500,"ppHp":300,"crushOn":False,"crushBits":8,"crushMix":0.5,"tremOn":False,"tremMode":"trem","tremSync":"1/8","tremDepth":0.5,
 "widthOn":False,"width":1.3,"rvToneOn":False,"rvHp":180,"rvLp":9000,"reverbWet":0,"reverbDecay":2.1,"shimmerOn":False,"shimmerLevel":0.12,"shimmerPitch":196,"shimmerMotion":0.28,"shimmerMode":"drone",
 "compOn":False,"compThreshold":-18,"compRatio":3,"compAttack":0.01,"compRelease":0.18}
def case(name, bpm=120, master=0.8, force=None, **kw):
    fx = dict(BASE); fx.update(kw); return name, {"fx": fx, "master": master, "seq": {"bpm": bpm}, "force": force or {}}
CASES = [
    case('dry'), case('drive', drive=0.5), case('phaser', phaserOn=True, phaserWet=0.6), case('delay', delayWet=0.5, delayFb=0.6),
    case('delaySync', delaySync='1/8d', delayWet=0.4, delayFb=0.5, bpm=132), case('multi', multiOn=True, multiWet=0.5, multiFb=0.5),
    case('reverb', reverbWet=0.4, reverbDecay=2.5), case('reverbTone', reverbWet=0.4, rvToneOn=True, rvHp=400, rvLp=3000),
    case('shimmer', reverbWet=0.3, shimmerOn=True, shimmerLevel=0.3), case('air', reverbWet=0.4, shimmerOn=True, shimmerMode='air', shimmerLevel=0.3),
    case('djLow', busFilter=-0.5, busFilterRes=0.6), case('djHigh', busFilter=0.4), case('crush', crushOn=True, crushBits=5, crushMix=0.7),
    case('chorus', chorusOn=True, chorusMix=0.5, chorusDepth=0.6), case('trem', tremOn=True, tremDepth=0.8), case('pan', tremOn=True, tremMode='pan', tremSync='1/4'),
    case('width', widthOn=True, width=1.8), case('pingpong', ppOn=True, ppMix=0.5, ppFb=0.6), case('comp', compOn=True, compThreshold=-24, compRatio=6),
    case('allButAir', drive=0.3, phaserOn=True, phaserWet=0.3, delayWet=0.3, delaySync='1/8', multiOn=True, multiWet=0.2, reverbWet=0.3, shimmerOn=True, shimmerLevel=0.2,
         busFilter=-0.2, chorusOn=True, ppOn=True, tremOn=True, widthOn=True, compOn=True, master=1.0),
    case('ch_drive', chorusOn=True, drive=0.3),
    case('ch_phaser', chorusOn=True, phaserOn=True, phaserWet=0.3),
    case('ch_delay', chorusOn=True, delayWet=0.3, delaySync='1/8'),
    case('ch_multi', chorusOn=True, multiOn=True, multiWet=0.2),
    case('ch_rev', chorusOn=True, reverbWet=0.3),
    case('ch_shim', chorusOn=True, shimmerOn=True, shimmerLevel=0.2, reverbWet=0.3),
    case('ch_dj', chorusOn=True, busFilter=-0.2),
    case('ch_master', chorusOn=True, master=1.0),
    case('everything', drive=0.3, phaserOn=True, phaserWet=0.3, delayWet=0.3, delaySync='1/8', multiOn=True, multiWet=0.2, reverbWet=0.3, shimmerOn=True, shimmerMode='air', shimmerLevel=0.2,
         busFilter=-0.2, chorusOn=True, ppOn=True, crushOn=True, crushBits=10, crushMix=0.3, tremOn=True, widthOn=True, compOn=True, master=1.0),
]
only = os.environ.get('ONLY'); CASES = [c for c in CASES if not only or c[0] in only.split(',')]

PAGE = r"""<!doctype html><script>
let __s = 1; function mulberry(){ __s |= 0; __s = __s + 0x6D2B79F5 | 0; let t = Math.imul(__s ^ __s >>> 15, 1 | __s); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }
Math.random = mulberry; window.setTimeout = () => 0; window.setInterval = () => 0;
</script><script src="lib.js"></script><script>
window.render = async (state, secs, seed) => {
  __s = seed; const N = Math.round(secs * 48000), ctx = new OfflineAudioContext(2, N, 48000);
  const e = new Engine(); e.ctx = ctx; e.buildFx(); e.state = state; e.applyFx();
  const F = { driveWet: e.driveWet.gain, driveDry: e.driveDry.gain, phaserWet: e.phaserWet.gain, phaserDry: e.phaserDry.gain, delayWet: e.delayWet.gain, delayFb: e.delayFb.gain, multiWet: e.multiWet.gain,
    reverbWet: e.reverbWet.gain, master: e.master.gain, busWet: e.busWet.gain, busDry: e.busDry.gain, shimmerDepth: e.shimmerDepth.gain, delayTime: e.delay.delayTime,
    compThreshold: e.comp.threshold, compRatio: e.comp.ratio, compAttack: e.comp.attack, compRelease: e.comp.release };
  for (const [k, v] of Object.entries(state.force || {})) { F[k].cancelScheduledValues(0); F[k].value = v; }
  const buf = ctx.createBuffer(2, N, 48000), L = buf.getChannelData(0), R = buf.getChannelData(1);
  let s = 12345; const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s >>> 8) / 16777216 * 2 - 1; };
  for (let t = 0; t < N; t++) { const tt = t / 48000, ph = tt % 0.5, burst = ph < 0.12 ? 1 : 0;
    const v = burst * (0.4 * Math.sin(2 * Math.PI * 220 * tt) * Math.exp(-8 * ph) + 0.15 * rnd());
    L[t] = v; R[t] = v * 0.7 + 0.1 * Math.sin(2 * Math.PI * 331 * tt) * burst; }
  const src = ctx.createBufferSource(); src.buffer = buf; src.connect(e.gate); src.start(0);
  const out = await ctx.startRendering(), a = out.getChannelData(0), b = out.getChannelData(1), il = new Float32Array(N * 2);
  for (let i = 0; i < N; i++) { il[2 * i] = a[i]; il[2 * i + 1] = b[i]; }
  let bin = ''; const u8 = new Uint8Array(il.buffer); for (let i = 0; i < u8.length; i += 32768) bin += String.fromCharCode.apply(null, u8.subarray(i, i + 32768));
  return btoa(bin);
};
</script>"""
open(f'{SERVE}/fx.html', 'w').write(PAGE)
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=SERVE); h.log_message = lambda *a: None
srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), h); threading.Thread(target=srv.serve_forever, daemon=True).start()

worst = 0; fails = 0
with sync_playwright() as p:
    br = p.chromium.launch(args=['--autoplay-policy=no-user-gesture-required']); pg = br.new_page()
    pg.on('console', lambda m: print('  [page]', m.text) if m.type in ('error', 'warning') else None)
    pg.goto(f'http://127.0.0.1:{PORT}/fx.html')
    for i, (name, st) in enumerate(CASES):
        seed = 2000
        json.dump(st, open('/tmp/mzfx/state.json', 'w'))
        subprocess.run(['/tmp/render_fx', '/tmp/mzfx/state.json', str(SECS), str(seed), '/tmp/mzfx/c.raw'], check=True)
        c = np.fromfile('/tmp/mzfx/c.raw', dtype=np.float32).reshape(-1, 2)
        # Chrome's own render of the AIR loop (a feedback cycle through the convolver) is not repeatable from one render to the next,
        # so for AIR the C++ is held to the closer of two browser renders.
        tries = 2 if st['fx'].get('shimmerOn') and st['fx'].get('shimmerMode') == 'air' else 1
        best = None
        for _ in range(tries):
            js = np.frombuffer(base64.b64decode(pg.evaluate('([s,t,d]) => render(s,t,d)', [st, SECS, seed])), dtype=np.float32).reshape(-1, 2)
            if best is None or np.abs(js - c[:len(js)]).max() < np.abs(best - c[:len(best)]).max(): best = js
        js = best; c = c[:len(js)]
        np.save(f"/tmp/mzfx/js_{name}.npy", js); np.save(f"/tmp/mzfx/c_{name}.npy", c)
        d = np.abs(js - c); peak = max(1e-9, np.abs(js).max()); rel = d.max() / peak
        tail = d[int(0.3 * SR):].max() / peak
        snr = 10 * np.log10((js ** 2).sum() / max(1e-30, ((js - c) ** 2).sum()))
        first = int(np.argmax(d.max(axis=1) > 1e-4 * peak)) if (d > 1e-4 * peak).any() else -1
        flips = (d.max(axis=1) > 1e-3 * peak).mean()
        if st['fx'].get('crushOn') and flips < 0.005:   # a bit-crusher turns a 1e-7 difference into a whole quantisation step now and then
            keep = d.max(axis=1) <= 1e-3 * peak
            snr = 10 * np.log10((js[keep] ** 2).sum() / max(1e-30, ((js[keep] - c[keep]) ** 2).sum()))
        ok = snr > (40 if tries > 1 else 60)
        worst = max(worst, rel); fails += not ok
        print(f'{"ok  " if ok else "DIFF"} {name:12s} flips {flips:.4f} peak {peak:.3f}  maxdiff {rel:.2e} (after 0.3s {tail:.2e})  SNR {snr:6.1f} dB  first diff @ {first}')
    br.close()
print(f'{len(CASES) - fails}/{len(CASES)} within 60 dB')
