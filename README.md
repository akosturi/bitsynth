# BitSynth

BitSynth is a compact, three-mode wavetable synthesizer and eight-step sequencer for a 16 MHz ATmega328P. It is a reimagining of a project I first built about ten years ago: the goal is to keep the deliberately limited hardware, but extract as much musical range and usability from it as possible.

The same firmware runs on the physical instrument and in the browser simulator. It provides an eight-step sequencer, scale-quantized live playing, an eight-oscillator drone engine, PCM drums, a 16x2 LCD interface, and 9-bit PWM audio output.

## Current targets

- **Hardware:** Arduino Pro Mini, 5 V / 16 MHz, ATmega328P
- **Firmware build:** PlatformIO environment `hardware`
- **Browser simulator:** the physical front-panel view in `simulator/hardware.html`
- **Current `hardware` build footprint:** 27,796 bytes flash (90.5%) and 724 bytes RAM (35.4%)

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

The simulator executes the compiled ATmega328P firmware locally in the browser using AVR8js. It is buildless; open `simulator/hardware.html` directly in a browser.

Alternatively, serve the repository over HTTP from its root:

```sh
python -m http.server 8000
```

Then open:

```text
http://localhost:8000/simulator/hardware.html
```

The page automatically loads `.pio/build/hardware/firmware.hex`. If that file is unavailable or the browser blocks local file access, click the LCD and select an ELF, Intel HEX, or raw BIN firmware file. Click or press a key once if the browser has suspended audio playback. The simulator imports the pinned AVR8js module from jsDelivr, so the first load requires an internet connection.

Simulator controls:

- Drag a knob vertically, or place the pointer over it and use the mouse wheel.
- Hold keys `1`-`8` for the eight step buttons.
- Tap `M` to change a SEQ/PLAY parameter page; hold it to change mode.
- Every knob and button is also usable with the pointer.

The simulator models the real 9-bit, 31.25 kHz PWM signal and the documented passive output filter, not just the synth's digital sample stream. Firmware execution, LCD output, multiplexed controls, audio generation, and filtering all run in the browser.

### Audio ISR cycle meter

Click the lower-right screw on the simulated front panel to reveal the audio ISR cycle meter. It shows the smoothed current ISR duration and its maximum, against the 800-cycle audio deadline. The screw does not change the firmware state or any control value.

## Hardware

Three CD74HC4067 multiplexers share the same address lines and provide the parameter pots, step buttons, and step pots.

| Function | Pin |
|---|---|
| MUX address bits 0, 1, 2 | `D8`, `D11`, `D10` |
| Four parameter pots | MUX to `A0` |
| Eight step buttons | MUX to `A1` |
| Eight step pots | MUX to `A2` |
| MODE button | `A3` |
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

A short tap on **MODE** advances to the next four-parameter control page in
SEQ and PLAY. A long press (about 650 ms) cycles through modes:

```text
SEQ -> PLAY -> DRONE -> SEQ
```

There is no PANIC gesture. Each new SEQ/PLAY page uses soft takeover, so pots
must move away from their position at the page change before they take effect.
In DRONE, hold **MODE** while turning a parameter pot to set that oscillator
pair's VCA-LFO rate. In PLAY, hold **MODE** and press step button 1 to toggle
the background sequence; using either SHIFT action prevents mode changing.

### SEQ - eight-step sequencer

The sequence runs in sixteenth notes.

| Control | Function |
|---|---|
| Parameter pots 1-4 | The four parameters on the selected control page |
| Step pots 1-8 | Chromatic pitch from C2 to B3; drum selection in `DRUM` |
| Step buttons 1-8 | Toggle the corresponding step on or off |

The SEQ pages are direct macros, macro LFO rate/depth pairs, then program,
tempo (60-220 BPM), and `DRM` playback tuning.

The LCD shows the preset, BPM, DRM playback tuning when applicable, and the gate pattern. `x` is an enabled step, `-` is disabled, and `O`/`o` marks the current enabled/disabled step.

SEQ contains `SIN`, `TRI`, `SAW`, `SQR`, `DRM`, and `NSE`. Melodic programs
store voice recipes, envelope length, pulse width, glide, and their macro/LFO
assignments. The PCM `DRM` program has its own compact data instead of unused
oscillator fields.

Melodic programs retain their stored PLAY VCA depth and rate and expose them
as direct macros (`VCD`, `VCR`). The waveform-morph engine is retained
internally without a current front-panel route.

Each preset also stores one square-wave pulse width. It is applied to every
square oscillator in that preset and otherwise has no effect; the stored value
is limited to a musically useful 5-95% duty cycle. The supplied `SQR` preset
uses a 35% pulse width, while the other presets retain the 50% default.

Each preset additionally stores one glide value (`0-127`). It applies to every
melodic voice of that preset, in both SEQ and PLAY; `0` switches glide off.

#### DRUM preset

Each step stores one of `KICK`, `SNAR`, or `CHH`; this is still a single eight-step track, not three separate drum tracks. A new hit immediately replaces the previous one.

The samples are signed 9-bit mono PCM at 20 kHz, losslessly bit-packed in flash. They are streamed directly from program memory without a RAM copy. Parameter 3 (`T`) changes playback speed from 0.75x to 1.25x. The selected DRM preset's `length` value applies a decay envelope lasting 25-100% of the pitch-adjusted sample duration.

### PLAY - scale-quantized live performance

PLAY reserves voice 0 for the monophonic background sequence and uses voices
1-3 for live notes.

| Control | Function |
|---|---|
| Parameter pots 1-4 | The four parameters on the selected control page |
| Step pot 1 | Octave range, C2-C5 |
| Step pots 2-4 | Unused |
| Step pots 5-8 | Unused |
| Step buttons 1-8 | Consecutive notes of the selected scale |

The PLAY pages are direct macros, macro LFO rate/depth pairs, then melodic
program, root note, scale, and style. `DRM` is intentionally SEQ-only.

Each PLAY program is melodic. Each voice recipe stores a signed
`detuneCents` pitch offset (`-1200` to `+1200`), so it can use any whole-cent
value from 1 to 1199 as well as semitone or octave intervals. Preset voices can
therefore be detuned by up to one octave in either direction. Offsets are
evaluated when a note is triggered and do not add oscillator layers or audio-
ISR work.

Preset modulation is a fixed 5 Hz triangle vibrato. Its stored `MOD` value is
neutral at 64; distance from 64 selects depth, so values above and below 64
produce the same centered pitch movement rather than an envelope-driven pitch
sweep.

- `POLY` allows three simultaneous live notes. A fourth note steals the oldest voice.
- `CHRD` plays a scale-derived 1-3-5 chord from the most recently held button.
- `UP`, `DOWN`, and `UPDN` arpeggiate held buttons on the sequencer's sixteenth-note clock.
- `CUP`, `CDWN`, and `CUDN` arpeggiate the 1-3-5 chord of the most recently held button upward, downward, or up then down, respectively, on that same clock.

The background sequence keeps its position when entering PLAY. Hold **MODE**
and press step button 1 to toggle it. A `DRUM` sequence uses its dedicated PCM
path and does not consume a normal voice.

#### Program macros and LFOs

Programs declare up to four direct macros and two optional LFO targets. The
LCD's upper row shows the current page's four three-character parameter names.
While a parameter is being adjusted, its field temporarily shows the value,
then returns to its name. In SEQ, the lower row remains the step-gate view; in
PLAY it remains the style/root/scale/octave view. A macro slot without an
assigned parameter shows `---`. The first LFO supplies rate/depth pair 1; the
second supplies pair 2. Rates use the same 0.05-20 Hz stepped range as DRONE.
Macro LFOs are resolved outside the audio interrupt;
the ISR receives only the resulting engine values. The current programs use
the first LFO for vibrato where assigned. Waveform morphing is currently not
available.

`NSE` is a generated white-noise source, not a wavetable. It uses an independent
16-bit LFSR for each normal voice, advances at the 20 kHz audio rate, and uses
the selected preset's envelope. Its pitch setting has no
effect. `NSE` is available in the shared SEQ/PLAY preset bank and as PLAY's
second waveform; it is intentionally unavailable in DRONE.

All parameter pages use the existing filtered, hysteretic pot handling and
soft takeover.

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

Available waveforms are sine, triangle, square, and saw. `NSE` is intentionally
unavailable in DRONE. Gates use short amplitude ramps to reduce switching clicks.

While holding **MODE**, parameter pots 1-4 set independent VCA triangle-LFO rates for oscillator pairs 1-2, 3-4, 5-6, and 7-8. Each offers off plus 94 rates: 0.05-0.20 Hz in 0.01 Hz steps, 0.2-5.0 Hz in 0.1 Hz steps, and 5-20 Hz in 0.5 Hz steps. The four pair VCAs use the same fine 9-bit linear amplitude scaling as PLAY. Soft takeover also applies when returning to waveform selection.

Leaving DRONE returns to SEQ and restarts the sequence from step 1.

## Wavetable utilities

The wavetables in `src/tables.h` can be replaced with custom waveforms. Audacity is recommended for preparing them: use a mono track with exactly 256 samples and export it as headerless little-endian signed 16-bit PCM RAW.

On Windows, set `RAW_TO_H_OUTPUT_BITS` to `9` near the top of `util/raw_to_h.bat`, then drag the `.raw` file onto the script. It creates a flash-ready `.h` file beside the source. Drag that generated header onto `util/sample_player.bat` to audition it. When copying the array into `src/tables.h`, retain the required table name, `PROGMEM` storage, 256-sample length, and signed 9-bit range.

## Architecture and critical implementation details

- **Real-time audio:** Timer2 runs the audio interrupt at 20 kHz. Timer1 generates 9-bit Fast PWM on `D9` at 31.25 kHz.
- **Synthesis:** 256-sample wavetables, phase accumulators, four envelope-driven normal voices, and a separate eight-oscillator drone path. Core tables and drum data live in flash.
- **Resource-aware mixing:** the drone engine recalculates one oscillator pair per audio interrupt. Each pair is therefore updated at 5 kHz while the PWM output remains at 20 kHz. This is an intentional quality/CPU trade-off for the 80-600 Hz range.
- **PWM quality:** the internal mix uses additional fixed-point precision before conversion to 9-bit PWM, with quantization-error feedback and dither where appropriate.
- **Stable controls:** multiplexed analog inputs use settling reads, median sampling, smoothing, movement confirmation, and hysteresis. Buttons are debounced in software.
- **Efficient LCD updates:** a two-line shadow buffer writes only changed characters, avoiding visible flicker and unnecessary time in the control loop.
- **Simulator fidelity:** Timer1 mode 6 is modeled explicitly. PWM edges feed an exact state-space model of the RC ladder and AC-coupling stage, followed by oversampling and a four-pole anti-alias filter before Web Audio output.

## Limitations

- Settings and patterns are not saved to EEPROM; reset restores the compiled defaults.
- The normal synth engine has four voices. PLAY permanently assigns one to the background sequence.
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
