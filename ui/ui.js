/* =========================================================================
   TAPE–TOP · UI prototype
   -------------------------------------------------------------------------
   This is a *feel* prototype, not the plugin. It has three jobs:
     1. show the layout and the design language on real proportions;
     2. play the four engine renders from dsp/out so the tape can be heard
        while looking at the machine that (will) make it;
     3. exercise the transport vocabulary (varispeed, break, reverse, chop,
        loop, memo) so we can decide what the plugin's panel needs.
   The audio is a rendered mix, not eight live tracks: there is no recording
   here yet. Everything else is real state.
   ========================================================================= */

(() => {
  'use strict';

  // ---------------------------------------------------------------- config
  const TRACKS = 8;
  const BAR_SECONDS = 2.5;              // 96 BPM, 4/4

  const STYLES = [
    { id: 'studio',   name: 'STUDIO',   sub: '4-TRACK',  color: '#3fb6e8', file: 'audio/01-studio-15ips.wav',
      ips: 15,   gap: 1.2, bump: 42, hiss: -86, wow: 0.04, drive: -2.0,
      note: '15 IPS · wide track · no wobble' },
    { id: 'vintage',  name: 'VINTAGE',  sub: '4-TRACK',  color: '#e2622a', file: 'audio/02-vintage-7.5ips.wav',
      ips: 7.5,  gap: 2.2, bump: 62, hiss: -68, wow: 0.12, drive: 1.5,
      note: '7.5 IPS · head bump · a breath of wobble' },
    { id: 'porta',    name: 'PORTA',    sub: 'CASSETTE', color: '#57c46a', file: 'audio/03-porta-3.75ips.wav',
      ips: 3.75, gap: 3.6, bump: 92, hiss: -52, wow: 0.35, drive: 5.0,
      note: '3.75 IPS · narrow track · real hiss' },
    { id: 'diskmini', name: 'DISK MINI', sub: 'MAGNETO-OPTICAL', color: '#8a6be0', file: 'audio/04-diskmini.wav',
      ips: 7.5,  gap: 2.6, bump: 70, hiss: -78, wow: 0.02, drive: 1.0,
      note: '15 kHz wall · psychoacoustic companding' },
    // Not a stock: the same sketch with no tape at all, so the difference can be
    // heard while looking at the machine that made it.
    { id: 'dry', name: 'DRY', sub: 'NO TAPE', color: '#e9e4d2', file: 'audio/00-dry-digital.wav',
      ips: 0, gap: 0, bump: 0, hiss: 0, wow: 0, drive: 0, dry: true,
      note: 'digital sum, no tape' },
  ];

  const TRICKS = [
    { id: 'loopin',   label: 'LOOP IN',  icon: 'M4 12h9M4 12l3-3M4 12l3 3M20 12h-4M20 12v-4' },
    { id: 'loopout',  label: 'LOOP OUT', icon: 'M20 12h-9M20 12l-3-3M20 12l-3 3M4 12h4M4 12v-4' },
    { id: 'loop',     label: 'LOOP',     icon: 'M5 12a7 7 0 0 1 12-4.9M19 12a7 7 0 0 1-12 4.9M17 3v4h-4M7 21v-4h4' },
    { id: 'break',    label: 'BREAK',    icon: 'M7 5h10M7 19h10M9 5v14M15 5v14' },
    { id: 'reverse',  label: 'REVERSE',  icon: 'M9 6L4 12l5 6M20 7v10M4 12h13' },
    { id: 'chop',     label: 'CHOP',     icon: 'M4 8h4v8H4zM10 8h4v8h-4zM16 8h4v8h-4z' },
    { id: 'memo1',    label: 'MEMO 1',   icon: 'M6 4h12v16l-6-4-6 4z' },
    { id: 'memo2',    label: 'MEMO 2',   icon: 'M6 4h12v16l-6-4-6 4zM9 9h6' },
  ];

  const LANE_COLORS = [
    '#e2622a', '#c9a227', '#57c46a', '#3fb6e8',
    '#8a6be0', '#d8452e', '#e9e4d2', '#57c46a',
  ];

  // ---------------------------------------------------------------- helpers
  const $ = (sel, root = document) => root.querySelector(sel);
  const el = (tag, cls, html) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (html !== undefined) n.innerHTML = html;
    return n;
  };
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const fmtTime = (s) => {
    s = Math.max(0, s);
    const m = Math.floor(s / 60);
    const sec = Math.floor(s % 60);
    const f = Math.floor((s % 1) * 25);
    return `${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}:${String(f).padStart(2, '0')}`;
  };

  const state = {
    styleId: 'vintage',
    style: null,
    duration: 11.17,
    pos: 0,
    playing: false,
    rate: 1,
    dir: 1,
    loop: { on: false, in: 0, out: 0 },
    chop: false,
    armed: new Set(),
    selectedTrack: 0,
    grid: 1,
    master: 1,
    memos: [null, null],
    levels: new Float32Array(TRACKS),
  };

  // --------------------------------------------------------------- audio
  const audio = {
    ctx: null,
    buffers: {},
    reversed: {},
    source: null,
    analyser: null,
    gain: null,
    data: null,
    stalled: false,
  };

  async function ensureAudio() {
    if (audio.ctx) return audio.ctx;
    const Ctx = window.AudioContext || window.webkitAudioContext;
    audio.ctx = new Ctx();
    audio.gain = audio.ctx.createGain();
    audio.analyser = audio.ctx.createAnalyser();
    audio.analyser.fftSize = 1024;
    audio.analyser.smoothingTimeConstant = 0.7;
    audio.data = new Uint8Array(audio.analyser.frequencyBinCount);
    audio.gain.connect(audio.analyser);
    audio.analyser.connect(audio.ctx.destination);
    return audio.ctx;
  }

  async function loadStyle(style) {
    if (audio.buffers[style.id]) {
      state.duration = audio.buffers[style.id].duration;
      return;
    }
    const ctx = await ensureAudio();
    const res = await fetch(style.file);
    const arr = await res.arrayBuffer();
    const buf = await ctx.decodeAudioData(arr);
    audio.buffers[style.id] = buf;

    // Reverse copy, so REVERSE is a real transport direction rather than a mute.
    const rev = ctx.createBuffer(buf.numberOfChannels, buf.length, buf.sampleRate);
    for (let c = 0; c < buf.numberOfChannels; c++) {
      const src = buf.getChannelData(c);
      const dst = rev.getChannelData(c);
      for (let i = 0, n = src.length; i < n; i++) dst[i] = src[n - 1 - i];
    }
    audio.reversed[style.id] = rev;
    state.duration = buf.duration;
    buildWaveforms(buf);
  }

  function currentBuffer() {
    const b = state.dir < 0 ? audio.reversed[state.styleId] : audio.buffers[state.styleId];
    return b || audio.buffers[state.styleId];
  }

  function stopSource() {
    if (audio.source) {
      try { audio.source.onended = null; audio.source.stop(); } catch (_) {}
      audio.source.disconnect();
      audio.source = null;
    }
  }

  function startSource(offset) {
    stopSource();
    if (!audio.ctx) return;
    const buf = currentBuffer();
    if (!buf) return;

    const src = audio.ctx.createBufferSource();
    src.buffer = buf;
    src.playbackRate.value = Math.max(0.05, state.rate);
    src.connect(audio.gain);

    let off = offset;
    if (state.dir < 0) off = buf.duration - offset;

    if (state.loop.on && state.loop.out > state.loop.in) {
      src.loop = true;
      if (state.dir > 0) {
        src.loopStart = state.loop.in;
        src.loopEnd = state.loop.out;
      } else {
        src.loopStart = Math.max(0, buf.duration - state.loop.out);
        src.loopEnd = Math.max(0.02, buf.duration - state.loop.in);
      }
      if (state.chop) {
        const len = Math.min(0.25, state.loop.out - state.loop.in);
        src.loopStart = state.dir > 0 ? state.loop.in : buf.duration - state.loop.in - len;
        src.loopEnd = src.loopStart + len;
      }
      if (off < src.loopStart || off > src.loopEnd) off = src.loopStart;
    }

    src.start(0, clamp(off, 0, buf.duration - 0.001));
    audio.source = src;
    audio.stalled = false;
  }

  function play() {
    if (state.playing) return;
    ensureAudio().then(() => {
      audio.ctx.resume();
      state.playing = true;
      startSource(state.pos);
      syncTransportUI();
    });
  }

  function pause() {
    state.playing = false;
    stopSource();
    syncTransportUI();
  }

  function togglePlay() { state.playing ? pause() : play(); }

  function seek(sec) {
    state.pos = clamp(sec, 0, state.duration);
    if (state.playing) startSource(state.pos);
  }

  // --------------------------------------------------------- waveform data
  const wave = { peaks: null, len: 0 };

  function buildWaveforms(buf) {
    // One real envelope for the printed mix; the eight lanes are renders of it
    // at different visual weights (the prototype has one mix, not eight stems).
    const n = 900;
    const ch = buf.getChannelData(0);
    const peaks = new Float32Array(n);
    const step = Math.floor(ch.length / n);
    for (let i = 0; i < n; i++) {
      let peak = 0;
      const start = i * step;
      for (let j = 0; j < step; j += 8) peak = Math.max(peak, Math.abs(ch[start + j] || 0));
      peaks[i] = peak;
    }
    wave.peaks = peaks;
    wave.len = n;
    laneCanvases.forEach((c, i) => drawLane(c, i));
  }

  // ------------------------------------------------------------- DOM build
  const laneCanvases = [];
  const laneNodes = [];
  const stripNodes = [];

  function buildChrome() {
    // barcode
    const svg = $('.barcode');
    let x = 0;
    let bars = '';
    let seed = 7;
    while (x < 118) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      const w = 1 + (seed % 3);
      bars += `<rect x="${x}" y="0" width="${w}" height="22" fill="rgba(233,228,210,.8)"/>`;
      x += w + 1 + ((seed >> 3) % 2);
    }
    svg.innerHTML = bars;
  }

  function buildStyles() {
    const host = $('#styles');
    STYLES.forEach((s) => {
      const b = el('button', 'style-chip');
      b.innerHTML = `<span class="dot"></span><span>${s.name}</span><span class="id">${s.sub}</span>`;
      b.style.color = s.color;
      b.dataset.id = s.id;
      b.addEventListener('click', () => setStyle(s.id));
      host.appendChild(b);
    });
  }

  function setStyle(id) {
    const style = STYLES.find((s) => s.id === id);
    if (!style) return;
    state.styleId = id;
    state.style = style;
    document.documentElement.style.setProperty('--accent', style.color);

    document.querySelectorAll('.style-chip').forEach((c) => c.classList.toggle('on', c.dataset.id === id));
    renderSpec();

    loadStyle(style).then(() => {
      state.pos = clamp(state.pos, 0, state.duration);
      if (state.playing) startSource(state.pos);
      laneCanvases.forEach((c, i) => drawLane(c, i));
    });
  }

  function renderSpec() {
    const s = state.style;
    if (s.dry) {
      $('#spec').innerHTML = `
        <dt>PATH</dt><dd>DIGITAL</dd>
        <dt>TAPE SPEED</dt><dd>—</dd>
        <dt>HEAD GAP</dt><dd>—</dd>
        <dt>HISS FLOOR</dt><dd>—</dd>
        <dt>WOW / FLUTTER</dt><dd>—</dd>
        <dt>RECORD DRIVE</dt><dd>—</dd>`;
      return;
    }
    const rows = [
      ['TAPE SPEED', `${s.ips.toFixed(2)} IPS`],
      ['HEAD GAP', `${s.gap.toFixed(1)} µm`],
      ['HEAD BUMP', `${s.bump} Hz`],
      ['HISS FLOOR', `${s.hiss} dB`],
      ['WOW / FLUTTER', `${s.wow.toFixed(2)} %`],
      ['RECORD DRIVE', `${s.drive > 0 ? '+' : ''}${s.drive.toFixed(1)} dB`],
    ];
    $('#spec').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  }

  function buildLanes() {
    const host = $('#lanes');
    for (let i = 0; i < TRACKS; i++) {
      const lane = el('div', 'lane');
      const id = el('div', 'lane-id', `<span class="arm"></span>T${i + 1}`);
      const canvasWrap = el('div', 'lane-canvas');
      const canvas = document.createElement('canvas');
      canvasWrap.appendChild(canvas);
      lane.appendChild(id);
      lane.appendChild(canvasWrap);

      lane.addEventListener('click', () => selectTrack(i));
      lane.querySelector('.lane-id').addEventListener('click', (e) => {
        e.stopPropagation();
        if (state.armed.has(i)) state.armed.delete(i); else state.armed.add(i);
        lane.classList.toggle('armed', state.armed.has(i));
      });

      host.appendChild(lane);
      laneNodes.push(lane);
      laneCanvases.push(canvas);
    }
    resizeLanes();
  }

  function resizeLanes() {
    laneCanvases.forEach((c) => {
      const r = c.getBoundingClientRect();
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      c.width = Math.max(1, Math.floor(r.width * dpr));
      c.height = Math.max(1, Math.floor(r.height * dpr));
    });
    laneCanvases.forEach((c, i) => drawLane(c, i));
  }

  function selectTrack(i) {
    state.selectedTrack = i;
    laneNodes.forEach((n, k) => n.classList.toggle('armed', state.armed.has(k)));
    stripNodes.forEach((n, k) => n.classList.toggle('sel', k === i));
  }

  function buildTricks() {
    const host = $('#tricks');
    TRICKS.forEach((t, i) => {
      const b = el('button', 'trick');
      b.dataset.id = t.id;
      b.innerHTML = `<span class="num">${i + 1}</span>
        <svg viewBox="0 0 24 24"><path d="${t.icon}"/></svg>
        <span>${t.label}</span>`;
      b.addEventListener('pointerdown', () => fireTrick(t.id, b, true));
      b.addEventListener('pointerup', () => fireTrick(t.id, b, false));
      b.addEventListener('pointerleave', () => fireTrick(t.id, b, false));
      host.appendChild(b);
    });
  }

  function fireTrick(id, node, down) {
    node.classList.add('flash');
    setTimeout(() => node.classList.remove('flash'), 300);

    switch (id) {
      case 'loopin':
        if (!down) return;
        state.loop.in = Math.min(state.pos, state.loop.out || state.pos + 1);
        updateLoopUI();
        break;
      case 'loopout':
        if (!down) return;
        state.loop.out = Math.max(state.pos, state.loop.in + 0.1);
        updateLoopUI();
        break;
      case 'loop':
        if (!down) return;
        state.loop.on = !state.loop.on;
        node.classList.toggle('on', state.loop.on);
        $('#loopToggle').classList.toggle('on', state.loop.on);
        if (state.playing) startSource(state.pos);
        updateLoopUI();
        break;
      case 'break':
        // Tape stop: the transport decelerates, the head loses flux and the
        // signal dies with the speed. Release spins it back up.
        setRateTarget(down ? 0 : 1, true);
        node.classList.toggle('on', down);
        break;
      case 'reverse':
        if (!down) return;
        state.dir *= -1;
        node.classList.toggle('on', state.dir < 0);
        if (state.playing) startSource(state.pos);
        updateReels();
        break;
      case 'chop':
        // Tempo-locked repeat: a short loop at the head, engaged from where the
        // tape currently is. Deliberately not the same thing as LOOP.
        if (!down) return;
        state.chop = !state.chop;
        node.classList.toggle('on', state.chop);
        if (state.chop) {
          state.loop.in = state.pos;
          state.loop.out = Math.min(state.duration, state.pos + BAR_SECONDS / 8);
          state.loop.on = true;
        } else {
          state.loop.on = false;
        }
        $('#loopToggle').classList.toggle('on', state.loop.on);
        if (state.playing) startSource(state.pos);
        updateLoopUI();
        break;
      case 'memo1':
      case 'memo2': {
        const slot = id === 'memo1' ? 0 : 1;
        if (down) {
          if (!state.memos[slot]) {
            state.memos[slot] = snapshot();
            node.classList.add('on');
          }
          recall(snapshot());
        } else {
          recall(state.memos[slot] || snapshot());
        }
        break;
      }
      default:
        break;
    }
  }

  function snapshot() {
    return {
      style: state.styleId,
      master: state.master,
      rate: state.rate,
      loop: { ...state.loop },
    };
  }

  function recall(snap) {
    if (!snap) return;
    state.master = snap.master;
    state.rate = snap.rate;
    state.loop = { ...snap.loop };
    $('#master').value = Math.round(snap.master * 100);
    $('#speed').value = Math.round(snap.rate * 100);
    $('#speedOut').textContent = `${snap.rate.toFixed(2)} ×`;
    if (snap.style !== state.styleId) setStyle(snap.style);
    updateLoopUI();
  }

  // ---------------------------------------------------------------- knobs
  function makeKnob({ label, value = 0.5, min = 0, max = 1, format = (v) => v.toFixed(2), color, onChange }) {
    const wrap = el('div', 'knob');
    const id = 'k' + Math.random().toString(36).slice(2, 8);
    wrap.innerHTML = `
      <svg viewBox="0 0 60 60" id="${id}">
        <circle class="body" cx="30" cy="30" r="17"/>
        <circle class="track" cx="30" cy="30" r="23" stroke-dasharray="1 5.4"/>
        <path class="arc" d=""/>
        <line class="pointer" x1="30" y1="30" x2="30" y2="15"/>
      </svg>
      <span class="lbl">${label}</span>
      <span class="val">${format(value)}</span>`;
    const svg = wrap.querySelector('svg');
    const arc = svg.querySelector('.arc');
    const pointer = svg.querySelector('.pointer');
    const valNode = wrap.querySelector('.val');
    if (color) arc.style.stroke = color;

    const START = -225, SWEEP = 270;
    const render = () => {
      const t = (value - min) / (max - min);
      const a = START + SWEEP * t;
      const rad = (a - 90) * Math.PI / 180;
      pointer.setAttribute('x2', 30 + Math.cos(rad) * 14);
      pointer.setAttribute('y2', 30 + Math.sin(rad) * 14);
      const r = 23;
      const a0 = (START - 90) * Math.PI / 180;
      const a1 = rad;
      const large = Math.abs(SWEEP * t) > 180 ? 1 : 0;
      arc.setAttribute('d', `M ${30 + Math.cos(a0) * r} ${30 + Math.sin(a0) * r} A ${r} ${r} 0 ${large} 1 ${30 + Math.cos(a1) * r} ${30 + Math.sin(a1) * r}`);
      valNode.textContent = format(value);
    };
    render();

    let dragging = false, startY = 0, startVal = value;
    svg.addEventListener('pointerdown', (e) => {
      dragging = true; startY = e.clientY; startVal = value;
      svg.setPointerCapture(e.pointerId);
    });
    svg.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const dy = startY - e.clientY;
      value = clamp(startVal + (dy / 140) * (max - min), min, max);
      render();
      onChange && onChange(value);
    });
    const end = () => { dragging = false; };
    svg.addEventListener('pointerup', end);
    svg.addEventListener('pointercancel', end);

    return wrap;
  }

  function buildMaster() {
    const host = $('#masterEq');
    const mk = (label, val, fmt, key) => makeKnob({
      label, value: val, min: -12, max: 12,
      format: (v) => `${v > 0 ? '+' : ''}${v.toFixed(1)}`,
      onChange: () => {},
    });
    host.appendChild(mk('LOW', 0));
    host.appendChild(mk('MID', 0));
    host.appendChild(mk('HIGH', 0));
  }

  function buildStrips() {
    const host = $('#strips');
    for (let i = 0; i < TRACKS; i++) {
      const s = el('div', 'strip');
      s.innerHTML = `
        <div class="strip-col">
          <span class="tid">T${i + 1}</span>
          <div class="barmini"><i></i></div>
        </div>`;
      const col = el('div', 'strip-col');
      col.appendChild(makeKnob({
        label: 'GAIN', value: 0.72, min: 0, max: 1,
        format: (v) => `${(v * 24 - 12).toFixed(1)}`,
        color: LANE_COLORS[i],
      }));
      s.appendChild(col);
      s.addEventListener('click', () => selectTrack(i));
      host.appendChild(s);
      stripNodes.push(s);
    }
  }

  // ---------------------------------------------------------- tape drawing
  function drawLane(canvas, i) {
    const ctx = canvas.getContext('2d');
    const dpr = canvas.width / Math.max(1, canvas.getBoundingClientRect().width || canvas.width);
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    const laneColor = LANE_COLORS[i];
    const dur = Math.max(1, state.duration);

    // grid
    ctx.strokeStyle = 'rgba(233,228,210,.10)';
    ctx.lineWidth = 1 * dpr;
    const step = state.grid;
    for (let t = 0; t <= dur; t += step) {
      const x = Math.round((t / dur) * w) + 0.5;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
      ctx.stroke();
    }

    // loop shading
    if (state.loop.out > state.loop.in) {
      const x0 = (state.loop.in / dur) * w;
      const x1 = (state.loop.out / dur) * w;
      ctx.fillStyle = state.loop.on ? 'rgba(226,98,42,.10)' : 'rgba(233,228,210,.04)';
      ctx.fillRect(x0, 0, x1 - x0, h);
      ctx.strokeStyle = state.loop.on ? 'rgba(226,98,42,.75)' : 'rgba(233,228,210,.28)';
      ctx.beginPath();
      ctx.moveTo(x0 + 0.5, 0); ctx.lineTo(x0 + 0.5, h);
      ctx.moveTo(x1 + 0.5, 0); ctx.lineTo(x1 + 0.5, h);
      ctx.stroke();
    }

    // takes: deterministic regions per lane
    const regions = takesFor(i, dur);
    regions.forEach((r) => {
      const x0 = (r.start / dur) * w;
      const x1 = (r.end / dur) * w;
      const yTop = h * 0.16, yBot = h * 0.84;

      ctx.fillStyle = hexA(laneColor, 0.055);
      ctx.fillRect(x0, yTop, x1 - x0, yBot - yTop);
      ctx.strokeStyle = hexA(laneColor, 0.55);
      ctx.lineWidth = 1 * dpr;
      ctx.strokeRect(x0 + 0.5, yTop + 0.5, x1 - x0 - 1, yBot - yTop - 1);

      // waveform from the real render
      if (wave.peaks) {
        const n = Math.max(2, Math.floor((x1 - x0) / (1.6 * dpr)));
        ctx.beginPath();
        for (let k = 0; k < n; k++) {
          const px = x0 + (k / (n - 1)) * (x1 - x0);
          const idx = Math.floor(((r.start + (k / n) * (r.end - r.start)) / dur) * wave.len);
          const amp = (wave.peaks[clamp(idx, 0, wave.len - 1)] || 0) * (0.55 + 0.1 * (i % 3));
          const y = (yTop + yBot) / 2;
          const hh = amp * (yBot - yTop) * 0.46;
          ctx.moveTo(px, y - hh);
          ctx.lineTo(px, y + hh);
        }
        ctx.strokeStyle = hexA(laneColor, 0.75);
        ctx.lineWidth = 1 * dpr;
        ctx.stroke();
      }
    });

    // playhead
    const px = (state.pos / dur) * w;
    ctx.strokeStyle = '#e9e4d2';
    ctx.globalAlpha = 0.9;
    ctx.lineWidth = 1 * dpr;
    ctx.beginPath();
    ctx.moveTo(px + 0.5, 0);
    ctx.lineTo(px + 0.5, h);
    ctx.stroke();
    ctx.globalAlpha = 1;

    // live level glow at the head
    const lvl = state.levels[i] || 0;
    if (lvl > 0.02) {
      const g = ctx.createRadialGradient(px, h / 2, 0, px, h / 2, 22 * dpr);
      g.addColorStop(0, hexA(laneColor, 0.45 * lvl));
      g.addColorStop(1, hexA(laneColor, 0));
      ctx.fillStyle = g;
      ctx.fillRect(px - 24 * dpr, 0, 48 * dpr, h);
    }
  }

  function takesFor(i, dur) {
    // Hand-placed so the deck reads like eight recorded takes, not a test tone.
    const layout = [
      [[0.10, 9.10]],
      [[0.10, 2.40], [3.00, 5.10], [6.20, 9.10]],
      [[0.60, 9.30]],
      [[1.40, 4.20], [5.00, 8.60]],
      [[1.40, 4.20], [5.00, 8.60]],
      [[2.20, 8.80]],
      [[0.30, 6.40], [7.00, 9.40]],
      [[0.10, 9.20]],
    ][i] || [[0.2, 8.8]];
    return layout.map(([a, b]) => ({ start: a * (dur / 9.2), end: b * (dur / 9.2) }));
  }

  function hexA(hex, a) {
    const n = parseInt(hex.slice(1), 16);
    return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
  }

  // ------------------------------------------------------------- transport
  function syncTransportUI() {
    document.querySelectorAll('.key').forEach((k) => {
      const act = k.dataset.act;
      k.classList.toggle('on', (act === 'play' && state.playing) || (act === 'rec' && state.armed.size > 0));
    });
    $('#stateline').textContent = state.playing
      ? (state.dir < 0 ? 'REVERSE' : 'ROLLING')
      : 'STOPPED';
    updateReels();
  }

  function updateReels() {
    const spin = state.playing && state.rate > 0.02;
    const dur = spin ? `${clamp(2.2 / state.rate, 0.5, 8)}s` : '0s';
    ['#reelA', '#reelB'].forEach((sel) => {
      const r = $(sel);
      r.style.setProperty('--spin', dur);
      r.classList.toggle('spinning', spin);
      r.classList.toggle('rev', state.dir < 0);
    });
  }

  function updateLoopUI() {
    const has = state.loop.out > state.loop.in;
    $('#loopread').textContent = has
      ? `${state.loop.in.toFixed(2)} → ${state.loop.out.toFixed(2)} s`
      : '–';
    $('#loopToggle').classList.toggle('on', state.loop.on);
    laneCanvases.forEach((c, i) => drawLane(c, i));
  }

  let rateTarget = 1;
  function setRateTarget(target, ramp) {
    if (!ramp) { rateTarget = target; return; }
    rateTarget = target;
  }

  // ------------------------------------------------------------ main loop
  let last = performance.now();

  function frame(now) {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;

    // rate glides to its target: that glide is the tape stop.
    const glide = 1 - Math.exp(-dt / 0.09);
    state.rate += (rateTarget - state.rate) * glide;

    if (state.playing) {
      if (state.rate < 0.03) {
        if (!audio.stalled) { stopSource(); audio.stalled = true; }
      } else {
        if (audio.stalled) { audio.stalled = false; startSource(state.pos); }
        else if (audio.source) audio.source.playbackRate.value = state.rate;
        state.pos += dt * state.rate * state.dir;
      }
    }

    // loop wrap
    if (state.loop.on && state.loop.out > state.loop.in) {
      const span = state.loop.out - state.loop.in;
      if (state.dir > 0 && state.pos > state.loop.out) state.pos = state.loop.in + ((state.pos - state.loop.out) % span);
      if (state.dir < 0 && state.pos < state.loop.in) state.pos = state.loop.out - ((state.loop.in - state.pos) % span);
    }

    // ends of tape
    if (state.pos >= state.duration) state.pos = 0;
    if (state.pos < 0) state.pos = state.duration;

    // metering
    if (audio.analyser) {
      audio.analyser.getByteFrequencyData(audio.data);
      const bins = audio.data.length;
      let sum = 0;
      for (let i = 0; i < TRACKS; i++) {
        // log-ish band split
        const a = Math.floor(bins * Math.pow(i / TRACKS, 1.7));
        const b = Math.max(a + 1, Math.floor(bins * Math.pow((i + 1) / TRACKS, 1.7)));
        let acc = 0;
        for (let k = a; k < b; k++) acc += audio.data[k];
        const v = acc / (b - a) / 255;
        state.levels[i] = state.levels[i] * 0.72 + v * 0.28;
        sum += state.levels[i];
      }
      const lvl = clamp(sum / TRACKS, 0, 1);
      $('#mL').style.height = `${clamp(lvl * 130, 0, 100)}%`;
      $('#mR').style.height = `${clamp(lvl * 122, 0, 100)}%`;
      $('#glue').style.width = `${clamp(lvl * 95, 0, 100)}%`;

      stripNodes.forEach((n, i) => {
        const bar = n.querySelector('.barmini i');
        if (bar) bar.style.height = `${clamp(state.levels[i] * 145, 0, 100)}%`;
      });

      drawScope();
    }

    // readouts
    $('#counter').textContent = fmtTime(state.pos);
    const bars = state.pos / BAR_SECONDS;
    const bar = Math.floor(bars) + 1;
    const beat = Math.floor((bars % 1) * 4) + 1;
    $('#barpos').textContent = `${String(bar).padStart(3, '0')} . ${beat}`;
    $('#speedOut').textContent = `${state.rate.toFixed(2)} ×`;

    laneCanvases.forEach((c, i) => drawLane(c, i));
    requestAnimationFrame(frame);
  }

  function drawScope() {
    const c = $('#scope');
    const ctx = c.getContext('2d');
    const { width: w, height: h } = c;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(233,228,210,.10)';
    for (let y = 8; y < h; y += 16) {
      ctx.beginPath(); ctx.moveTo(0, y + .5); ctx.lineTo(w, y + .5); ctx.stroke();
    }
    const bins = audio.data.length;
    ctx.beginPath();
    const accent = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#e2622a';
    for (let i = 0; i < 120; i++) {
      const idx = Math.floor(Math.pow(i / 120, 1.8) * bins);
      const v = (audio.data[idx] || 0) / 255;
      const x = (i / 120) * w;
      const y = h - 4 - v * (h - 10);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.strokeStyle = accent;
    ctx.lineWidth = 1.2;
    ctx.stroke();
  }

  // -------------------------------------------------------------- controls
  function bindControls() {
    document.querySelectorAll('.key').forEach((k) => {
      k.addEventListener('click', () => {
        switch (k.dataset.act) {
          case 'play': togglePlay(); break;
          case 'stop': pause(); seek(0); break;
          case 'rew': pause(); seek(Math.max(0, state.pos - 2)); break;
          case 'ff': pause(); seek(Math.min(state.duration, state.pos + 2)); break;
          case 'rec': {
            // Arm/disarm the selected track - the record path exists in the DSP,
            // the capture path arrives with the plugin.
            const t = state.selectedTrack;
            if (state.armed.has(t)) state.armed.delete(t); else state.armed.add(t);
            laneNodes[t].classList.toggle('armed', state.armed.has(t));
            syncTransportUI();
            break;
          }
        }
      });
    });

    $('#speed').addEventListener('input', (e) => {
      const v = Number(e.target.value) / 100;
      rateTarget = v;
      state.rate = v;
      if (audio.source) audio.source.playbackRate.value = v;
      updateReels();
    });

    $('#master').addEventListener('input', (e) => {
      state.master = Number(e.target.value) / 100;
      if (audio.gain) audio.gain.gain.value = state.master;
      $('#masterOut').textContent = `${(20 * Math.log10(Math.max(0.0001, state.master))).toFixed(1)} dB`;
    });

    $('#setIn').addEventListener('click', () => fireTrick('loopin', $('#setIn'), true));
    $('#setOut').addEventListener('click', () => fireTrick('loopout', $('#setOut'), true));
    $('#loopToggle').addEventListener('click', () => fireTrick('loop', $('#loopToggle'), true));

    document.querySelectorAll('.grid-btns .chip').forEach((c) => {
      c.addEventListener('click', () => {
        document.querySelectorAll('.grid-btns .chip').forEach((k) => k.classList.remove('on'));
        c.classList.add('on');
        state.grid = Number(c.dataset.grid);
        laneCanvases.forEach((cv, i) => drawLane(cv, i));
      });
    });

    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT') return;
      if (e.code === 'Space') { e.preventDefault(); togglePlay(); }
      const n = Number(e.key);
      if (n >= 1 && n <= 8) {
        const t = TRICKS[n - 1];
        const node = document.querySelector(`.trick[data-id="${t.id}"]`);
        fireTrick(t.id, node, true);
        setTimeout(() => fireTrick(t.id, node, false), 160);
      }
      if (e.key === 'ArrowLeft') { pause(); seek(state.pos - 1); }
      if (e.key === 'ArrowRight') { pause(); seek(state.pos + 1); }
    });

    window.addEventListener('resize', () => {
      resizeLanes();
    });
  }

  // ------------------------------------------------------------------ init
  function init() {
    buildChrome();
    buildStyles();
    buildLanes();
    buildTricks();
    buildMaster();
    buildStrips();
    bindControls();

    state.style = STYLES.find((s) => s.id === state.styleId);
    setStyle(state.styleId);
    updateLoopUI();
    updateReels();

    // Start the demo on first gesture (browsers require one for audio).
    const kick = () => {
      ensureAudio().then(() => {
        audio.gain.gain.value = 1;
        play();
      });
      window.removeEventListener('pointerdown', kick);
    };
    window.addEventListener('pointerdown', kick, { once: true });

    requestAnimationFrame(frame);
  }

  document.addEventListener('DOMContentLoaded', init);
})();
