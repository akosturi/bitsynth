// Full-firmware integration run: real Timer0/Timer2/ADC/ports in AVR8js,
// nested interrupts, the foreground loop, and a scripted front-panel session
// (SEQ -> PLAY with held notes -> DRONE with gates -> SEQ). Reports audio
// interrupt load, missed output samples, render span, output RMS, mode, and
// the lowest stack pointer, and stops on any register-clobbering ISR or reset.
//
// Usage:
//   deno run -A util/fullsim.js <firmware.hex> <firmware.nm> [crash-trace.txt]
// Alternative sessions via environment: SEQTOGGLE=1 (PLAY background toggle
// with a held note), SLOWTAP=1 (slow MODE double tap), DRONETEST=out.txt and
// DUMPOUT=out.txt (write output samples), PHASES=1 (print voice phases).
// Prepare the inputs from the repository root:
//   pio run -e hardware
//   avr-nm -S -C .pio/build/hardware/firmware.elf > firmware.nm
// (avr-nm is in ~/.platformio/packages/toolchain-atmelavr/bin). Requires Deno;
// AVR8js is imported from jsDelivr, so the first run needs network access.
import {
  AVRADC, AVRIOPort, AVRTimer, CPU, adcConfig, avrInstruction,
  portBConfig, portCConfig, portDConfig, timer0Config, timer2Config,
} from 'https://cdn.jsdelivr.net/npm/avr8js@0.21.1/+esm';

const [hexPath, nmPath] = Deno.args;
const CLOCK = 16_000_000;

function loadHex(text) {
  const bytes = new Uint8Array(0x8000);
  let base = 0;
  for (const line of text.split(/\r?\n/)) {
    if (!line.startsWith(':')) continue;
    const n = parseInt(line.substr(1, 2), 16), addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    if (type === 0) for (let i = 0; i < n; i++) bytes[base + addr + i] = parseInt(line.substr(9 + i * 2, 2), 16);
    else if (type === 2) base = parseInt(line.substr(9, 4), 16) << 4;
  }
  return new Uint16Array(bytes.buffer);
}
const syms = {};
for (const line of Deno.readTextFileSync(nmPath).split(/\r?\n/)) {
  const m = line.match(/^([0-9a-f]+)\s+(?:[0-9a-f]+\s+)?\w\s+(.+)$/);
  if (m) syms[m[2].trim()] = parseInt(m[1], 16);
}

const program = loadHex(Deno.readTextFileSync(hexPath));
const cpu = new CPU(program);
new AVRTimer(cpu, timer0Config);
new AVRTimer(cpu, timer2Config);
const portB = new AVRIOPort(cpu, portBConfig);
const portC = new AVRIOPort(cpu, portCConfig);
new AVRIOPort(cpu, portDConfig);
const adc = new AVRADC(cpu, adcConfig);

// Front panel: 4 parameter pots, 8 step pots, 8 step buttons, MODE.
const settingPots = [0, 420, 511, 511];
const stepPots = [0, 89, 178, 223, 312, 401, 489, 534];
const pressed = new Array(8).fill(false);
let modePressed = false;
function muxChannel() {
  const b = cpu.data[0x25]; // PORTB: D8=bit0, D10=bit2, D11=bit3
  return ((b >> 0) & 1) | (((b >> 3) & 1) << 1) | (((b >> 2) & 1) << 2);
}
function updateInputs() {
  const ch = muxChannel();
  adc.channelValues[0] = ((1023 - settingPots[ch & 3]) / 1023) * 5;
  adc.channelValues[2] = ((1023 - stepPots[ch]) / 1023) * 5;
  portC.setPin(1, !pressed[ch]);
  portC.setPin(3, !modePressed);
}
portB.addListener(() => updateInputs());
updateInputs();

// Interrupt accounting: a stack of active vectors, with cycles attributed to
// the innermost one. Vector entry = PC in the vector table after dispatch.
const VEC7 = 7 * 2, VEC8 = 8 * 2; // word addresses of TIMER2_COMPA/COMPB
const stack = [];
const snaps = [];
let audioCycles = 0, compaCount = 0, compbCount = 0, maxCompbSpan = 0;
let ocrWrites = 0, lastOcr = 256;
const ocrHist = [];
let compbStart = 0;
const RETI = 0x9518;
let minSp = 0x8ff, resets = 0, lastPcs = [];
function step() {
  const pc = cpu.pc, c0 = cpu.cycles;
  lastPcs.push(pc + 65536 * (cpu.data[1] + 256 * (stack.length ? 1 : 0))); if (lastPcs.length > 400000) lastPcs.splice(0, 200000);
  const isReti = program[pc] === RETI;
  const wasEnabled = cpu.data[95] & 0x80;
  avrInstruction(cpu);
  // Hardware executes one more instruction after the I flag is set (SEI,
  // RETI, or an SREG write) before servicing an interrupt.
  if (!wasEnabled && (cpu.data[95] & 0x80)) {
    cpu.data[95] &= 0x7f; cpu.tick(); cpu.data[95] |= 0x80;
  } else cpu.tick();
  const dc = cpu.cycles - c0;
  const top = stack.length ? stack[stack.length - 1] : -1;
  if (top === VEC7 || top === VEC8) audioCycles += dc;
  if (isReti && stack.length) {
    const v = stack.pop();
    const snap = snaps.pop();
    for (let r = 0; r < 32; r++) if (r !== 0 && r !== 1 && cpu.data[r] !== snap[r]) {
      console.log(`ISR vector ${v / 2} changed r${r}: ${snap[r].toString(16)} -> ${cpu.data[r].toString(16)} at cycle ${cpu.cycles}`);
      Deno.exit(0);
    }
    if (cpu.data[1] !== 0 && snap[1] === 0) { console.log(`ISR vector ${v / 2} left r1 nonzero`); }
    if (v === VEC8) maxCompbSpan = Math.max(maxCompbSpan, cpu.cycles - compbStart);
  }
  if (cpu.pc < 26 * 2 && cpu.pc !== pc + 1 && cpu.pc > 0) {
    // Dispatched into the vector table.
    stack.push(cpu.pc);
    snaps.push(cpu.data.slice(0, 32));
    if (cpu.pc === VEC7) compaCount++;
    if (cpu.pc === VEC8) { compbCount++; compbStart = cpu.cycles; }
    if (cpu.pc === VEC7 || cpu.pc === VEC8) audioCycles += 2; // dispatch cost attributed
  }
  const sp = cpu.data[0x5d] | (cpu.data[0x5e] << 8);
  if (sp < minSp) minSp = sp;
  if (cpu.pc === 0 && pc !== 0) {
    resets++;
    console.log(`RESET at cycle ${cpu.cycles} from pc ${(pc * 2).toString(16)} sp ${sp.toString(16)}`);
    Deno.writeTextFileSync(Deno.args[2] || 'trace.txt', lastPcs.map(p => ((p % 65536) * 2).toString(16) + ' ' + (Math.floor(p / 65536) % 256).toString(16) + ' ' + Math.floor(p / 16777216)).join(String.fromCharCode(10)));
    Deno.exit(0);
  }
}

function run(seconds, label) {
  const end = cpu.cycles + seconds * CLOCK;
  const a0 = audioCycles, c0 = compaCount, b0 = compbCount, t0 = cpu.cycles;
  maxCompbSpan = 0;
  let sum = 0, sum2 = 0, n = 0, nextSample = cpu.cycles;
  while (cpu.cycles < end) {
    step();
    if (cpu.cycles >= nextSample) {
      const v = (cpu.data[0x88] | (cpu.data[0x89] << 8)) - 256;
      if (DUMPOUT) DUMPOUT.push(v);
      sum += v; sum2 += v * v; n++;
      nextSample += 800;
    }
  }
  const periods = (cpu.cycles - t0) / 800;
  const mean = sum / n, rms = Math.sqrt(Math.max(0, sum2 / n - mean * mean));
  if (!label.startsWith(' ') && !label.startsWith('MODE')) console.log(`${label.padEnd(26)} audio load ${(100 * (audioCycles - a0) / (cpu.cycles - t0)).toFixed(1).padStart(5)}%  ` +
    `(${((audioCycles - a0) / periods).toFixed(0)} cyc/period)  COMPA ${compaCount - c0}/${Math.round(periods)}  ` +
    `COMPB ${compbCount - b0}  max COMPB span ${maxCompbSpan}  output RMS ${rms.toFixed(1)}  mode ${cpu.data[syms['instrumentMode'] - 0x800000]}  minSP ${minSp.toString(16)}`);
}
const ms = (x) => x / 1000;
function tapMode() { modePressed = true; updateInputs(); run(ms(60), 'MODE down'); modePressed = false; updateInputs(); run(ms(60), 'MODE up'); }
function doubleTap(label) {
  modePressed = true; updateInputs(); run(ms(50), '  tap'); modePressed = false; updateInputs(); run(ms(80), '  tap');
  modePressed = true; updateInputs(); run(ms(50), '  tap'); modePressed = false; updateInputs(); run(ms(500), label);
}

const quiet = console.log;
const DUMPOUT = Deno.env.get('DUMPOUT') ? [] : null;
if (Deno.env.get('SEQTOGGLE')) {
  const en = () => cpu.data[syms['playSequencerEnabled'] - 0x800000];
  const chord = (label) => { modePressed = true; updateInputs(); run(0.1, ' m'); pressed[0] = true; updateInputs(); run(0.1, ' b');
    pressed[0] = false; updateInputs(); run(0.1, ' b'); modePressed = false; updateInputs(); run(0.6, ' m'); console.log(label, 'seq enabled =', en(), 'mode', cpu.data[syms['instrumentMode'] - 0x800000]); };
  run(0.5, ' boot'); doubleTap(' ->PLAY'); console.log('PLAY start seq enabled =', en());
  chord('toggle 1 (no note held)');
  pressed[3] = true; updateInputs(); run(0.3, ' hold');
  chord('toggle 2 (note 4 held)');
  chord('toggle 3 (note 4 held)');
  pressed[3] = false; updateInputs(); run(0.3, ' rel');
  chord('toggle 4 (released)');
  Deno.exit(0);
}
if (Deno.env.get('SLOWTAP')) {
  run(1.0, 'boot');
  modePressed = true; updateInputs(); run(0.12, ' t'); modePressed = false; updateInputs(); run(0.2, ' t');
  modePressed = true; updateInputs(); run(0.15, ' t'); modePressed = false; updateInputs(); run(0.6, 'after slow double tap');
  Deno.exit(0);
}
if (Deno.env.get('DRONETEST')) {
  run(0.5, 'boot'); doubleTap('->PLAY'); doubleTap('->DRONE');
  for (const b of [0,1,2,3]) { pressed[b] = true; updateInputs(); run(0.06, ' g'); pressed[b] = false; updateInputs(); run(0.06, ' g'); }
  const out = []; const end = cpu.cycles + 3 * CLOCK; let next = cpu.cycles;
  while (cpu.cycles < end) { step(); if (cpu.cycles >= next) { out.push((cpu.data[0x88] | (cpu.data[0x89] << 8))); next += 800; } }
  Deno.writeTextFileSync(Deno.env.get('DRONETEST'), out.join(String.fromCharCode(10))); Deno.exit(0);
}
run(1.0, 'boot + SEQ');
run(1.0, 'SEQ steady');
if (Deno.env.get('PHASES')) {
  const rd = (n, off) => cpu.data[syms[n] - 0x800000 + off] | (cpu.data[syms[n] - 0x800000 + off + 1] << 8);
  if (syms['synthVoices']) console.log('phases', [0, 1, 2, 3].map(v => rd('synthVoices', v * 22).toString(16)), 'steps', [0, 1].map(v => rd('synthVoices', v * 22 + 2)));
  else console.log('phases', [0, 1, 2, 3].map(v => rd('PCW', v * 2).toString(16)), 'steps', [0, 1].map(v => rd('FTW', v * 2)));
}
if (DUMPOUT) { Deno.writeTextFileSync(Deno.env.get('DUMPOUT'), DUMPOUT.join(String.fromCharCode(10))); Deno.exit(0); }
doubleTap('-> PLAY');
for (const b of [1, 2, 3]) pressed[b] = true; updateInputs();
run(1.0, 'PLAY 3 buttons held');
for (const b of [1, 2, 3]) pressed[b] = false; updateInputs();
run(0.3, 'PLAY released');
doubleTap('-> DRONE');
for (const b of [0, 1, 2, 3, 4, 5, 6, 7]) { pressed[b] = true; updateInputs(); run(ms(60), '  gate'); pressed[b] = false; updateInputs(); run(ms(60), '  gate'); }
run(1.0, 'DRONE 8 gates');
doubleTap('-> SEQ');
run(1.0, 'SEQ again');
