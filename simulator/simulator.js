import {
  AVRADC,
  AVRIOPort,
  AVRTimer,
  CPU,
  adcConfig,
  avrInstruction,
  portBConfig,
  portCConfig,
  portDConfig,
  timer0Config,
  timer2Config,
} from 'https://cdn.jsdelivr.net/npm/avr8js@0.21.1/+esm';

const CLOCK_HZ = 16_000_000;
const FLASH_BYTES = 32 * 1024;
const PROGRAM_WORDS = 0x8000;
const AUDIO_OVERSAMPLE = 2;
const AUDIO_BUFFER_SIZE = 2048;
const AUDIO_ISR_BUDGET_CYCLES = 800;
const CYCLE_METER_SMOOTHING = 0.15;
const CYCLE_METER_UPDATE_MS = 500;
const CYCLE_METER_BLOCKS = 4;
const INTERRUPT_ENTRY_CYCLES = 4;
const INTERRUPT_ENTRY_EXTRA_CYCLES = 2;
const TIMER2_COMPA_VECTOR = timer2Config.compAInterrupt;
const TIMER2_COMPB_VECTOR = timer2Config.compBInterrupt;
const VECTOR_TABLE_WORDS = 52;
const RETI_OPCODE = 0x9518;
const SREG = 0x5f;
const AUDIO_BLOCK_CYCLES = 8 * AUDIO_ISR_BUDGET_CYCLES;
const TCCR1A = 0x80;
const TCCR1B = 0x81;
const OCR1AL = 0x88;
const OCR1AH = 0x89;

const FALLBACK_HARDWARE = {
  settings: Array.from({ length: 4 }, (_, index) => ({ label: `P${index + 1} MACRO` })),
  steps: Array.from({ length: 8 }, (_, index) => ({ label: `STEP ${index + 1}`, key: String(index + 1) })),
  pins: {
    muxSelect: [8, 11, 10],
    settingsAnalog: 'A0',
    buttonInput: 'A1',
    stepAnalog: 'A2',
    modeInput: 'A3',
    audio: 9,
  },
};

const $ = (selector) => document.querySelector(selector);
const ui = {
  firmwareFile: $('#firmware-file'),
  settingsGrid: $('#settings-grid'),
  stepsGrid: $('#steps-grid'),
  modeButton: $('#mode-button'),
  lcdLines: [$('#lcd-line-1'), $('#lcd-line-2')],
  lcdCanvas: $('#lcd-dot-matrix'),
  cycleMeter: $('#cycle-meter'),
  cycleMeterScrew: $('#preset-screw'),
};

let hardware = FALLBACK_HARDWARE;
let settings = [];
let steps = [];
let modePressed = false;
let machine = null;
let audioContext = null;
let audioNode = null;
let running = false;

const LCD_FONT_5X8 = new Uint8Array(`
  00 00 00 00 00  00 00 5f 00 00  00 07 00 07 00  14 7f 14 7f 14
  24 2a 7f 2a 12  23 13 08 64 62  36 49 55 22 50  00 05 03 00 00
  00 1c 22 41 00  00 41 22 1c 00  14 08 3e 08 14  08 08 3e 08 08
  00 50 30 00 00  08 08 08 08 08  00 60 60 00 00  20 10 08 04 02
  3e 51 49 45 3e  00 42 7f 40 00  42 61 51 49 46  21 41 45 4b 31
  18 14 12 7f 10  27 45 45 45 39  3c 4a 49 49 30  01 71 09 05 03
  36 49 49 49 36  06 49 49 29 1e  00 36 36 00 00  00 56 36 00 00
  08 14 22 41 00  14 14 14 14 14  00 41 22 14 08  02 01 51 09 06
  32 49 79 41 3e  7e 11 11 11 7e  7f 49 49 49 36  3e 41 41 41 22
  7f 41 41 22 1c  7f 49 49 49 41  7f 09 09 09 01  3e 41 49 49 7a
  7f 08 08 08 7f  00 41 7f 41 00  20 40 41 3f 01  7f 08 14 22 41
  7f 40 40 40 40  7f 02 0c 02 7f  7f 04 08 10 7f  3e 41 41 41 3e
  7f 09 09 09 06  3e 41 51 21 5e  7f 09 19 29 46  46 49 49 49 31
  01 01 7f 01 01  3f 40 40 40 3f  1f 20 40 20 1f  3f 40 38 40 3f
  63 14 08 14 63  07 08 70 08 07  61 51 49 45 43  00 7f 41 41 00
  02 04 08 10 20  00 41 41 7f 00  04 02 01 02 04  40 40 40 40 40
  00 01 02 04 00  20 54 54 54 78  7f 48 44 44 38  38 44 44 44 20
  38 44 44 48 7f  38 54 54 54 18  08 7e 09 01 02  0c 52 52 52 3e
  7f 08 04 04 78  00 44 7d 40 00  20 40 44 3d 00  7f 10 28 44 00
  00 41 7f 40 00  7c 04 18 04 78  7c 08 04 04 78  38 44 44 44 38
  7c 14 14 14 08  08 14 14 18 7c  7c 08 04 04 08  48 54 54 54 20
  04 3f 44 40 20  3c 40 40 20 7c  1c 20 40 20 1c  3c 40 30 40 3c
  44 28 10 28 44  0c 50 50 50 3c  44 64 54 4c 44  00 08 36 41 00
  00 00 7f 00 00  00 41 36 08 00  08 04 08 10 08  00 06 09 09 06
`.trim().split(/\s+/).map((value) => Number.parseInt(value, 16)));

let lcdText = ui.lcdLines.map((line) => line.textContent.padEnd(16).slice(0, 16));
let lcdDrawFrame = 0;
let lcdCgram = new Uint8Array(64);

// Draws the LCD as a 5x8 dot matrix, CGRAM glyphs included.
function drawLcdDotMatrix() {
  const canvas = ui.lcdCanvas;
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;

  const pixelRatio = Math.min(2, devicePixelRatio || 1);
  const width = Math.max(1, Math.round(rect.width * pixelRatio));
  const height = Math.max(1, Math.round(rect.height * pixelRatio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }

  const context = canvas.getContext('2d');
  context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
  context.clearRect(0, 0, rect.width, rect.height);

  const unit = Math.min(rect.width / 98, rect.height / 20);
  const matrixWidth = 95 * unit;
  const matrixHeight = 18 * unit;
  const originX = (rect.width - matrixWidth) / 2;
  const originY = (rect.height - matrixHeight) / 2;
  const dotSize = unit * 0.68;
  const dotInset = (unit - dotSize) / 2;

  context.shadowColor = 'transparent';
  context.shadowBlur = 0;
  context.fillStyle = 'rgba(139, 199, 255, .055)';
  for (let line = 0; line < 2; line++) {
    for (let character = 0; character < 16; character++) {
      for (let column = 0; column < 5; column++) {
        for (let row = 0; row < 8; row++) {
          const x = originX + (character * 6 + column) * unit + dotInset;
          const y = originY + (line * 10 + row) * unit + dotInset;
          context.fillRect(x, y, dotSize, dotSize);
        }
      }
    }
  }

  context.fillStyle = '#edf8ff';
  context.shadowColor = '#dff3ff';
  context.shadowBlur = unit * 0.75;
  for (let line = 0; line < 2; line++) {
    for (let character = 0; character < 16; character++) {
      const code = lcdText[line].charCodeAt(character);
      const glyph = code >= 32 && code <= 127 ? code - 32 : 31;
      for (let column = 0; column < 5; column++) {
        const bits = LCD_FONT_5X8[glyph * 5 + column];
        for (let row = 0; row < 8; row++) {
          const lit = code < 16
            ? lcdCgram[(code & 7) * 8 + row] & (0x10 >> column)
            : bits & (1 << row);
          if (!lit) continue;
          const x = originX + (character * 6 + column) * unit + dotInset;
          const y = originY + (line * 10 + row) * unit + dotInset;
          context.fillRect(x, y, dotSize, dotSize);
        }
      }
    }
  }
}

function setLcdLines(line1, line2, cgram = lcdCgram) {
  lcdCgram = cgram;
  lcdText = [line1, line2].map((line) => String(line).padEnd(16).slice(0, 16));
  ui.lcdLines.forEach((element, index) => {
    const text = lcdText[index].replace(/[\x00-\x0f]/g, '█');
    if (element.textContent !== text) element.textContent = text;
  });
  scheduleLcdDotMatrix();
}

function scheduleLcdDotMatrix() {
  if (lcdDrawFrame) return;
  lcdDrawFrame = requestAnimationFrame(() => {
    lcdDrawFrame = 0;
    drawLcdDotMatrix();
  });
}

new ResizeObserver(scheduleLcdDotMatrix).observe(ui.lcdCanvas);

function connectionOtherEnd(connections, endpoint) {
  for (const connection of connections) {
    if (connection[0] === endpoint) return connection[1];
    if (connection[1] === endpoint) return connection[0];
  }
  return null;
}

// Resolves a diagram.json endpoint to its Arduino pin.
function unoPin(connections, endpoint) {
  const other = connectionOtherEnd(connections, endpoint);
  if (!other?.startsWith('uno:')) return null;
  const pin = other.slice(4).split('.')[0];
  return /^\d+$/.test(pin) ? Number(pin) : pin;
}

// Reads the pin mapping from diagram.json.
async function readDiagram() {
  try {
    const response = await fetch('./diagram.json', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const diagram = await response.json();
    const part = (id) => diagram.parts.find((item) => item.id === id);
    const label = (id, fallback) => part(id)?.attrs?.text || fallback;
    hardware = {
      settings: Array.from({ length: 4 }, (_, index) => ({
        label: label(`settingText${index + 1}`, `P${index + 1}`),
      })),
      steps: Array.from({ length: 8 }, (_, index) => ({
        label: part(`stepButton${index + 1}`)?.attrs?.label || `STEP ${index + 1}`,
        key: part(`stepButton${index + 1}`)?.attrs?.key || String(index + 1),
      })),
      pins: {
        muxSelect: ['S0', 'S1', 'S2'].map((pin) => unoPin(diagram.connections, `settingsMux:${pin}`)),
        settingsAnalog: unoPin(diagram.connections, 'settingsMux:COM'),
        buttonInput: unoPin(diagram.connections, 'buttonMux:COM'),
        stepAnalog: unoPin(diagram.connections, 'stepMux:COM'),
        modeInput: unoPin(diagram.connections, 'modeButton:1.l'),
        audio: unoPin(diagram.connections, 'audio:2'),
      },
    };
    const pinSignature = JSON.stringify(hardware.pins);
    const expected = JSON.stringify(FALLBACK_HARDWARE.pins);
    if (pinSignature !== expected) throw new Error(`unsupported pin map ${pinSignature}`);
  } catch (error) {
    hardware = FALLBACK_HARDWARE;
    console.warn('diagram.json not used:', error);
  }
}

function makeKnob(control) {
  const card = document.createElement('div');
  card.className = 'control-card';
  const knob = document.createElement('div');
  knob.className = 'knob';
  knob.tabIndex = 0;
  knob.setAttribute('role', 'slider');
  knob.setAttribute('aria-label', control.label);
  knob.setAttribute('aria-valuemin', '0');
  knob.setAttribute('aria-valuemax', '1023');

  const render = () => {
    const angle = -135 + (control.value / 1023) * 270;
    knob.style.setProperty('--turn', `${angle}deg`);
    knob.setAttribute('aria-valuenow', String(control.value));
    machine?.updateInputs();
  };
  const setValue = (next) => {
    control.value = Math.max(0, Math.min(1023, Math.round(next)));
    render();
  };

  let dragStartY = 0;
  let dragStartValue = 0;
  knob.addEventListener('pointerdown', (event) => {
    dragStartY = event.clientY;
    dragStartValue = control.value;
    knob.setPointerCapture(event.pointerId);
    event.preventDefault();
  });
  knob.addEventListener('pointermove', (event) => {
    if (!knob.hasPointerCapture(event.pointerId)) return;
    setValue(dragStartValue + (dragStartY - event.clientY) * 6);
  });
  knob.addEventListener('wheel', (event) => {
    event.preventDefault();
    setValue(control.value + (event.deltaY < 0 ? 24 : -24));
  }, { passive: false });
  knob.addEventListener('keydown', (event) => {
    const delta = event.shiftKey ? 64 : 12;
    if (event.key === 'ArrowUp' || event.key === 'ArrowRight') setValue(control.value + delta);
    else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') setValue(control.value - delta);
    else if (event.key === 'Home') setValue(0);
    else if (event.key === 'End') setValue(1023);
    else return;
    event.preventDefault();
  });

  card.append(knob);
  render();
  return card;
}

function setStepPressed(index, pressed) {
  const step = steps[index];
  if (!step || step.pressed === pressed) return;
  step.pressed = pressed;
  step.button.classList.toggle('active', pressed);
  step.button.setAttribute('aria-pressed', String(pressed));
  machine?.updateInputs();
}

function setModePressed(pressed) {
  if (modePressed === pressed) return;
  modePressed = pressed;
  ui.modeButton.classList.toggle('active', pressed);
  ui.modeButton.setAttribute('aria-pressed', String(pressed));
  machine?.updateInputs();
}

function bindMomentary(element, setter) {
  element.addEventListener('pointerdown', (event) => {
    element.setPointerCapture(event.pointerId);
    setter(true);
    event.preventDefault();
  });
  element.addEventListener('pointerup', (event) => {
    if (element.hasPointerCapture(event.pointerId)) element.releasePointerCapture(event.pointerId);
    setter(false);
  });
  element.addEventListener('pointercancel', () => setter(false));
  element.addEventListener('lostpointercapture', () => setter(false));
}

function buildControls() {
  settings = hardware.settings.map((item) => ({ ...item, value: 511 }));
  steps = hardware.steps.map((item) => ({ ...item, value: 511, pressed: false }));
  ui.settingsGrid.replaceChildren(...settings.map(makeKnob));
  const cards = steps.map((control, index) => {
    const card = makeKnob(control);
    const button = document.createElement('button');
    button.className = 'pad';
    button.setAttribute('aria-label', `${control.label}, key ${control.key}`);
    control.button = button;
    bindMomentary(button, (pressed) => setStepPressed(index, pressed));
    card.append(button);
    return card;
  });
  ui.stepsGrid.replaceChildren(...cards);
  bindMomentary(ui.modeButton, setModePressed);
}

// Write-only HD44780 model driven by the 4-bit LCD port.
class HD44780 {
  constructor(render) {
    this.render = render;
    this.reset();
  }

  reset() {
    this.ddram = new Uint8Array(0x80).fill(32);
    this.cgram = new Uint8Array(64);
    this.cgramMode = false;
    this.address = 0;
    this.pendingNibble = null;
    this.fourBit = false;
    this.increment = true;
    this.lastPort = 0;
    this.paint();
  }

  portChanged(value, oldValue) {
    const enableFell = (oldValue & 0x08) && !(value & 0x08);
    if (!enableFell) return;
    const nibble = (value >> 4) & 0x0f;
    const rs = Boolean(value & 0x04);
    if (!this.fourBit) {
      if (!rs && nibble === 0x02) this.fourBit = true;
      return;
    }
    if (this.pendingNibble === null) {
      this.pendingNibble = { nibble, rs };
      return;
    }
    const byte = (this.pendingNibble.nibble << 4) | nibble;
    const dataMode = this.pendingNibble.rs;
    this.pendingNibble = null;
    if (dataMode) this.writeData(byte);
    else this.command(byte);
  }

  command(value) {
    if (value === 0x01) {
      this.ddram.fill(32);
      this.address = 0;
      this.cgramMode = false;
      this.paint();
    } else if ((value & 0xfe) === 0x02) {
      this.address = 0;
      this.cgramMode = false;
    } else if (value & 0x80) {
      this.address = value & 0x7f;
      this.cgramMode = false;
    } else if (value & 0x40) {
      this.address = value & 0x3f;
      this.cgramMode = true;
    } else if ((value & 0xfc) === 0x04) {
      this.increment = Boolean(value & 0x02);
    }
  }

  writeData(value) {
    if (this.cgramMode) {
      this.cgram[this.address & 0x3f] = value & 0x1f;
      this.address = (this.address + (this.increment ? 1 : -1)) & 0x3f;
    } else {
      this.ddram[this.address & 0x7f] = value;
      this.address = (this.address + (this.increment ? 1 : -1)) & 0x7f;
    }
    this.paint();
  }

  paint() {
    const line = (offset) => Array.from(this.ddram.slice(offset, offset + 16), (code) =>
      code < 16 || (code >= 32 && code <= 126) ? String.fromCharCode(code) : ' ').join('');
    this.render(line(0x00), line(0x40), this.cgram);
  }
}

function multiplyMatrices(left, right) {
  const size = left.length;
  const output = Array.from({ length: size }, () => Array(size).fill(0));
  for (let row = 0; row < size; row++) {
    for (let middle = 0; middle < size; middle++) {
      const value = left[row][middle];
      for (let column = 0; column < size; column++) output[row][column] += value * right[middle][column];
    }
  }
  return output;
}

// Matrix exponential by scaling and squaring.
function matrixExponential(matrix) {
  const size = matrix.length;
  const identity = Array.from({ length: size }, (_, row) =>
    Array.from({ length: size }, (_, column) => row === column ? 1 : 0));
  const norm = Math.max(...matrix.map((row) => row.reduce((sum, value) => sum + Math.abs(value), 0)));
  const squarings = Math.max(0, Math.ceil(Math.log2(Math.max(norm, 0.5) / 0.5)));
  const scale = 2 ** squarings;
  const scaled = matrix.map((row) => row.map((value) => value / scale));
  let result = identity.map((row) => [...row]);
  let term = identity.map((row) => [...row]);
  for (let order = 1; order <= 24; order++) {
    term = multiplyMatrices(term, scaled).map((row) => row.map((value) => value / order));
    let largest = 0;
    for (let row = 0; row < size; row++) {
      for (let column = 0; column < size; column++) {
        result[row][column] += term[row][column];
        largest = Math.max(largest, Math.abs(term[row][column]));
      }
    }
    if (largest < 1e-15) break;
  }
  for (let index = 0; index < squarings; index++) result = multiplyMatrices(result, result);
  return result;
}

// Exact state-space model of the RC output ladder and AC coupling.
class OutputFilter {
  constructor() {
    const r1 = 2200;
    const r2 = 2200;
    const r3 = 2200;
    const r4 = 100000;
    const c1 = 4.7e-9;
    const c2 = 4.7e-9;
    const c3 = 6.8e-9;
    const c4 = 4.7e-6;
    const g1 = 1 / r1;
    const g2 = 1 / r2;
    const g3 = 1 / r3;
    const g4 = 1 / r4;
    this.a = [
      [-(g1 + g2) / c1, g2 / c1, 0, 0],
      [g2 / c2, -(g2 + g3) / c2, g3 / c2, 0],
      [0, g3 / c3, -(g3 + g4) / c3, g4 / c3],
      [0, 0, g4 / c4, -g4 / c4],
    ];
    this.b = [g1 / c1, 0, 0, 0];
    this.state = new Float64Array(4);
    this.input = 0;
    this.lastCycle = 0;
    this.cache = new Map();
    for (let cycles = 1; cycles <= 512; cycles++) this.coefficients(cycles);
  }

  coefficients(cycles) {
    if (this.cache.has(cycles)) return this.cache.get(cycles);
    const dt = cycles / CLOCK_HZ;
    const augmented = Array.from({ length: 5 }, (_, row) =>
      Array.from({ length: 5 }, (_, column) => {
        if (row < 4 && column < 4) return this.a[row][column] * dt;
        if (row < 4 && column === 4) return this.b[row] * dt;
        return 0;
      }));
    const exponential = matrixExponential(augmented);
    const m = new Float64Array(16);
    const n = new Float64Array(4);
    for (let row = 0; row < 4; row++) {
      n[row] = exponential[row][4];
      for (let column = 0; column < 4; column++) m[row * 4 + column] = exponential[row][column];
    }
    const result = { m, n };
    this.cache.set(cycles, result);
    return result;
  }

  integrateTo(cycle) {
    let remaining = Math.max(0, Math.floor(cycle - this.lastCycle));
    while (remaining > 0) {
      const chunk = Math.min(remaining, 512);
      const { m, n } = this.coefficients(chunk);
      const [x0, x1, x2, x3] = this.state;
      const input = this.input;
      this.state[0] = m[0] * x0 + m[1] * x1 + m[2] * x2 + m[3] * x3 + n[0] * input;
      this.state[1] = m[4] * x0 + m[5] * x1 + m[6] * x2 + m[7] * x3 + n[1] * input;
      this.state[2] = m[8] * x0 + m[9] * x1 + m[10] * x2 + m[11] * x3 + n[2] * input;
      this.state[3] = m[12] * x0 + m[13] * x1 + m[14] * x2 + m[15] * x3 + n[3] * input;
      this.lastCycle += chunk;
      remaining -= chunk;
    }
  }

  setInputVoltage(voltage, cycle) {
    this.integrateTo(cycle);
    this.input = voltage;
  }

  sample(cycle) {
    this.integrateTo(cycle);
    return this.state[2] - this.state[3];
  }
}

// Timer1 9-bit Fast PWM on D9; each period's mean voltage drives the filter, so the carrier does not alias.
class FastPwm9 {
  constructor(filter) {
    this.filter = filter;
    this.running = false;
    this.enabled = false;
    this.lastCycle = 0;
    this.phase = 0;
    this.activeCompare = 0;
    this.pendingCompare = 0;
    this.hasPendingCompare = false;
    this.voltage = 0;
  }

  setVoltage(voltage, cycle) {
    if (this.voltage === voltage) return;
    this.voltage = voltage;
    this.filter.setInputVoltage(voltage, cycle);
  }

  periodVoltage() {
    return this.activeCompare >= 511 ? 5 : 5 * (this.activeCompare + 1) / 512;
  }

  start(cycle, compare) {
    this.running = true;
    this.lastCycle = cycle;
    this.phase = 0;
    this.activeCompare = Math.min(compare, 511);
    this.pendingCompare = this.activeCompare;
    this.hasPendingCompare = false;
    if (this.enabled) this.setVoltage(this.periodVoltage(), cycle);
  }

  stop(cycle) {
    this.advanceTo(cycle);
    this.running = false;
  }

  writeCompare(compare, cycle) {
    this.advanceTo(cycle);
    this.pendingCompare = Math.min(compare, 511);
    this.hasPendingCompare = true;
  }

  setEnabled(enabled, cycle, portHigh) {
    this.advanceTo(cycle);
    this.enabled = enabled;
    this.setVoltage(enabled ? this.periodVoltage() : (portHigh ? 5 : 0), cycle);
  }

  advanceTo(targetCycle) {
    if (!this.running || targetCycle <= this.lastCycle) return;
    while (this.lastCycle < targetCycle) {
      const distance = 512 - this.phase;
      if (this.lastCycle + distance > targetCycle) {
        this.phase += targetCycle - this.lastCycle;
        this.lastCycle = targetCycle;
        this.filter.integrateTo(targetCycle);
        return;
      }
      this.lastCycle += distance;
      this.phase = 0;
      if (this.hasPendingCompare) {
        this.activeCompare = this.pendingCompare;
        this.hasPendingCompare = false;
      }
      if (this.enabled) this.setVoltage(this.periodVoltage(), this.lastCycle);
    }
  }
}

// Timer2 CTC with compare A/B interrupts; schedules only the next match, for speed.
class CtcTimer2 {
  constructor(cpu) {
    const { TIFR, TIMSK, TCNT, OCRA, OCRB, TCCRA, TCCRB } = timer2Config;
    const interrupt = (address, mask) =>
      ({ address, flagRegister: TIFR, flagMask: mask, enableRegister: TIMSK, enableMask: mask });
    this.cpu = cpu;
    this.compA = interrupt(timer2Config.compAInterrupt, 2);
    this.compB = interrupt(timer2Config.compBInterrupt, 4);
    this.divider = 0;
    this.top = 0;
    this.ocrB = 0;
    this.base = 0;
    this.match = () => this.onMatch();
    const configure = (register) => (value) => {
      const count = this.count();
      const phase = this.divider ? (cpu.cycles - this.base) % this.divider : 0;
      cpu.data[register] = value;
      this.divider = timer2Config.dividers[cpu.data[TCCRB] & 7] || 0;
      if ((cpu.data[TCCRA] & 3) !== 2 || (cpu.data[TCCRB] & 8)) this.divider = 0;
      this.top = cpu.data[OCRA];
      this.ocrB = cpu.data[OCRB];
      this.restart(register === TCNT ? value : Math.min(count, this.top), phase % (this.divider || 1));
      return true;
    };
    for (const register of [TCCRA, TCCRB, OCRA, OCRB, TCNT]) cpu.writeHooks[register] = configure(register);
    cpu.readHooks[TCNT] = () => this.count();
    cpu.writeHooks[TIFR] = (value) => {
      cpu.data[TIFR] = value;
      cpu.clearInterruptByFlag(this.compA, value);
      cpu.clearInterruptByFlag(this.compB, value);
      return true;
    };
    cpu.writeHooks[TIMSK] = (value) => {
      cpu.updateInterruptEnable(this.compA, value);
      cpu.updateInterruptEnable(this.compB, value);
    };
  }

  count() {
    if (!this.divider) return this.cpu.data[timer2Config.TCNT];
    const ticks = Math.floor((this.cpu.cycles - this.base) / this.divider);
    return ticks % (this.top + 1);
  }

  restart(count, phase) {
    this.cpu.clearClockEvent(this.match);
    this.cpu.data[timer2Config.TCNT] = count;
    if (!this.divider) return;
    this.base = this.cpu.cycles - phase - count * this.divider;
    this.schedule(count);
  }

  schedule(count) {
    const period = this.top + 1;
    let next = period;
    for (const target of [this.top, this.ocrB]) {
      if (target > this.top) continue;
      const distance = (target - count + period) % period || period;
      next = Math.min(next, distance);
    }
    const ticks = Math.floor((this.cpu.cycles - this.base) / this.divider);
    this.cpu.addClockEvent(this.match, this.base + (ticks + next) * this.divider - this.cpu.cycles);
  }

  onMatch() {
    const ticks = Math.floor((this.cpu.cycles - this.base) / this.divider);
    const count = ticks % (this.top + 1);
    if (count === this.top) this.cpu.setInterruptFlag(this.compA);
    if (count === this.ocrB) this.cpu.setInterruptFlag(this.compB);
    this.base += (ticks - count) * this.divider;
    this.schedule(count);
  }
}

class BiquadLowPass {
  constructor(sampleRate, cutoff, q) {
    const omega = 2 * Math.PI * cutoff / sampleRate;
    const cosine = Math.cos(omega);
    const alpha = Math.sin(omega) / (2 * q);
    const a0 = 1 + alpha;
    this.b0 = ((1 - cosine) / 2) / a0;
    this.b1 = (1 - cosine) / a0;
    this.b2 = this.b0;
    this.a1 = (-2 * cosine) / a0;
    this.a2 = (1 - alpha) / a0;
    this.z1 = 0;
    this.z2 = 0;
  }

  process(input) {
    const output = this.b0 * input + this.z1;
    this.z1 = this.b1 * input - this.a1 * output + this.z2;
    this.z2 = this.b2 * input - this.a2 * output;
    return output;
  }
}

// Four-pole Butterworth anti-alias filter for the monitor output.
class MonitorAntiAliasFilter {
  constructor(outputSampleRate) {
    const oversampledRate = outputSampleRate * AUDIO_OVERSAMPLE;
    const cutoff = Math.min(18_000, outputSampleRate * 0.4);
    this.sections = [
      new BiquadLowPass(oversampledRate, cutoff, 0.5411961),
      new BiquadLowPass(oversampledRate, cutoff, 1.306563),
    ];
  }

  process(input) {
    return this.sections.reduce((value, section) => section.process(value), input);
  }
}

class BitsynthMachine {
  constructor(bytes) {
    this.program = new Uint16Array(PROGRAM_WORDS);
    this.program.fill(0xffff);
    new Uint8Array(this.program.buffer).set(bytes.subarray(0, FLASH_BYTES));
    this.cpu = new CPU(this.program);
    this.timer0 = new AVRTimer(this.cpu, timer0Config);
    this.timer2 = new CtcTimer2(this.cpu);
    this.portB = new AVRIOPort(this.cpu, portBConfig);
    this.portC = new AVRIOPort(this.cpu, portCConfig);
    this.portD = new AVRIOPort(this.cpu, portDConfig);
    this.adc = new AVRADC(this.cpu, adcConfig);
    this.filter = new OutputFilter();
    this.pwm = new FastPwm9(this.filter);
    this.lcd = new HD44780(setLcdLines);
    this.portValues = { B: 0, C: 0, D: 0 };
    this.audioHigh = false;
    this.sampleCycle = 0;
    this.antiAlias = null;
    this.antiAliasSampleRate = 0;
    this.isrStack = [];
    this.meterWindows = 0;
    this.audioIsrWindowCycles = 0;
    this.audioIsrWindowStart = 0;
    this.audioIsrCallbackCycles = 0;
    this.audioIsrCycles = 0;
    this.audioIsrMaxCycles = 0;
    this.audioIsrBlockMax = 0;

    this.portB.addListener((value, oldValue) => this.outputChanged('B', value, oldValue));
    this.portD.addListener((value, oldValue) => {
      this.lcd.portChanged(value, oldValue);
      this.outputChanged('D', value, oldValue);
    });
    this.portC.addListener((value, oldValue) => this.outputChanged('C', value, oldValue));

    this.cpu.writeHooks[TCCR1A] = (value) => {
      if (this.pwm.running) this.pwm.advanceTo(this.cpu.cycles);
      this.cpu.data[TCCR1A] = value;
      const enabled = (value & 0xc0) === 0x80;
      if (enabled !== this.pwm.enabled) this.pwm.setEnabled(enabled, this.cpu.cycles, this.audioHigh);
      return true;
    };
    this.cpu.writeHooks[TCCR1B] = (value) => {
      const oldValue = this.cpu.data[TCCR1B];
      const wasRunning = Boolean(oldValue & 0x07);
      const willRun = Boolean(value & 0x07);
      if (wasRunning) this.pwm.advanceTo(this.cpu.cycles);
      this.cpu.data[TCCR1B] = value;
      if (!wasRunning && willRun) {
        const compare = this.cpu.data[OCR1AL] | (this.cpu.data[OCR1AH] << 8);
        this.pwm.start(this.cpu.cycles, compare);
      } else if (wasRunning && !willRun) {
        this.pwm.stop(this.cpu.cycles);
      }
      return true;
    };
    this.cpu.writeHooks[OCR1AL] = (value) => {
      this.cpu.data[OCR1AL] = value;
      const compare = value | (this.cpu.data[OCR1AH] << 8);
      if (this.pwm.running) this.pwm.writeCompare(compare, this.cpu.cycles);
      return true;
    };

    this.updateInputs();
  }

  arduinoPin(pin) {
    if (typeof pin === 'string' && pin.startsWith('A')) {
      return Boolean(this.portValues.C & (1 << Number(pin.slice(1))));
    }
    if (pin >= 8) return Boolean(this.portValues.B & (1 << (pin - 8)));
    return Boolean(this.portValues.D & (1 << pin));
  }

  outputChanged(port, value, oldValue) {
    this.portValues[port] = value;
    const audioHigh = this.arduinoPin(hardware.pins.audio);
    if (audioHigh !== this.audioHigh) {
      this.audioHigh = audioHigh;
      if (!this.pwm.enabled) this.filter.setInputVoltage(audioHigh ? 5 : 0, this.cpu.cycles);
    }
    if (port === 'B' && ((value ^ oldValue) & 0x0d)) this.updateInputs();
  }

  selectedMuxChannel() {
    return hardware.pins.muxSelect.reduce((channel, pin, bit) =>
      channel | (this.arduinoPin(pin) ? (1 << bit) : 0), 0);
  }

  adcVoltage(control) {
    const raw = 1023 - control.value;
    return (raw / 1023) * 5;
  }

  updateInputs() {
    const channel = this.selectedMuxChannel();
    const setting = settings[channel] || settings[0];
    const step = steps[channel] || steps[0];
    if (setting) this.adc.channelValues[0] = this.adcVoltage(setting);
    if (step) this.adc.channelValues[2] = this.adcVoltage(step);
    this.portC.setPin(1, !(steps[channel]?.pressed));
    this.portC.setPin(3, !modePressed);
  }

  // Runs to targetCycle; like the hardware, one more instruction runs after interrupts are enabled.
  run(targetCycle) {
    const cpu = this.cpu;
    const data = cpu.data;
    while (cpu.cycles < targetCycle) {
      const enabled = data[SREG] & 0x80;
      avrInstruction(cpu);
      if (!enabled && (data[SREG] & 0x80)) {
        data[SREG] &= 0x7f;
        cpu.tick();
        data[SREG] |= 0x80;
      } else {
        cpu.tick();
        if (enabled && cpu.pc < VECTOR_TABLE_WORDS) cpu.cycles += INTERRUPT_ENTRY_EXTRA_CYCLES;
      }
    }
  }

  // Like run(), with audio-ISR cycle accounting for the meter.
  runMetered(targetCycle) {
    const cpu = this.cpu;
    const data = cpu.data;
    const program = this.program;
    const stack = this.isrStack;
    const audio = (vector) => vector === TIMER2_COMPA_VECTOR || vector === TIMER2_COMPB_VECTOR;
    while (cpu.cycles < targetCycle && this.meterWindows) {
      const before = cpu.cycles;
      const returns = program[cpu.pc] === RETI_OPCODE;
      const enabled = data[SREG] & 0x80;
      avrInstruction(cpu);
      let entered = false;
      if (!enabled && (data[SREG] & 0x80)) {
        data[SREG] &= 0x7f;
        cpu.tick();
        data[SREG] |= 0x80;
      } else {
        cpu.tick();
        entered = enabled && cpu.pc < VECTOR_TABLE_WORDS;
        if (entered) cpu.cycles += INTERRUPT_ENTRY_EXTRA_CYCLES;
      }
      const entry = entered ? INTERRUPT_ENTRY_CYCLES : 0;
      if (audio(stack[stack.length - 1])) this.audioIsrWindowCycles += cpu.cycles - before - entry;
      if (returns && stack.length) stack.pop();
      if (entered) {
        stack.push(cpu.pc);
        if (audio(cpu.pc)) this.audioIsrWindowCycles += entry;
      }
      if (cpu.cycles - this.audioIsrWindowStart >= AUDIO_BLOCK_CYCLES) {
        const perSample = this.audioIsrWindowCycles * AUDIO_ISR_BUDGET_CYCLES /
          (cpu.cycles - this.audioIsrWindowStart);
        if (this.audioIsrWindowWarm) {
          this.audioIsrBlockMax = Math.max(this.audioIsrBlockMax, perSample);
          this.audioIsrCallbackCycles += this.audioIsrWindowCycles;
          this.audioIsrMeasuredCycles += cpu.cycles - this.audioIsrWindowStart;
        }
        this.audioIsrWindowWarm = true;
        this.audioIsrWindowCycles = 0;
        this.audioIsrWindowStart = cpu.cycles;
        this.meterWindows--;
      }
    }
  }

  // Runs the firmware for one audio callback and samples the filtered output.
  renderAudio(outputLeft, outputRight) {
    const outputSampleRate = audioContext.sampleRate;
    if (!this.antiAlias || this.antiAliasSampleRate !== outputSampleRate) {
      this.antiAlias = new MonitorAntiAliasFilter(outputSampleRate);
      this.antiAliasSampleRate = outputSampleRate;
    }
    this.audioIsrBlockMax = 0;
    this.audioIsrCallbackCycles = 0;
    this.meterWindows = ui.cycleMeter.hidden ? 0 : 1 + CYCLE_METER_BLOCKS;
    this.isrStack.length = 0;
    this.audioIsrWindowCycles = 0;
    this.audioIsrWindowStart = this.cpu.cycles;
    this.audioIsrWindowWarm = false;
    this.audioIsrMeasuredCycles = 0;
    const cyclesPerSubsample = CLOCK_HZ / (outputSampleRate * AUDIO_OVERSAMPLE);
    for (let index = 0; index < outputLeft.length; index++) {
      let sample = 0;
      for (let subSample = 0; subSample < AUDIO_OVERSAMPLE; subSample++) {
        this.sampleCycle += cyclesPerSubsample;
        if (this.meterWindows) this.runMetered(this.sampleCycle);
        this.run(this.sampleCycle);
        this.pwm.advanceTo(this.cpu.cycles);
        const voltage = this.filter.sample(this.cpu.cycles);
        sample = this.antiAlias.process(voltage / 2.5);
      }
      outputLeft[index] = outputRight[index] = Math.max(-1, Math.min(1, sample));
    }
    if (this.audioIsrBlockMax) {
      const average = this.audioIsrCallbackCycles * AUDIO_ISR_BUDGET_CYCLES /
        Math.max(1, this.audioIsrMeasuredCycles);
      this.audioIsrCycles = this.audioIsrCycles
        ? this.audioIsrCycles * (1 - CYCLE_METER_SMOOTHING) +
          average * CYCLE_METER_SMOOTHING
        : average;
      this.audioIsrMaxCycles = Math.max(this.audioIsrMaxCycles, Math.round(this.audioIsrBlockMax));
    }
  }
}

function parseIntelHex(text) {
  const bytes = new Uint8Array(FLASH_BYTES).fill(0xff);
  let base = 0;
  let written = 0;
  let highest = 0;
  for (const [lineIndex, rawLine] of text.split(/\r?\n/).entries()) {
    const line = rawLine.trim();
    if (!line) continue;
    if (!/^:[0-9a-f]+$/i.test(line) || line.length % 2 !== 1) throw new Error(`Invalid HEX line ${lineIndex + 1}`);
    const record = Uint8Array.from(line.slice(1).match(/../g), (pair) => Number.parseInt(pair, 16));
    const count = record[0];
    if (record.length !== count + 5) throw new Error(`Bad HEX length on line ${lineIndex + 1}`);
    if ((record.reduce((sum, value) => sum + value, 0) & 0xff) !== 0) throw new Error(`Bad HEX checksum on line ${lineIndex + 1}`);
    const address = (record[1] << 8) | record[2];
    const type = record[3];
    if (type === 0) {
      for (let index = 0; index < count; index++) {
        const target = base + address + index;
        if (target < FLASH_BYTES) {
          bytes[target] = record[4 + index];
          written++;
          highest = Math.max(highest, target + 1);
        }
      }
    } else if (type === 2) {
      base = ((record[4] << 8) | record[5]) << 4;
    } else if (type === 4) {
      base = ((record[4] << 8) | record[5]) << 16;
    } else if (type === 1) {
      break;
    }
  }
  if (!written) throw new Error('HEX contains no ATmega328P flash data');
  return bytes.slice(0, highest);
}

function parseElf(buffer) {
  const view = new DataView(buffer);
  if (view.byteLength < 52 || view.getUint32(0, false) !== 0x7f454c46) throw new Error('Not an ELF file');
  if (view.getUint8(4) !== 1 || view.getUint8(5) !== 1) throw new Error('ELF must be 32-bit little-endian');
  if (view.getUint16(18, true) !== 83) throw new Error('ELF target is not AVR');
  const output = new Uint8Array(FLASH_BYTES).fill(0xff);
  const source = new Uint8Array(buffer);
  const programOffset = view.getUint32(28, true);
  const entrySize = view.getUint16(42, true);
  const entryCount = view.getUint16(44, true);
  let written = 0;
  let highest = 0;
  for (let index = 0; index < entryCount; index++) {
    const offset = programOffset + index * entrySize;
    if (offset + 32 > view.byteLength || view.getUint32(offset, true) !== 1) continue;
    const fileOffset = view.getUint32(offset + 4, true);
    const virtualAddress = view.getUint32(offset + 8, true);
    const physicalAddress = view.getUint32(offset + 12, true);
    const fileSize = view.getUint32(offset + 16, true);
    let flashAddress = physicalAddress;
    if (flashAddress >= FLASH_BYTES && virtualAddress < FLASH_BYTES) flashAddress = virtualAddress;
    if (flashAddress >= FLASH_BYTES || fileOffset + fileSize > source.length) continue;
    const length = Math.min(fileSize, FLASH_BYTES - flashAddress);
    output.set(source.subarray(fileOffset, fileOffset + length), flashAddress);
    written += length;
    highest = Math.max(highest, flashAddress + length);
  }
  if (!written) throw new Error('ELF has no loadable AVR flash segment');
  return output.slice(0, highest);
}

async function decodeFirmware(file) {
  const buffer = await file.arrayBuffer();
  const name = file.name.toLowerCase();
  if (name.endsWith('.hex')) return parseIntelHex(new TextDecoder().decode(buffer));
  if (name.endsWith('.elf') || new Uint8Array(buffer, 0, Math.min(4, buffer.byteLength))[0] === 0x7f) return parseElf(buffer);
  if (buffer.byteLength > FLASH_BYTES) throw new Error(`Raw binary exceeds ${FLASH_BYTES} bytes`);
  if (!buffer.byteLength) throw new Error('Firmware file is empty');
  return new Uint8Array(buffer);
}

async function loadFirmware(file) {
  stopAudio();
  try {
    machine = new BitsynthMachine(await decodeFirmware(file));
    startAudio();
  } catch (error) {
    machine = null;
    console.error('Could not load firmware:', error);
  }
}

// Starts the firmware's audio; a blocked context resumes on the first click or key.
function startAudio() {
  if (!audioContext) {
    audioContext = new AudioContext({ latencyHint: 'interactive' });
    audioNode = audioContext.createScriptProcessor(AUDIO_BUFFER_SIZE, 0, 2);
    audioNode.onaudioprocess = (event) => {
      const left = event.outputBuffer.getChannelData(0);
      const right = event.outputBuffer.getChannelData(1);
      if (running) {
        try {
          machine.renderAudio(left, right);
          return;
        } catch (error) {
          running = false;
          queueMicrotask(stopAudio);
          console.error('Simulation stopped:', error);
        }
      }
      left.fill(0);
      right.fill(0);
    };
  }
  audioContext.resume().catch(() => {});
  running = true;
  audioNode.connect(audioContext.destination);
}

function stopAudio() {
  running = false;
  audioNode?.disconnect();
}

function unlockAudio() {
  if (running && audioContext.state !== 'running') audioContext.resume().catch(() => {});
}

async function loadDefaultFirmware() {
  try {
    const response = await fetch('../.pio/build/hardware/firmware.hex', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    await loadFirmware(new File([await response.blob()], 'firmware.hex'));
  } catch {
    setLcdLines('Click here to', 'load firmware');
  }
}

function updateCycleMeter() {
  if (ui.cycleMeter.hidden) return;
  const cycles = Math.round(machine?.audioIsrCycles || 0);
  const maxCycles = machine?.audioIsrMaxCycles || 0;
  const text = cycles
    ? `ISR ${cycles} / ${AUDIO_ISR_BUDGET_CYCLES} cycles (${Math.round(cycles / AUDIO_ISR_BUDGET_CYCLES * 100)}%) · MAX ${maxCycles}`
    : `ISR -- / ${AUDIO_ISR_BUDGET_CYCLES} cycles · MAX --`;
  if (ui.cycleMeter.textContent !== text) ui.cycleMeter.textContent = text;
  ui.cycleMeter.classList.toggle('over-budget', maxCycles > AUDIO_ISR_BUDGET_CYCLES);
}

document.addEventListener('keydown', (event) => {
  unlockAudio();
  if (event.repeat) return;
  const key = event.key.toLowerCase();
  const index = steps.findIndex((step) => step.key.toLowerCase() === key);
  if (index >= 0) {
    setStepPressed(index, true);
    event.preventDefault();
  } else if (key === 'm') {
    setModePressed(true);
    event.preventDefault();
  }
});

document.addEventListener('keyup', (event) => {
  const key = event.key.toLowerCase();
  const index = steps.findIndex((step) => step.key.toLowerCase() === key);
  if (index >= 0) setStepPressed(index, false);
  else if (key === 'm') setModePressed(false);
});

window.addEventListener('blur', () => {
  steps.forEach((_, index) => setStepPressed(index, false));
  setModePressed(false);
});
document.addEventListener('pointerdown', unlockAudio);

ui.firmwareFile.addEventListener('change', () => {
  const [file] = ui.firmwareFile.files;
  if (file) loadFirmware(file);
});

await readDiagram();
buildControls();
ui.cycleMeterScrew.addEventListener('click', () => {
  const show = ui.cycleMeter.hidden;
  ui.cycleMeter.hidden = !show;
  ui.cycleMeterScrew.setAttribute('aria-label', `${show ? 'Hide' : 'Show'} audio ISR cycle meter`);
  ui.cycleMeterScrew.setAttribute('aria-pressed', String(show));
  if (show && machine) {
    machine.audioIsrCycles = 0;
    machine.audioIsrMaxCycles = 0;
  }
  updateCycleMeter();
});
setInterval(updateCycleMeter, CYCLE_METER_UPDATE_MS);
scheduleLcdDotMatrix();
await loadDefaultFirmware();
