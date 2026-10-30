# BitSynth

BitSynth is a compact, three-mode wavetable synthesizer and eight-step sequencer for a 16 MHz ATmega328P. It is a reimagining of a project I first built about ten years ago: the goal is to keep the deliberately limited hardware, but extract as much musical range and usability from it as possible.

The same firmware runs on the physical instrument and in the browser simulator. It provides an eight-step sequencer, scale-quantized live playing, an eight-oscillator drone engine, PCM drums, a 16x2 LCD interface, and 9-bit PWM audio output.

## Current targets

- **Hardware:** Arduino Pro Mini, 5 V / 16 MHz, ATmega328P
- **Firmware build:** PlatformIO environment `hardware`
- **Browser simulator:** the physical front-panel view in `simulator/hardware.html`
- **Current `hardware` build footprint:** 29,040 bytes flash (94.5%) and 725 bytes RAM (35.4%)

An Uno or Nano with the same ATmega328P clock and voltage can also run the firmware if wired to the same pins.

## Setup and build

### Recommended development environment

Install:

1. [Visual Studio Code](https://code.visualstudio.com/)
2. The **PlatformIO IDE** VS Code extension

Alternatively, install PlatformIO Core and use the `pio` command directly. The only external firmware dependency is Arduino's `LiquidCrystal` library; PlatformIO installs the version declared in `platformio.ini` automatically. No Node.js packages are required.

Build the firmware:

```sh
pio run -e hardware
```

The relevant outputs are:

- `.pio/build/hardware/firmware.hex` for upload and automatic simulator loading
- `.pio/build/hardware/firmware.elf` for debugging or manual simulator loading

Upload to a connected board:

```sh
pio run -e hardware -t upload
```

A Pro Mini normally needs a suitable 5 V USB-to-serial adapter. If PlatformIO cannot select it automatically, set `upload_port` in `platformio.ini` or pass it on the command line.

## Browser simulator

The simulator executes the compiled ATmega328P firmware locally in the browser using AVR8js. It is buildless, but it must be opened through HTTP because it fetches the firmware and hardware description.

From the repository root:

```sh
python -m http.server 8000
```

Then open:

```text
http://localhost:8000/simulator/hardware.html
```

The page automatically loads `.pio/build/hardware/firmware.hex`. If that file is unavailable, click the LCD and select an ELF, Intel HEX, or raw BIN firmware file. Click or press a key once if the browser has suspended audio playback. The simulator imports the pinned AVR8js module from jsDelivr, so the first load requires an internet connection.

Simulator controls:

- Drag a knob vertically, or place the pointer over it and use the mouse wheel.
- Hold keys `1`-`8` for the eight step buttons.
- Hold `M` for **MODE / PANIC**.
- Every knob and button is also usable with the pointer.

The simulator models the real 9-bit, 31.25 kHz PWM signal and the documented passive output filter, not just the synth's digital sample stream. Firmware execution, LCD output, multiplexed controls, audio generation, and filtering all run in the browser.

## Hardware

Three CD74HC4067 multiplexers share the same address lines and provide the parameter pots, step buttons, and step pots.

| Function | Pin |
|---|---|
| MUX address bits 0, 1, 2 | `D8`, `D11`, `D10` |
| Four parameter pots | MUX to `A0` |
| Eight step buttons | MUX to `A1` |
| Eight step pots | MUX to `A2` |
| MODE / PANIC button | `A3` |
| 16x2 LCD (`RS`, `E`, `D4`-`D7`) | `D2`-`D7` |
| 9-bit PWM audio | `D9` / Timer1 channel A |

The KiCad project and a wiring reference are in `schematic/`.

### Analog audio output

`D9` is a PWM signal and must be filtered before it is connected to an audio input. The current circuit is a three-stage passive low-pass filter followed by AC coupling and a volume control:

```text
D9 PWM -- R1 2.2 kΩ --o-- R2 2.2 kΩ --o-- R3 2.2 kΩ --o-- (+) C4 4.7 µF --o-- VOL 50 kΩ log -- Rout 1 kΩ -- AUDIO OUT
                      |                |                |                         |          |
                   C1 4.7 nF       C2 4.7 nF       C3 6.8 nF                R4 100 kΩ    GND
                      |                |                |                         |
                     GND              GND              GND                       GND
```

The positive lead of electrolytic capacitor `C4` faces `R3`. Connect the top of the volume pot to the `C4/R4` node, the bottom to ground, and the wiper through `Rout` to the output. This is a high-impedance line output; do not drive a speaker or low-impedance headphones without an amplifier.

## User interface

### Global controls

A short tap on **MODE** cycles through:

```text
SEQ -> PLAY -> DRONE -> SEQ
```

Holding **MODE** for one second performs **PANIC**: it immediately stops the drum and normal voices, clears held PLAY notes, and disables every DRONE oscillator. In SEQ and PLAY, the sequencer clock continues and a later enabled step may produce sound again.

In PLAY and DRONE, **MODE** is also a shift key for secondary controls. Using a shifted control prevents that button press from changing mode.

### SEQ - eight-step sequencer

The sequence runs in sixteenth notes.

| Control | Function |
|---|---|
| Parameter 1 | Sequencer preset |
| Parameter 2 | Tempo, 60-220 BPM |
| Parameter 3 | Preset-specific `COLOR` |
| Parameter 4 | Preset-specific `SHAPE`; glide for `LEAD` |
| Step pots 1-8 | Chromatic pitch from C2 to B3; drum selection in `DRUM` |
| Step buttons 1-8 | Toggle the corresponding step on or off |

The LCD shows the preset, BPM, `COLOR`/`SHAPE`, and the gate pattern. `x` is an enabled step, `-` is disabled, and `O`/`o` marks the current enabled/disabled step.

| Preset | Voice structure | `COLOR` | `SHAPE` |
|---|---|---|---|
| `SUB` | Sine + square sub-octave | Detune | Decay |
| `BAS` | Saw + square sub-octave | Pitch sweep | Decay |
| `PLK` | Triangle + square octave | Detune | Decay |
| `BEL` | Sine harmonics + triangle | Inharmonic offset | Ring length |
| `LED` | Saw + reverse saw | Detune/spread | Glide |
| `CRD` | Four-layer chord | Minor / major / sus2 / sus4 | Decay |
| `CIP` | Two square waves one octave apart | Sample-and-hold / crush | Decay |
| `PRC` | Noise + low sine | Noise pitch and pitch sweep | Decay |
| `DRM` | Monophonic PCM drum engine | Playback tuning | Decay |

#### DRUM preset

Each step stores one of `KICK`, `SNAR`, or `CHH`; this is still a single eight-step track, not three separate drum tracks. A new hit immediately replaces the previous one.

The samples are signed 9-bit mono PCM at 20 kHz, losslessly bit-packed in flash. They are streamed directly from program memory without a RAM copy. `T` changes playback speed from 0.75x to 1.25x, while `D` applies a decay envelope lasting 25-100% of the pitch-adjusted sample duration.

### PLAY - scale-quantized live performance

PLAY reserves voice 0 for the monophonic background sequence and uses voices 1-3 for live notes.

| Control | Function |
|---|---|
| Parameter 1 | Live preset |
| Parameter 2 | Root note, C-B |
| Parameter 3 | Scale: `MAJ`, `MIN`, `PEN`, or `BLU` |
| Parameter 4 | Style: `POLY`, `CHRD`, `UP`, `DOWN`, or `UPDN` |
| Step pot 1 | Octave range, C2-C5 |
| Step pots 2-8 | Unused |
| Step buttons 1-8 | Consecutive notes of the selected scale |

Live presets are `PURE`, `SOFT`, `KEYS`, `PLUK`, `BASS`, `LEAD`, `ORGN`, and `LOFI`.

- `POLY` allows three simultaneous live notes. A fourth note steals the oldest voice.
- `CHRD` plays a scale-derived 1-3-5 chord from the most recently held button.
- `UP`, `DOWN`, and `UPDN` arpeggiate held buttons on the sequencer's sixteenth-note clock.

The background sequence keeps its position when entering PLAY. Hold **MODE** and press step button 1 to toggle it; the LCD shows `SEQ+` or `SEQ-`. Its clock continues while muted, so arpeggiators remain synchronized. A `DRUM` sequence uses the same reserved background slot and does not reduce the three live voices.

#### PLAY shifted controls

Hold **MODE** while moving a parameter pot:

| Control | Shifted function |
|---|---|
| Parameter 1 | Wavetable-morph triangle LFO: off or 0.1-5.0 Hz |
| Parameter 2 | Second waveform: off, sine, triangle, square, or saw |
| Parameter 3 | Shared live low-pass filter: off, then approximately 6.6 kHz to 101 Hz |
| Parameter 4 | Filter triangle LFO: off or 0.1-5.0 Hz |

With a second waveform but no morph LFO, only the second waveform is used. With the LFO enabled, all three live voices morph between the preset and second waveform at matching phase positions; no extra oscillator is required. The filter and both LFOs affect only the three live voices, never the background sequence.

The morph LFO is available only when a second waveform is selected, and the filter LFO only when the filter is enabled.

After shifted editing, soft takeover prevents the normal preset, root, scale, and style values from jumping. Each normal function becomes active again only after its knob is moved deliberately.

### DRONE - eight continuous oscillators

DRONE stops the sequencer and switches to a dedicated eight-oscillator mixer.

| Control | Function |
|---|---|
| Parameter 1 | Waveform for oscillators 1-2 |
| Parameter 2 | Waveform for oscillators 3-4 |
| Parameter 3 | Waveform for oscillators 5-6 |
| Parameter 4 | Waveform for oscillators 7-8 |
| Step pots 1-8 | Continuous, unquantized frequency from 80-600 Hz |
| Step buttons 1-8 | Toggle the corresponding oscillator |

Available waveforms are sine, triangle, square, and saw. Gates use short amplitude ramps to reduce switching clicks.

While holding **MODE**, parameter pots 1-4 set independent VCA triangle-LFO rates for oscillator pairs 1-2, 3-4, 5-6, and 7-8. Each ranges from off to 0.1-5.0 Hz. Soft takeover also applies when returning to waveform selection.

Leaving DRONE returns to SEQ and restarts the sequence from step 1.

## Architecture and critical implementation details

- **Real-time audio:** Timer2 runs the audio interrupt at 20 kHz. Timer1 generates 9-bit Fast PWM on `D9` at 31.25 kHz.
- **Synthesis:** 256-sample wavetables, phase accumulators, four envelope-driven normal voices, and a separate eight-oscillator drone path. Core tables and drum data live in flash.
- **Resource-aware mixing:** the drone engine recalculates one oscillator pair per audio interrupt. Each pair is therefore updated at 5 kHz while the PWM output remains at 20 kHz. This is an intentional quality/CPU trade-off for the 80-600 Hz range.
- **PLAY isolation:** morphing, its 2.5 kHz control-rate LFOs, and the shared one-pole low-pass filter operate only on live voices 1-3. Voice 0 remains a clean background sequencer voice.
- **PWM quality:** the internal mix uses additional fixed-point precision before conversion to 9-bit PWM, with quantization-error feedback and dither where appropriate.
- **Stable controls:** multiplexed analog inputs use settling reads, median sampling, smoothing, movement confirmation, and hysteresis. Buttons are debounced in software.
- **Efficient LCD updates:** a two-line shadow buffer writes only changed characters, avoiding visible flicker and unnecessary time in the control loop.
- **Simulator fidelity:** Timer1 mode 6 is modeled explicitly. PWM edges feed an exact state-space model of the RC ladder and AC-coupling stage, followed by oversampling and a four-pole anti-alias filter before Web Audio output.

## Limitations

- Settings and patterns are not saved to EEPROM; reset restores the compiled defaults.
- The normal synth engine has four voices. PLAY permanently assigns one to the background sequence.
- The PLAY filter is a shared, non-resonant one-pole filter.
- DRONE uses the pair-wise update strategy described above and has no normal per-note envelopes.
- Flash is intentionally used very tightly; check the PlatformIO size report after adding code or samples.

## Repository layout

```text
src/             Firmware, synth engine, wavetables, envelopes, and drum samples
simulator/       Browser front panel, AVR runtime, hardware mapping, and image assets
schematic/       KiCad schematic/PCB project and wiring reference
util/            Windows helpers for converting and previewing PCM sample headers
platformio.ini   Build target and library dependency
```

## License

This project is licensed under the MIT License. See `LICENSE`.
