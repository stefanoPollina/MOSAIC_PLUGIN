/* Mosaic VST3 bridge. Runs before Mosaic's own scripts when the page is inside the plugin.
   Mosaic keeps doing everything it does in a browser (presets, banks, sequencer, BANK, automation, the TABLE editor),
   but its sound is made by the plugin: the Engine below sends Mosaic's audio messages to the C++ engines and shows
   the plugin's clock, meters and output in place of Web Audio's. */
(function () {
  'use strict';
  const J = window.__JUCE__ && window.__JUCE__.backend;
  if (!J) return;                                   // an ordinary browser: Mosaic runs as it always has
  const MP = window.MosaicPlugin = { active: true };

  /* ---------- the project's saved sound: Mosaic starts from it (it reads its last state at start-up) ---------- */
  try {
    const x = new XMLHttpRequest(); x.open('GET', '/__mosaic_state', false); x.send();
    if (x.status === 200 && x.responseText && x.responseText.length > 2) localStorage.setItem('continuum-mosaic-last-state-v2', x.responseText);
  } catch (e) { /* no saved state: Mosaic starts as it would in a browser */ }
  // MIDI comes from the host (sample-accurate), not from the page's own Web MIDI
  try { Object.defineProperty(navigator, 'requestMIDIAccess', { value: undefined, configurable: true }); } catch (e) { /* read-only: harmless */ }
  // downloads (WAV, MIDI, presets, banks) become save dialogs
  const realClick = HTMLAnchorElement.prototype.click;
  HTMLAnchorElement.prototype.click = function () {
    if (this.download && /^(blob|data):/.test(this.href)) {
      const name = this.download;
      fetch(this.href).then((r) => r.arrayBuffer()).then((buf) => {
        const u8 = new Uint8Array(buf); let bin = ''; for (let i = 0; i < u8.length; i += 32768) bin += String.fromCharCode.apply(null, u8.subarray(i, i + 32768));
        J.emitEvent('save', { name, data: btoa(bin) });
      });
      return;
    }
    return realClick.call(this);
  };

  /* ---------- the plugin's audio clock ---------- */
  let clk = { t: 0, sr: 48000, offline: false, wall: performance.now() };
  J.addEventListener('clock', (c) => { clk = Object.assign({}, c, { wall: performance.now() }); });
  const now = () => clk.offline ? clk.t : clk.t + Math.min(0.25, (performance.now() - clk.wall) / 1000);

  /* ---------- messages to the plugin, batched per task ---------- */
  let q = [];
  const flush = () => { if (!q.length) return; const list = q; q = []; J.emitEvent('msg', { list }); };
  const send = (m) => { q.push(m); if (q.length === 1) Promise.resolve().then(flush); };
  MP.send = send;

  /* ---------- stand-ins for Web Audio objects Mosaic touches but no longer needs ---------- */
  const param = () => ({ value: 0, defaultValue: 0, setValueAtTime() { return this; }, linearRampToValueAtTime() { return this; }, exponentialRampToValueAtTime() { return this; },
    setTargetAtTime() { return this; }, setValueCurveAtTime() { return this; }, cancelScheduledValues() { return this; }, cancelAndHoldAtTime() { return this; } });
  const PARAMS = new Set(['gain', 'frequency', 'detune', 'Q', 'pan', 'delayTime', 'offset', 'threshold', 'knee', 'ratio', 'attack', 'release', 'playbackRate', 'reduction']);
  function dummy() {
    return new Proxy({ numberOfOutputs: 1, numberOfInputs: 1, channelCount: 2 }, {
      get(t, k) {
        if (k in t) return t[k];
        if (k === 'connect') return (x) => x;
        if (k === 'disconnect' || k === 'start' || k === 'stop' || k === 'setPeriodicWave' || k === 'addEventListener' || k === 'removeEventListener') return () => {};
        if (PARAMS.has(k)) return (t[k] = param());
        if (k === 'port') return (t[k] = { postMessage() {}, onmessage: null, start() {}, close() {} });
        if (k === 'curve' || k === 'buffer' || k === 'onended') return null;
        return undefined;
      },
      set(t, k, v) { t[k] = v; return true; }
    });
  }

  /* ---------- the plugin's output, for Mosaic's scopes, meters and spectrum ---------- */
  const RING = 32768, ring = [new Float32Array(RING), new Float32Array(RING)]; let ringW = 0;
  J.addEventListener('audio', (a) => {
    const bin = atob(a.data), n = bin.length >> 2, u8 = new Uint8Array(n * 4);
    for (let i = 0; i < u8.length; i++) u8[i] = bin.charCodeAt(i);
    const f = new Float32Array(u8.buffer);
    for (let i = 0; i + 1 < n; i += 2) { ring[0][ringW] = f[i]; ring[1][ringW] = f[i + 1]; ringW = (ringW + 1) % RING; }
  });
  const latest = (ch, out, n) => {
    for (let i = 0; i < n; i++) {
      const k = (ringW - n + i + RING * 2) % RING;
      out[i] = ch === 0 ? ring[0][k] : ch === 1 ? ring[1][k] : 0.5 * (ring[0][k] + ring[1][k]);
    }
  };
  // a small real FFT for the analyser's frequency data (Blackman window and smoothing, as the AnalyserNode)
  function fftMag(x) {
    const n = x.length, re = Float64Array.from(x), im = new Float64Array(n);
    for (let i = 1, j = 0; i < n; i++) { let b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { const t = re[i]; re[i] = re[j]; re[j] = t; } }
    for (let len = 2; len <= n; len <<= 1) { const a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a), h = len >> 1;
      for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let k = 0; k < h; k++) { const p = i + k, qq = p + h, xr = re[qq] * cr - im[qq] * ci, xi = re[qq] * ci + im[qq] * cr;
        re[qq] = re[p] - xr; im[qq] = im[p] - xi; re[p] += xr; im[p] += xi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } } }
    const m = new Float64Array(n / 2); for (let k = 0; k < n / 2; k++) m[k] = Math.hypot(re[k], im[k]) / n; return m;
  }
  class FakeAnalyser {
    constructor() { this.fftSize = 2048; this.smoothingTimeConstant = 0.8; this.minDecibels = -100; this.maxDecibels = -30; this.ch = -1; this.prev = null; this.channelCount = 2; }
    get frequencyBinCount() { return this.fftSize / 2; }
    connect(x) { return x; } disconnect() {}
    getFloatTimeDomainData(a) { latest(this.ch, a, Math.min(a.length, this.fftSize)); }
    getByteTimeDomainData(a) { const t = new Float32Array(Math.min(a.length, this.fftSize)); latest(this.ch, t, t.length); for (let i = 0; i < t.length; i++) a[i] = Math.max(0, Math.min(255, Math.floor(128 * (1 + t[i])))); }
    _spectrum() {
      const n = this.fftSize, t = new Float32Array(n); latest(this.ch, t, n);
      for (let i = 0; i < n; i++) { const x = i / n; t[i] *= 0.42 - 0.5 * Math.cos(2 * Math.PI * x) + 0.08 * Math.cos(4 * Math.PI * x); }
      const m = fftMag(t), s = this.smoothingTimeConstant;
      if (!this.prev || this.prev.length !== m.length) this.prev = new Float64Array(m.length);
      for (let k = 0; k < m.length; k++) this.prev[k] = s * this.prev[k] + (1 - s) * m[k];
      return this.prev;
    }
    getFloatFrequencyData(a) { const m = this._spectrum(); for (let k = 0; k < a.length && k < m.length; k++) a[k] = m[k] > 0 ? 20 * Math.log10(m[k]) : -Infinity; }
    getByteFrequencyData(a) { const m = this._spectrum(), lo = this.minDecibels, hi = this.maxDecibels;
      for (let k = 0; k < a.length && k < m.length; k++) { const db = m[k] > 0 ? 20 * Math.log10(m[k]) : -1000; a[k] = Math.max(0, Math.min(255, Math.floor(255 / (hi - lo) * (db - lo)))); } }
  }
  const splitter = () => ({ connect(an, out) { if (an instanceof FakeAnalyser) an.ch = out | 0; return an; }, disconnect() {} });
  const master = Object.assign(dummy(), {});
  master.connect = (x) => { if (x instanceof FakeAnalyser) x.ch = -1; return x; };

  /* ---------- the recorder's capture node: the plugin records its own output ---------- */
  const recNodes = new Set();
  J.addEventListener('rec', (m) => {
    if (m.type === 'chunk') { const dec = (s) => { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return new Float32Array(u.buffer); }; m = { type: 'chunk', a: dec(m.a), b: dec(m.b) }; }
    for (const n of recNodes) if (n.port.onmessage) n.port.onmessage({ data: m });
  });
  const RealWorkletNode = window.AudioWorkletNode;
  window.AudioWorkletNode = function (ctx, name, opts) {
    if (ctx !== fakeCtx) return new RealWorkletNode(ctx, name, opts);
    const node = Object.assign(dummy(), { port: { onmessage: null, postMessage: (m) => send({ type: 'rec', msg: m }), start() {}, close() {} } });
    recNodes.add(node); return node;
  };

  /* ---------- the audio context Mosaic sees ---------- */
  const fakeCtx = {
    get currentTime() { return now(); }, get sampleRate() { return clk.sr; }, state: 'running', baseLatency: 0, outputLatency: 0,
    destination: dummy(), audioWorklet: { addModule: () => Promise.resolve() },
    resume: () => Promise.resolve(), suspend: () => Promise.resolve(), close: () => Promise.resolve(),
    createAnalyser: () => new FakeAnalyser(), createChannelSplitter: splitter, createChannelMerger: dummy, createGain: dummy, createBiquadFilter: dummy,
    createDelay: dummy, createOscillator: dummy, createConstantSource: dummy, createWaveShaper: dummy, createConvolver: dummy, createStereoPanner: dummy,
    createDynamicsCompressor: dummy, createBufferSource: dummy, createMediaStreamSource: dummy, createMediaStreamDestination: dummy, createPeriodicWave: () => ({}),
    createBuffer: (ch, len, sr) => { const d = Array.from({ length: ch }, () => new Float32Array(len)); return { numberOfChannels: ch, length: len, sampleRate: sr, duration: len / sr, getChannelData: (i) => d[i], copyToChannel: (src, i) => d[i].set(src), copyFromChannel: (dst, i) => dst.set(d[i].subarray(0, dst.length)) }; }
  };
  MP.ctx = fakeCtx;

  /* ---------- Mosaic's Engine, playing through the plugin ---------- */
  MP.makeEngine = (Base) => {
    class PluginEngine extends Base {
      constructor() { super(); this.ctx = fakeCtx; this.audioRenderer = 'worklet'; }
      get renderedUntil() { return now(); } set renderedUntil(v) {}
      ensure(state) {
        if (!this.ready) {
          this.gate = dummy(); this.master = master; this.analyser = new FakeAnalyser();
          this.node = { port: { postMessage: (m) => send(m), onmessage: null }, connect() {}, disconnect() {} };
          this.ready = true;
          if (state) this.setState(state);
        }
        return Promise.resolve();
      }
      unlock() {} boot() { return Promise.resolve(); } installProcessor() { return Promise.resolve(); } buildFx() {}
      gateFx() {} setReverbDecay() {} rampDrive() {} applyBus() {}
      setState(s) {
        if (!s) return; this.state = s;
        try { if (window.MosaicWT) window.MosaicWT.hooks.state(this, s); } catch (e) { /* the TABLE editor only */ }
        if (!this.ready) return;
        const L = s.layers || {};
        send({ type: 'state', slim: this.slimState(s), fx: s.fx || {}, master: s.master, bpm: (s.seq && s.seq.bpm) || 120, name: s.name || '', wtLab: s.wtLab || null,
          aOn: !(L.A && L.A.enabled === false), bOn: !(L.B && L.B.enabled === false), polyphony: s.polyphony || 8, glide: s.glide || 0 });
        J.emitEvent('ui', { state: JSON.stringify(s) });
      }
      applyFx() { if (this.state) this.setState(this.state); }
      // TABLE voices are played by the plugin from these same messages
      noteOn(id, note, vel = 100, opts = {}) { if (!this.node) return; send({ type: 'on', id, note, vel, layer: opts.layer || 'A', glide: opts.glide || 0, legato: !!opts.legato, time: opts.time || now() }); }
      noteOff(id, time) { if (this.node) send({ type: 'off', id, time: time || now() }); }
      bend(value) { if (this.node) send({ type: 'bend', value, time: now() }); }
      mod(value) { if (this.node) send({ type: 'mod', value, time: now() }); }
      sustain(on) { if (this.node) send({ type: 'sustain', on: !!on, time: now() }); }
      panic() { if (this.node) send({ type: 'panic' }); this.setGate(1); }
      setGate(value, time) { send({ type: 'gate', value: Math.max(0, Math.min(1, value)), time: time == null ? now() : Math.max(time, now()), cancel: time == null }); }
    }
    const e = new PluginEngine();
    J.addEventListener('meter', (m) => { if (e.onMeter) e.onMeter({ voices: m.voices, cpu: m.cpu, scope: Float32Array.from(m.scope || []) }); });
    MP.engine = e;
    return e;
  };

  /* ---------- host MIDI the plugin hands over (only while BANK's arpeggiator plays the notes) ---------- */
  const arpOn = () => { try { const b = window.MosaicBank && window.MosaicBank.bank; return !!(b && b.arp && b.arp.active); } catch (e) { return false; } };
  J.addEventListener('midi', (m) => {
    for (const d of m.list || []) {
      try { if (arpOn()) { if (MP.onMidi) MP.onMidi(d); } else if (MP.observeMidi) MP.observeMidi(d); } catch (e) { console.error(e); }
    }
  });
  let lastArp = false;
  setInterval(() => { const a = arpOn(); if (a !== lastArp) { lastArp = a; send({ type: 'arp', on: a }); } }, 100);

  /* ---------- the DAW's transport: tempo, play and stop, and offline renders ---------- */
  let hostPlaying = false;
  J.addEventListener('transport', (t) => {
    const st = window.MosaicBank && window.MosaicBank.getState(), seq = window.__mosaicSequencer, e = MP.engine;
    if (!st || !e) return;
    if (t.hasTempo && t.bpm > 0 && Math.abs((st.seq.bpm || 120) - t.bpm) > 1e-6) { st.seq.bpm = t.bpm; e.setState(st); if (MP.onTempo) MP.onTempo(t.bpm); }
    if (!seq || !t.hasTransport) return;
    if (t.playing && !hostPlaying) {
      // start on the DAW's step grid: the next step boundary after the play position
      const div = Number(st.seq.division || 0.25), beat = 60 / (st.seq.bpm || 120), pos = Math.max(0, t.ppq), k = Math.ceil(pos / div - 1e-9);
      seq.start();
      setTimeout(() => {
        seq.next = t.time + (k * div - pos) * beat; seq.step = ((k % st.seq.length) + st.seq.length) % st.seq.length;
        e.l8anchor = seq.next - seq.step * div * beat; send({ type: 'clock', anchor: e.l8anchor });
      }, 0);
    } else if (!t.playing && hostPlaying) seq.stop();
    hostPlaying = t.playing;
  });
  // offline (faster than real time) render: the plugin waits until the sequencer has scheduled up to `until`
  J.addEventListener('pump', (p) => {
    clk = Object.assign({}, clk, { t: p.now, offline: true, wall: performance.now() });
    try { const e = MP.engine; if (e && e.beforeRender) e.beforeRender(p.until); } catch (err) { console.error(err); }
    flush(); J.emitEvent('pumped', { until: p.until });
  });

  /* ---------- start audio without the START click ---------- */
  const autoStart = () => {
    const b = [...document.querySelectorAll('button')].find((x) => x.textContent.trim() === 'START' && x.offsetParent !== null);
    if (b) { b.click(); J.emitEvent('ready', {}); return; }
    setTimeout(autoStart, 200);
  };
  window.addEventListener('load', () => setTimeout(autoStart, 50));
})();
