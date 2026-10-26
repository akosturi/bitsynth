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
const PROGRAM_WORDS = 0x8000; // Matches AVR8js's ATmega328P demo allocation.
const AUDIO_OVERSAMPLE = 2;
const AUDIO_BUFFER_SIZE = 2048;
const TCCR1A = 0x80;
const TCCR1B = 0x81;
const OCR1AL = 0x88;
const OCR1AH = 0x89;

const FALLBACK_HARDWARE = {
  source: 'embedded fallback',
  settings: [0, 420, 511, 511].map((value, index) => ({
    id: `setting${index + 1}`,
    label: ['P1 PRESET', 'P2 BPM', 'P3 COLOR', 'P4 SHAPE'][index],
    value,
  })),
  steps: [0, 89, 178, 223, 312, 401, 489, 534].map((value, index) => ({
    id: `stepPot${index + 1}`,
    label: `STEP ${index + 1}`,
    value,
    key: String(index + 1),
  })),
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
  diagramStatus: $('#diagram-status'),
  engineStatus: $('#engine-status'),
  firmwareFile: $('#firmware-file'),
  firmwareName: $('#firmware-name'),
  dropZone: $('#drop-zone'),
  runButton: $('#run-button'),
  resetButton: $('#reset-button'),
  settingsGrid: $('#settings-grid'),
  stepsGrid: $('#steps-grid'),
  modeButton: $('#mode-button'),
  lcdLines: [$('#lcd-line-1'), $('#lcd-line-2')],
  volume: $('#volume'),
  runtimeNote: $('#runtime-note'),
  scope: $('#scope'),
};

let hardware = FALLBACK_HARDWARE;
let settings = [];
let steps = [];
let modePressed = false;
let firmwareImage = null;
let machine = null;
let audioContext = null;
let audioNode = null;
let running = false;
let audioFailureReported = false;

function connectionOtherEnd(connections, endpoint) {
  for (const connection of connections) {
    if (connection[0] === endpoint) return connection[1];
    if (connection[1] === endpoint) return connection[0];
  }
  return null;
}

function unoPin(connections, endpoint) {
  const other = connectionOtherEnd(connections, endpoint);
  if (!other?.startsWith('uno:')) return null;
  const pin = other.slice(4).split('.')[0];
  return /^\d+$/.test(pin) ? Number(pin) : pin;
}

async function readDiagram() {
  try {
    const response = await fetch('../wokwi/diagram.json', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const diagram = await response.json();
    const part = (id) => diagram.parts.find((item) => item.id === id);
    const label = (id, fallback) => part(id)?.attrs?.text || fallback;
    hardware = {
      source: 'wokwi/diagram.json',
      settings: Array.from({ length: 4 }, (_, index) => ({
        id: `setting${index + 1}`,
        label: label(`settingText${index + 1}`, `P${index + 1}`),
        value: Number(part(`setting${index + 1}`)?.attrs?.value ?? 0),
      })),
      steps: Array.from({ length: 8 }, (_, index) => ({
        id: `stepPot${index + 1}`,
        label: part(`stepButton${index + 1}`)?.attrs?.label || `STEP ${index + 1}`,
        value: Number(part(`stepPot${index + 1}`)?.attrs?.value ?? 0),
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
    ui.diagramStatus.textContent = 'diagram: connected';
  } catch (error) {
    hardware = FALLBACK_HARDWARE;
    ui.diagramStatus.textContent = 'diagram: fallback map';
    ui.diagramStatus.title = String(error);
  }
}

function makeKnob(control, small = false) {
  const card = document.createElement('div');
  card.className = 'control-card';
  const knob = document.createElement('div');
  knob.className = `knob${small ? ' small' : ''}`;
  knob.tabIndex = 0;
  knob.setAttribute('role', 'slider');
  knob.setAttribute('aria-label', control.label);
  knob.setAttribute('aria-valuemin', '0');
  knob.setAttribute('aria-valuemax', '1023');
  const name = document.createElement('div');
  name.className = 'control-name';
  name.textContent = control.label;
  const value = document.createElement('div');
  value.className = 'control-value';

  control.value = Math.max(0, Math.min(1023, Number(control.value) || 0));
  const render = () => {
    const angle = -135 + (control.value / 1023) * 270;
    knob.style.setProperty('--turn', `${angle}deg`);
    knob.setAttribute('aria-valuenow', String(control.value));
    value.textContent = `${Math.round((control.value / 1023) * 100)}%`;
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

  control.element = knob;
  card.append(knob, name, value);
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
  settings = hardware.settings.map((item) => ({ ...item }));
  steps = hardware.steps.map((item) => ({ ...item, pressed: false }));
  ui.settingsGrid.replaceChildren(...settings.map((control) => makeKnob(control)));
  const cards = steps.map((control, index) => {
    const card = makeKnob(control, true);
    const button = document.createElement('button');
    button.className = 'pad';
    button.setAttribute('aria-label', `${control.label}, key ${control.key}`);
    button.innerHTML = `<span class="keycap">${control.key}</span><span class="step-number">${index + 1}</span>`;
    control.button = button;
    bindMomentary(button, (pressed) => setStepPressed(index, pressed));
    card.append(button);
    return card;
  });
  ui.stepsGrid.replaceChildren(...cards);
  bindMomentary(ui.modeButton, setModePressed);
}

class HD44780 {
  constructor(render) {
    this.render = render;
    this.reset();
  }

  reset() {
    this.ddram = new Uint8Array(0x80).fill(32);
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
      this.paint();
    } else if (value === 0x02) {
      this.address = 0;
    } else if (value & 0x80) {
      this.address = value & 0x7f;
    } else if ((value & 0xfc) === 0x04) {
      this.increment = Boolean(value & 0x02);
    }
  }

  writeData(value) {
    this.ddram[this.address & 0x7f] = value;
    this.address = (this.address + (this.increment ? 1 : -1)) & 0x7f;
    this.paint();
  }

  paint() {
    const line = (offset) => Array.from(this.ddram.slice(offset, offset + 16), (code) =>
      code >= 32 && code <= 126 ? String.fromCharCode(code) : ' ').join('');
    this.render(line(0x00), line(0x40));
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
    this.state = new Float64Array(4); // v1, v2, v3, voltage across C4
    this.input = 0;
    this.lastCycle = 0;
    this.cache = new Map();
    // Build every possible step before audio starts, so coefficient creation
    // can never cause a real-time deadline miss in the first notes.
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
    return this.state[2] - this.state[3]; // Node after C4. Rout is unloaded by a high-Z input.
  }
}

// Bitsynth-specific Timer1 mode 6 model: 9-bit, single-slope Fast PWM,
// OCR1A double-buffered at BOTTOM, non-inverting output on OC1A/D9.
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
    this.high = false;
  }

  setOutput(high, cycle) {
    if (this.high === high) return;
    this.high = high;
    this.filter.setInputVoltage(high ? 5 : 0, cycle);
  }

  start(cycle, compare) {
    this.running = true;
    this.lastCycle = cycle;
    this.phase = 0;
    this.activeCompare = Math.min(compare, 511);
    this.pendingCompare = this.activeCompare;
    this.hasPendingCompare = false;
    if (this.enabled) this.setOutput(true, cycle);
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
    if (enabled) this.setOutput(this.activeCompare === 511 || this.phase <= this.activeCompare, cycle);
    else this.setOutput(portHigh, cycle);
  }

  cyclesUntil(eventPhase) {
    const distance = (eventPhase - this.phase + 512) % 512;
    return distance || 512;
  }

  advanceTo(targetCycle) {
    if (!this.running || targetCycle <= this.lastCycle) return;
    while (this.lastCycle < targetCycle) {
      const clearPhase = Math.min(this.activeCompare + 1, 511);
      let distance = this.cyclesUntil(0);
      if (this.enabled && this.activeCompare < 511) distance = Math.min(distance, this.cyclesUntil(clearPhase));
      if (this.lastCycle + distance > targetCycle) {
        const elapsed = targetCycle - this.lastCycle;
        this.filter.integrateTo(targetCycle);
        this.phase = (this.phase + elapsed) % 512;
        this.lastCycle = targetCycle;
        return;
      }

      this.lastCycle += distance;
      this.phase = (this.phase + distance) % 512;
      this.filter.integrateTo(this.lastCycle);
      if (this.phase === 0) {
        if (this.hasPendingCompare) {
          this.activeCompare = this.pendingCompare;
          this.hasPendingCompare = false;
        }
        if (this.enabled) this.setOutput(true, this.lastCycle);
      } else if (this.enabled && this.activeCompare < 511 && this.phase === this.activeCompare + 1) {
        this.setOutput(false, this.lastCycle);
      }
    }
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

class MonitorAntiAliasFilter {
  constructor(outputSampleRate) {
    const oversampledRate = outputSampleRate * AUDIO_OVERSAMPLE;
    const cutoff = Math.min(18_000, outputSampleRate * 0.4);
    // Two Butterworth sections form a flat 4-pole low-pass. This models the
    // anti-alias stage of the browser/audio interface, not part of the synth.
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
    this.timer2 = new AVRTimer(this.cpu, timer2Config);
    this.portB = new AVRIOPort(this.cpu, portBConfig);
    this.portC = new AVRIOPort(this.cpu, portCConfig);
    this.portD = new AVRIOPort(this.cpu, portDConfig);
    this.adc = new AVRADC(this.cpu, adcConfig);
    this.filter = new OutputFilter();
    this.pwm = new FastPwm9(this.filter);
    this.lcd = new HD44780((line1, line2) => {
      ui.lcdLines[0].textContent = line1;
      ui.lcdLines[1].textContent = line2;
    });
    this.portValues = { B: 0, C: 0, D: 0 };
    this.audioHigh = false;
    this.scope = new Float32Array(2048);
    this.scopeIndex = 0;
    this.sampleCycle = 0;
    this.previewBuild = false;
    this.antiAlias = null;
    this.antiAliasSampleRate = 0;

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

  isPreviewBuild() {
    const tccr1a = this.cpu.data[TCCR1A];
    const tccr1b = this.cpu.data[TCCR1B];
    return !(tccr1a & 0xc0) || !(tccr1b & 0x07);
  }

  adcVoltage(control) {
    // The real front panel pots are wired opposite to Wokwi's visual value.
    // WOKWI_SIMULATION firmware already performs this inversion itself.
    const raw = this.isPreviewBuild() ? control.value : 1023 - control.value;
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

  renderAudio(outputLeft, outputRight, volume) {
    const outputSampleRate = audioContext.sampleRate;
    if (!this.antiAlias || this.antiAliasSampleRate !== outputSampleRate) {
      this.antiAlias = new MonitorAntiAliasFilter(outputSampleRate);
      this.antiAliasSampleRate = outputSampleRate;
    }
    const cyclesPerSubsample = CLOCK_HZ / (outputSampleRate * AUDIO_OVERSAMPLE);
    for (let index = 0; index < outputLeft.length; index++) {
      let sample = 0;
      for (let subSample = 0; subSample < AUDIO_OVERSAMPLE; subSample++) {
        this.sampleCycle += cyclesPerSubsample;
        while (this.cpu.cycles < this.sampleCycle) {
          avrInstruction(this.cpu);
          this.cpu.tick();
        }
        this.pwm.advanceTo(this.cpu.cycles);
        const voltage = this.filter.sample(this.cpu.cycles);
        sample = this.antiAlias.process(voltage / 2.5);
      }
      const monitored = Math.max(-1, Math.min(1, sample * volume));
      outputLeft[index] = monitored;
      outputRight[index] = monitored;
      this.scope[this.scopeIndex++ & (this.scope.length - 1)] = monitored;
    }
    this.previewBuild = this.isPreviewBuild();
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
  return { bytes: bytes.slice(0, highest), detail: `${written.toLocaleString()} flash bytes` };
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
  return { bytes: output.slice(0, highest), detail: `${written.toLocaleString()} flash bytes` };
}

async function decodeFirmware(file) {
  const buffer = await file.arrayBuffer();
  const name = file.name.toLowerCase();
  if (name.endsWith('.hex')) return parseIntelHex(new TextDecoder().decode(buffer));
  if (name.endsWith('.elf') || new Uint8Array(buffer, 0, Math.min(4, buffer.byteLength))[0] === 0x7f) return parseElf(buffer);
  if (buffer.byteLength > FLASH_BYTES) throw new Error(`Raw binary exceeds ${FLASH_BYTES} bytes`);
  if (!buffer.byteLength) throw new Error('Firmware file is empty');
  return { bytes: new Uint8Array(buffer), detail: `${buffer.byteLength.toLocaleString()} raw bytes` };
}

async function loadFirmware(file) {
  stopAudio();
  try {
    const decoded = await decodeFirmware(file);
    firmwareImage = { ...decoded, name: file.name };
    machine = new BitsynthMachine(decoded.bytes);
    ui.firmwareName.textContent = file.name;
    ui.engineStatus.textContent = 'firmware: ready';
    ui.engineStatus.className = 'badge';
    ui.runtimeNote.textContent = `${decoded.detail}. Press Start audio.`;
    ui.runButton.disabled = false;
    ui.resetButton.disabled = false;
    ui.runButton.textContent = 'Start audio';
  } catch (error) {
    firmwareImage = null;
    machine = null;
    ui.firmwareName.textContent = 'Could not load firmware';
    ui.engineStatus.textContent = 'firmware: error';
    ui.engineStatus.className = 'badge error';
    ui.runtimeNote.textContent = error instanceof Error ? error.message : String(error);
    ui.runButton.disabled = true;
    ui.resetButton.disabled = true;
  }
}

async function ensureAudio() {
  if (!audioContext) audioContext = new AudioContext({ latencyHint: 'interactive' });
  await audioContext.resume();
  if (!audioNode) {
    audioNode = audioContext.createScriptProcessor(AUDIO_BUFFER_SIZE, 0, 2);
    audioNode.onaudioprocess = (event) => {
      const left = event.outputBuffer.getChannelData(0);
      const right = event.outputBuffer.getChannelData(1);
      if (!running || !machine) {
        left.fill(0);
        right.fill(0);
        return;
      }
      try {
        machine.renderAudio(left, right, Number(ui.volume.value));
      } catch (error) {
        left.fill(0);
        right.fill(0);
        if (!audioFailureReported) {
          audioFailureReported = true;
          queueMicrotask(() => reportRuntimeError(error));
        }
      }
    };
  }
}

async function startAudio() {
  if (!machine) return;
  await ensureAudio();
  running = true;
  audioFailureReported = false;
  audioNode.connect(audioContext.destination);
  ui.runButton.textContent = 'Stop';
  ui.engineStatus.textContent = 'engine: running';
  ui.engineStatus.className = 'badge live';
}

function stopAudio() {
  running = false;
  if (audioNode) {
    try { audioNode.disconnect(); } catch { /* already disconnected */ }
  }
  if (firmwareImage) {
    ui.runButton.textContent = 'Start audio';
    ui.engineStatus.textContent = 'engine: paused';
    ui.engineStatus.className = 'badge';
  }
}

function reportRuntimeError(error) {
  stopAudio();
  ui.engineStatus.textContent = 'engine: fault';
  ui.engineStatus.className = 'badge error';
  ui.runtimeNote.textContent = error instanceof Error ? error.message : String(error);
}

function resetMachine() {
  if (!firmwareImage) return;
  const wasRunning = running;
  stopAudio();
  machine = new BitsynthMachine(firmwareImage.bytes);
  ui.runtimeNote.textContent = `${firmwareImage.detail}. Reset complete.`;
  if (wasRunning) startAudio().catch(reportRuntimeError);
}

function drawScope() {
  const canvas = ui.scope;
  const rect = canvas.getBoundingClientRect();
  const scale = Math.min(2, devicePixelRatio || 1);
  const width = Math.max(1, Math.round(rect.width * scale));
  const height = Math.max(1, Math.round(rect.height * scale));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const context = canvas.getContext('2d');
  context.clearRect(0, 0, width, height);
  context.strokeStyle = '#1c332a';
  context.lineWidth = 1;
  context.beginPath();
  context.moveTo(0, height / 2);
  context.lineTo(width, height / 2);
  context.stroke();
  if (machine) {
    context.strokeStyle = '#72e4d1';
    context.lineWidth = Math.max(1, scale);
    context.beginPath();
    const visible = Math.min(machine.scope.length, Math.round(width / scale) * 2);
    for (let index = 0; index < visible; index++) {
      const ringIndex = (machine.scopeIndex - visible + index) & (machine.scope.length - 1);
      const x = (index / (visible - 1)) * width;
      const y = height * (0.5 - machine.scope[ringIndex] * 0.42);
      if (index === 0) context.moveTo(x, y);
      else context.lineTo(x, y);
    }
    context.stroke();
  }
  if (running && machine) {
    const mode = machine.previewBuild ? '1-bit Wokwi preview' : '9-bit Fast PWM · 31.25 kHz';
    ui.runtimeNote.textContent = `${mode} · ${audioContext.sampleRate.toLocaleString()} Hz monitor · local execution`;
  }
  requestAnimationFrame(drawScope);
}

function isTextEntry(target) {
  return target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target?.isContentEditable;
}

document.addEventListener('keydown', (event) => {
  if (isTextEntry(event.target) || event.repeat) return;
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

ui.firmwareFile.addEventListener('change', () => {
  const [file] = ui.firmwareFile.files;
  if (file) loadFirmware(file);
});
for (const type of ['dragenter', 'dragover']) {
  ui.dropZone.addEventListener(type, (event) => {
    event.preventDefault();
    ui.dropZone.classList.add('drag');
  });
}
for (const type of ['dragleave', 'drop']) {
  ui.dropZone.addEventListener(type, (event) => {
    event.preventDefault();
    ui.dropZone.classList.remove('drag');
  });
}
ui.dropZone.addEventListener('drop', (event) => {
  const [file] = event.dataTransfer.files;
  if (file) loadFirmware(file);
});
ui.runButton.addEventListener('click', () => {
  if (running) stopAudio();
  else startAudio().catch(reportRuntimeError);
});
ui.resetButton.addEventListener('click', resetMachine);

await readDiagram();
buildControls();
drawScope();
