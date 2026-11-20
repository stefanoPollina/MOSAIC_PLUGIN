"""Parity test: the C++ voice engine vs Mosaic's own 'mosaic-voice' worklet, same seed, same state, same timed events.
Needs Chrome + Playwright, a server with mosaic-voice.js (MOSAIC_WEB), the compiled render_voice (RENDER_VOICE), and states (STATES json: name -> state JSON)."""
import asyncio, json, os, struct, subprocess, sys
from playwright.async_api import async_playwright
SERVER = os.environ.get('MOSAIC_WEB', 'http://localhost:8791'); RENDER = os.environ.get('RENDER_VOICE', '/tmp/render_voice')
STATES = json.load(open(os.environ.get('STATES', '/tmp/mz_states.json')))
JS = """async ([state, events, seed, secs]) => {
  let src = await (await fetch('/mosaic-voice.js')).text();
  src = `let __s=${seed}; Math.random=function(){__s|=0;__s=__s+0x6D2B79F5|0;let t=Math.imul(__s^__s>>>15,1|__s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};` + src;
  const ctx = new OfflineAudioContext(2, 48000*secs, 48000);
  await ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src],{type:'text/javascript'})));
  const n = new AudioWorkletNode(ctx,'mosaic-voice',{numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2]}); n.connect(ctx.destination);
  n.port.postMessage({type:'state', state});
  for (const e of events) n.port.postMessage(e);
  await new Promise(r=>setTimeout(r,80));
  const b = await ctx.startRendering(); const L=b.getChannelData(0), R=b.getChannelData(1), o=new Array(L.length*2);
  for (let i=0;i<L.length;i++){o[2*i]=L[i];o[2*i+1]=R[i];} return o;
}"""
def ev_lines(evs):
  out = []
  for e in evs:
    t = e.get('time', 0)
    if e['type'] == 'on': out.append(f"on {e['id']} {e['note']} {e.get('vel',100)} {e.get('layer','A')} {e.get('glide',0)} {1 if e.get('legato') else 0} {t}")
    elif e['type'] == 'off': out.append(f"off {e['id']} {t}")
    elif e['type'] in ('bend','mod'): out.append(f"{e['type']} {e['value']} {t}")
    elif e['type'] == 'sustain': out.append(f"sustain {1 if e['on'] else 0} {t}")
  return '\n'.join(out)
def chord(notes, t0=0.01, dur=1.2, layer='A', vel=100):
  ev = []
  for k, n in enumerate(notes): ev.append({'type':'on','id':f'n{k}{layer}','note':n,'vel':vel,'layer':layer,'time':t0 + k*0.003})
  for k, n in enumerate(notes): ev.append({'type':'off','id':f'n{k}{layer}','time':t0 + dur})
  return ev
def melody():
  ev = []; t = 0.02
  for k, n in enumerate([48, 55, 60, 63, 67, 72, 70, 67]):
    ev.append({'type':'on','id':f'm{k}','note':n,'vel':70 + (k*13)%57,'layer':'A','glide':0.04 if k%3==0 else 0,'time':t}); ev.append({'type':'off','id':f'm{k}','time':t+0.18}); t += 0.21
  ev += [{'type':'bend','value':0.6,'time':0.5},{'type':'bend','value':0,'time':1.1},{'type':'mod','value':0.8,'time':0.3}]
  return ev
async def main():
  names = sys.argv[1:] or list(STATES)
  async with async_playwright() as p:
    b = await p.chromium.launch(executable_path='/usr/bin/google-chrome', args=['--autoplay-policy=no-user-gesture-required'])
    pg = await b.new_page(); await pg.goto(SERVER + '/index.html')
    worst = 0; bad = []
    for i, name in enumerate(names):
      state = json.loads(STATES[name]); seed = 1000 + i; secs = 2.2
      evs = (chord([48, 60, 64, 67]) + chord([55], 0.4, 0.9, 'B')) if i % 2 == 0 else melody()
      js = await pg.evaluate(JS, [state, evs, seed, secs])
      open('/tmp/v_state.json','w').write(json.dumps(state)); open('/tmp/v_ev.txt','w').write(ev_lines(evs))
      subprocess.run([RENDER, '/tmp/v_state.json', '/tmp/v_ev.txt', str(secs), str(seed), '/tmp/v_out.raw'], check=True)
      raw = open('/tmp/v_out.raw','rb').read(); cpp = struct.unpack(f'{len(raw)//4}f', raw)
      d = max(abs(a - c) for a, c in zip(js, cpp)); pk = max(abs(a) for a in js) or 1e-9
      first = next((k for k, (a, c) in enumerate(zip(js, cpp)) if abs(a - c) > 1e-4), None)
      worst = max(worst, d)
      tag = 'OK  ' if d < 1e-4 else 'DIFF'
      if d >= 1e-4: bad.append(name)
      print(f'{tag} {name[:34]:34} peak {pk:6.3f}  max diff {d:.2e}' + (f'  first at sample {first//2} ({"L" if first%2==0 else "R"})' if first is not None else ''))
    print(f'\n{len(names)-len(bad)}/{len(names)} identical (worst {worst:.2e})'); await b.close()
asyncio.run(main())
