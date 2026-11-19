// Headless audio-ISR benchmark: cycles per 20 kHz sample and a hashed output trace for bit-exact comparisons.
// Usage: deno run -A util/isrbench.js <firmware.hex> <firmware.nm> [results.json], with firmware.nm from avr-nm -S -C firmware.elf.
// Options: ONLY=name,name, FRAMES=n, PROFILE=name.
import { CPU, avrInstruction } from 'https://cdn.jsdelivr.net/npm/avr8js@0.21.1/+esm';

const [hexPath, nmPath, outPath] = Deno.args;

function loadHex(text) {
  const bytes = new Uint8Array(0x8000);
  let base = 0;
  for (const line of text.split(/\r?\n/)) {
    if (!line.startsWith(':')) continue;
    const n = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    if (type === 0) for (let i = 0; i < n; i++) bytes[base + addr + i] = parseInt(line.substr(9 + i * 2, 2), 16);
    else if (type === 2) base = parseInt(line.substr(9, 4), 16) << 4;
    else if (type === 4) base = parseInt(line.substr(9, 4), 16) << 16;
  }
  return new Uint16Array(bytes.buffer);
}

const syms = {}, symSizes = {};
for (const line of Deno.readTextFileSync(nmPath).split(/\r?\n/)) {
  const m = line.match(/^([0-9a-f]+)\s+(?:([0-9a-f]+)\s+)?\w\s+(.+)$/);
  if (m) {
    syms[m[3].trim()] = parseInt(m[1], 16);
    if (m[2]) symSizes[m[3].trim()] = parseInt(m[2], 16);
  }
}
const sym = (name) => {
  if (!(name in syms)) throw new Error('missing symbol ' + name);
  return syms[name];
};
const dataAddr = (name) => sym(name) - 0x800000;
const has = (name) => name in syms;

const program = loadHex(Deno.readTextFileSync(hexPath));
const cpu = new CPU(program);
const mainWord = sym('main') >> 1;
const vecWord = sym('__vector_7') >> 1;
for (let i = 0; i < 2_000_000 && cpu.pc !== mainWord; i++) avrInstruction(cpu);
if (cpu.pc !== mainWord) throw new Error('did not reach main');
const snapshot = cpu.data.slice();

const w8 = (n, v, i = 0) => {
  if (n === 'chorusLiveOnly' && !has(n)) n = 'voiceZeroBackground';
  if (n === 'MOD' && !has(n)) { const d = Math.abs((v << 24) >> 24); pendingDepth[i] = d; if (CONTROL_IN_STRUCT()) vw('vibratoDepth', d, i); else cpu.data[dataAddr('vibratoDepth') + i] = d; return; }
  cpu.data[dataAddr(n) + i] = v & 0xff;
};
const pendingDepth = [0, 0, 0, 0];
const w16 = (n, v, i = 0) => {
  if (CONTROL_IN_STRUCT() && ['PITCH', 'envs', 'EPCW', 'EFTW', 'vibratoPeakQ8'].includes(n) && !has(n)) { vw(n, v, i); return; } const a = dataAddr(n) + i * 2; cpu.data[a] = v & 0xff; cpu.data[a + 1] = (v >> 8) & 0xff; };
const w32 = (n, v, i = 0) => {
  if (n === 'chorusLfoFTW' && has('chorusDelaySamples')) v = v * 2; const a = dataAddr(n) + i * 4; for (let b = 0; b < 4; b++) cpu.data[a + b] = (v >>> (b * 8)) & 0xff; };
const tbl = (n) => sym(n);
const SENTINEL = 0x3ff0;

const BLOCK = has('__vector_8') && has('audioOutputRing');
const vecBWord = BLOCK ? sym('__vector_8') >> 1 : 0;
const BLOCK_N = BLOCK ? symSizes['audioOutputRing'] / 4 : 1;
let lastRender = 0, maxRender = 0;
let RENDERLOG = Deno.env.get('RENDERLOG') ? [] : null;
function callVector(word) {
  let sp = cpu.data[0x5d] | (cpu.data[0x5e] << 8);
  cpu.data[sp--] = SENTINEL & 0xff;
  cpu.data[sp--] = SENTINEL >> 8;
  cpu.data[0x5d] = sp & 0xff; cpu.data[0x5e] = sp >> 8;
  cpu.pc = word;
  const start = cpu.cycles;
  let guard = 0;
  while (cpu.pc !== SENTINEL) {
    const pc = cpu.pc, c0 = cpu.cycles;
    avrInstruction(cpu);
    if (PROFILE) PROFILE[pc] = (PROFILE[pc] || 0) + (cpu.cycles - c0);
    if (++guard > 100000) throw new Error('ISR runaway');
  }
  return cpu.cycles - start + 7;
}
function callIsr() {
  if (!BLOCK) return callVector(vecWord);
  let c = callVector(vecWord);
  if (cpu.data[0x70] & 0x04) { lastRender = callVector(vecBWord); maxRender = Math.max(maxRender, lastRender); c += lastRender; if (RENDERLOG) RENDERLOG.push(lastRender); }
  return c;
}

const VOICE_FIELDS = { phase: [0, 2], step: [2, 2], source: [4, 2], pulseWidth: [6, 2], noise: [8, 2], amp: [10, 1],
  EPCW: [11, 2], EFTW: [13, 2], envs: [15, 2], PITCH: [17, 2], vibratoPeakQ8: [19, 2], vibratoDepth: [21, 1] };
const VOICE_SIZE = () => has('synthUpdateVoices') ? 22 : 11;
const CONTROL_IN_STRUCT = () => has('synthUpdateVoices');
const LEGACY = { phase: 'PCW', step: 'FTW', source: 'waveSources', pulseWidth: 'pulseWidths', noise: 'noiseLfsrs', amp: 'AMP' };
function vw(field, value, v) {
  if (has('synthVoices')) {
    const [off, size] = VOICE_FIELDS[field];
    const a = dataAddr('synthVoices') + v * VOICE_SIZE() + off;
    cpu.data[a] = value & 0xff; if (size === 2) cpu.data[a + 1] = (value >> 8) & 0xff;
  } else if (field === 'amp') w8(LEGACY[field], value, v);
  else w16(LEGACY[field], value, v);
}
function commonVoices(waves, opts = {}) {
  const pitches = [0x0359, 0x0438, 0x02a8, 0x0550];
  const mods = opts.mods ?? [20, -30, 40, 63];
  const envNames = ['Env0', 'Env1', 'Env3', 'Env2'];
  for (let v = 0; v < 4; v++) {
    w16('PITCH', pitches[v], v);
    vw('step', pitches[v], v);
    w8('MOD', mods[v], v);
    vw('source', waves[v], v);
    vw('pulseWidth', 0x5999, v);
    w16('envs', tbl(envNames[v]), v);
    w16('EPCW', 0, v);
    w16('EFTW', opts.eftw ?? 0x0008, v);
    vw('amp', 200, v);
  }
  if (has('vibratoPeakQ8') || CONTROL_IN_STRUCT()) for (let v = 0; v < 4; v++) {
    const p = (((pitches[v] >> 6) * pendingDepth[v]) >> 4) << 3;
    w16('vibratoPeakQ8', p, v);
  }
}

const SAW = () => tbl('SawTable'), TRI = () => tbl('TriangleTable'), SIN = () => tbl('SinTable');
const SQR = 0, NSE = 0xffff;

function single(v, wave, mod = 0) {
  commonVoices([SQR, SQR, SQR, SQR], { mods: [mod, mod, mod, mod] });
  for (let o = 0; o < 4; o++) if (o !== v) { vw('amp', 0, o); w16('EPCW', 0x8000, o); }
  vw('source', wave(), v);
}
const scenarios = {
  s0saw: () => single(0, SAW), s1saw: () => single(1, SAW), s1sqr: () => single(1, () => SQR), s2sin: () => single(2, SIN), s1sawVib: () => single(1, SAW, 30),
  seqIdle: () => {},
  seq4: () => commonVoices([SAW(), TRI(), SIN(), SQR]),
  seq4NoVib: () => commonVoices([SAW(), TRI(), SIN(), SQR], { mods: [0, 0, 0, 0] }),
  seq4Chorus: () => {
    commonVoices([SAW(), TRI(), SIN(), SQR]);
    w8('chorusActive', 1); w8('chorusLiveOnly', 0); w32('chorusLfoFTW', 185328);
  },
  seq4Noise: () => commonVoices([NSE, NSE, SAW(), NSE]),
  playMorphVcaChorus: () => {
    commonVoices([SAW(), SQR, TRI(), SAW()]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', SAW()); w16('playMorphSecondarySource', SQR);
    w32('playMorphLfoFTW', 127 * 16 * 17180); w8('playMorphAmount', 40);
    w16('playVcaAttenuationDepth', 200); w32('playVcaLfoFTW', 500 * 17180); w16('playVcaGain', 180);
    w8('chorusActive', 1); w8('chorusLiveOnly', 1); w32('chorusLfoFTW', 110166);
  },
  playMorphNoise: () => {
    commonVoices([SAW(), SQR, TRI(), SAW()]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', NSE); w16('playMorphSecondarySource', SIN());
    w32('playMorphLfoFTW', 50 * 16 * 17180); w8('playMorphAmount', 40);
    w16('playVcaAttenuationDepth', 256); w32('playVcaLfoFTW', 1000 * 17180); w16('playVcaGain', 180);
    w8('chorusActive', 1); w8('chorusLiveOnly', 0); w32('chorusLfoFTW', 110166);
  },
  playMorphConst: () => {
    commonVoices([SAW(), SQR, TRI(), NSE]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', SAW()); w16('playMorphSecondarySource', SQR);
    w32('playMorphLfoFTW', 0); w32('playMorphLfoPCW', 0x2a000000); w8('playMorphAmount', 42);
    w8('chorusActive', 1); w8('chorusLiveOnly', 1); w32('chorusLfoFTW', 110166);
  },
  playMorphTablesNoFx: () => {
    commonVoices([SAW(), SQR, TRI(), NSE]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', TRI()); w16('playMorphSecondarySource', SAW());
    w32('playMorphLfoFTW', 0); w32('playMorphLfoPCW', 0x30000000); w8('playMorphAmount', 48);
  },
  playMorphSqrTri: () => {
    commonVoices([SAW(), SQR, TRI(), SAW()]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', SQR); w16('playMorphSecondarySource', TRI());
    w32('playMorphLfoFTW', 90 * 16 * 17180); w8('playMorphAmount', 20);
    w16('playVcaAttenuationDepth', 100); w32('playVcaLfoFTW', 700 * 17180); w16('playVcaGain', 220);
    w8('chorusActive', 1); w8('chorusLiveOnly', 0); w32('chorusLfoFTW', 185328);
  },
  playMorphNoiseFirst: () => {
    commonVoices([SAW(), SQR, TRI(), SAW()]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', NSE); w16('playMorphSecondarySource', SAW());
    w32('playMorphLfoFTW', 70 * 16 * 17180); w8('playMorphAmount', 90);
    w16('playVcaAttenuationDepth', 180); w32('playVcaLfoFTW', 1500 * 17180); w16('playVcaGain', 120);
    w8('chorusActive', 1); w8('chorusLiveOnly', 1); w32('chorusLfoFTW', 110166);
  },
  playMorphSqrSqr: () => {
    commonVoices([SAW(), SQR, TRI(), SAW()]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', SQR); w16('playMorphSecondarySource', SQR);
    w32('playMorphLfoFTW', 30 * 16 * 17180); w8('playMorphAmount', 64);
  },
  playMorphConstNoVca: () => {
    commonVoices([SAW(), SQR, TRI(), NSE]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', TRI()); w16('playMorphSecondarySource', NSE);
    w32('playMorphLfoFTW', 0); w32('playMorphLfoPCW', 0x80000000); w8('playMorphAmount', 128);
  },
  playDrumMorphChorus: () => {
    commonVoices([SAW(), SQR, TRI(), SAW()]);
    w8('playMorphActive', 1);
    w16('playMorphPrimarySource', TRI()); w16('playMorphSecondarySource', SAW());
    w32('playMorphLfoFTW', 127 * 16 * 17180); w8('playMorphAmount', 100);
    w16('playVcaAttenuationDepth', 128); w32('playVcaLfoFTW', 300 * 17180); w16('playVcaGain', 200);
    w8('chorusActive', 1); w8('chorusLiveOnly', 1); w32('chorusLfoFTW', 185328);
    w8('drumActive', 1); w16('drumSampleAddress', tbl('snareSample')); w16('drumSampleLength', 1600);
    w32('drumSamplePhase', 0); w32('drumSampleStep', 0x12000); w16('drumEnvelope', 65535); w16('drumEnvelopeStep', 30);
    vw('amp', 0, 0); w16('EPCW', 0x8000, 0);
  },
  seqDrumChorus: () => {
    w8('chorusActive', 1); w8('chorusLiveOnly', 0); w32('chorusLfoFTW', 185328);
    w8('drumActive', 1); w16('drumSampleAddress', tbl('kickSample')); w16('drumSampleLength', 1400);
    w32('drumSamplePhase', 0); w32('drumSampleStep', 0xC000); w16('drumEnvelope', 65535); w16('drumEnvelopeStep', 20);
  },
  drone8: () => {
    w8('droneMode', 1);
    const waves = [SIN(), SIN(), TRI(), TRI(), SQR, SQR, SAW(), SAW()];
    for (let v = 0; v < 8; v++) {
      w16('droneFTW', 270 + v * 160, v);
      w16('droneWaveSources', waves[v], v);
      w8('droneTargetAmp', 255, v);
      w8('droneAmp', v & 1 ? 255 : 0, v);
    }
    for (let p = 0; p < 4; p++) w32('droneLfoFTW', (p + 1) * 300 * 8590, p);
  },
  drone8NoLfo: () => {
    w8('droneMode', 1);
    const waves = [SIN(), SAW(), TRI(), TRI(), SQR, SQR, SAW(), SAW()];
    for (let v = 0; v < 8; v++) {
      w16('droneFTW', 270 + v * 160, v);
      w16('droneWaveSources', waves[v], v);
      w8('droneTargetAmp', 255, v);
      w8('droneAmp', 255, v);
    }
  },
};

const FRAMES = +(Deno.env.get('FRAMES') || 6000);
const ONLY = Deno.env.get('ONLY');
const PROFILE_SCEN = Deno.env.get('PROFILE');
let PROFILE = null;
const results = {};
for (const [name, setup] of Object.entries(scenarios)) {
  if (ONLY && !ONLY.split(',').includes(name)) continue;
  cpu.data.set(snapshot);
  cpu.data[0x5d] = 0xff; cpu.data[0x5e] = 0x08;
  PROFILE = name === PROFILE_SCEN ? {} : null;
  setup();
  let max = 0, sum = 0, win = 0;
  maxRender = 0;
  const trace = [];
  for (let f = 0; f < FRAMES; f++) {
    const c = callIsr();
    sum += c;
    if (BLOCK) { win += c; if ((f % BLOCK_N) === BLOCK_N - 1) { max = Math.max(max, win / BLOCK_N); win = 0; } }
    else max = Math.max(max, c);
    trace.push(cpu.data[0x88] | (cpu.data[0x89] << 8));
  }
  if (Deno.env.get('DUMP')) for (const n of Deno.env.get('DUMP').split(',')) { const a = dataAddr(n); console.log(n, Array.from(cpu.data.slice(a, a + 44)).map(x => x.toString(16).padStart(2, '0')).join(' ')); }
  let h = 2166136261;
  for (const v of trace) { h ^= v; h = Math.imul(h, 16777619) >>> 0; h ^= v >> 8; h = Math.imul(h, 16777619) >>> 0; }
  const min = Math.min(...trace), maxOut = Math.max(...trace);
  if (PROFILE) Deno.writeTextFileSync(outPath + '.prof', JSON.stringify(Object.fromEntries(Object.entries(PROFILE).map(([k,v])=>[(k*2).toString(16),v/FRAMES]))));
  results[name] = { max, avg: +(sum / FRAMES).toFixed(1), hash: h.toString(16), outRange: [min, maxOut], trace };
  if (BLOCK) results[name].maxRender = maxRender;
  if (RENDERLOG) { const h = {}; RENDERLOG.forEach((r, i) => { const k = Math.round(r / 50) * 50; h[k] = (h[k] || 0) + 1; }); console.log(name, JSON.stringify(h), 'first max at block', RENDERLOG.indexOf(maxRender)); RENDERLOG = []; }
  console.log(`${name.padEnd(22)} max ${String(max).padStart(4)}  avg ${(sum / FRAMES).toFixed(1).padStart(6)}  out ${min}-${maxOut}  hash ${h.toString(16)}${BLOCK ? '  render ' + maxRender : ''}`);
}
if (PROFILE_SCEN) { const p = {}; cpu.data.set(snapshot); }
if (outPath) Deno.writeTextFileSync(outPath, JSON.stringify(results));
