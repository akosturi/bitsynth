# BitSynth

BitSynth is a compact, three-mode wavetable synthesizer and eight-step sequencer for a 16 MHz ATmega328P. It is a reimagining of a project I first built about ten years ago: the goal is to keep the deliberately limited hardware, but extract as much musical range and usability from it as possible.

The same firmware runs on the physical instrument and in the browser simulator. It provides an eight-step sequencer with optional generative variation, scale-quantized live playing, an eight-oscillator drone engine, PCM drums, a 16x2 LCD interface, and 9-bit PWM audio output.

## Current targets

- **Hardware:** Arduino Pro Mini, 5 V / 16 MHz, ATmega328P
- **Firmware build:** PlatformIO environment `hardware`
- **Browser simulator:** the physical front-panel view in `simulator/hardware.html`
- **Last verified `hardware` build footprint:** 30,018 bytes flash (97.7%) and 1,084 bytes RAM (52.9%)

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
- Tap `M` to change a SEQ/PLAY parameter page; double-tap it to change mode. Hold `M` with `1` or `8` for the MODE + step-button combinations.
- Every knob and button is also usable with the pointer.

The simulator models the real 9-bit, 31.25 kHz PWM signal and the documented passive output filter, not just the synth's digital sample stream. Firmware execution, LCD output, multiplexed controls, audio generation, and filtering all run in the browser.

### Audio ISR cycle meter

Click the lower-right screw on the simulated front panel to reveal the audio ISR cycle meter. It shows the smoothed audio interrupt load in cycles per 20 kHz sample period and the highest average over one eight-sample render block, against the 800-cycle budget. Both audio interrupts count, and a sample-output interrupt nested in block rendering is counted once. The screw does not change the firmware state or any control value.

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

On power-up, BitSynth opens directly in SEQ.

One short **MODE** tap advances to the next four-parameter control page in SEQ
and PLAY after the 350 ms double-tap window. Two short taps within that window
cycle through modes:

```text
SEQ -> PLAY -> DRONE -> SEQ
```

Holding **MODE** by itself does nothing. It remains a modifier for the documented
MODE combinations, including PLAY's MODE + step button 1 background-sequence toggle
and the SEQ/PLAY MODE + step button 8 `GEN` toggle.

There is no PANIC gesture. Each new SEQ/PLAY page uses soft takeover, so pots
must move away from their position at the page change before they take effect.
In DRONE, hold **MODE** while turning a parameter pot to set that oscillator
pair's VCA-LFO rate. In PLAY, hold **MODE** and press step button 1 to toggle
the background sequence. In SEQ and PLAY, hold **MODE** and press step button 8
to toggle `GEN`. None of these MODE combinations counts as a tap gesture.

### SEQ - eight-step sequencer

The sequence runs in sixteenth notes. A disabled step fades the previous
step's notes or drum hit out in about 20 ms instead of cutting them, so rests
do not click.

| Control | Function |
|---|---|
| Parameter pots 1-4 | The four parameters on the selected control page |
| Step pots 1-8 | Chromatic pitch from C2 to B3; drum selection in `DRUM` |
| Step buttons 1-8 | Toggle the corresponding step on or off |

SEQ packs its available parameters left-to-right into pages of four: program,
tempo (60-190 BPM at 1 BPM resolution), then the selected program's direct macros and LFO
rate/depth pairs. `DRM` additionally exposes playback tuning after tempo; it
does not appear for melodic programs. Empty macro and LFO slots are skipped,
so no completely empty parameter page is shown. The `DRM` direct macro is
`DEC` (decay).

The LCD's upper row shows the selected control page's parameter names, except program and tempo, which always show the selected program and BPM value, and briefly (about 1.5 s) shows values when they change. The lower row shows the gate pattern: `x` is an enabled step, `-` is disabled, and `O`/`o` marks the current enabled/disabled step. Turning a step pot briefly shows its step number and note name, or drum name in `DRM`, at the lower right. A `×` after the gate pattern shows that `GEN` is on.

#### GEN - generative variation

`GEN` slowly varies the melody of a melodic sequence while it runs, including
as PLAY's background sequence. It has no parameters; MODE + step button 8
switches it on or off. At the start of every eight-step cycle, it may change
at most one step: that step takes the note of another enabled step, moves an
octave up or back, or returns to its own note. Step 1 never changes, gates and
rhythm are untouched, and only notes already in enabled steps are used, so the
sequence keeps its pitch classes and key. Turning a step pot returns that step
to its own note. Switching `GEN` on or off restores the programmed sequence.
`DRM` is unaffected.

#### Programs

SEQ contains twelve melodic programs followed by `DRM`; PLAY contains the same
melodic programs but omits `DRM`. A melodic program's letter names its
category (`A` bass, `B` lead, `C` pad, `D` keys, `E` sequence and effect) and the digits
number it within that category. The references describe the character each
program aims at within this engine's limits (one shared filter envelope),
not a copy of the original instrument.

| Program | Sound | Voices | Macros | LFOs (default) |
|---|---|---|---|---|
| `A01` | Funk bass in the style of "Thriller": two slowly beating saws over a square sub-octave, punchy decay | 3 | `FLT` `RES` `LEN` `ENV` `GLD` `FEG` | - |
| `A02` | Reese bass: two saws 28 cents apart over a sine sub-octave | 3 | `FLT` `RES` `LEN` `ENV` `GLD` | `FLT` (0.3 Hz, 12) |
| `A03` | 808 bass: a long sine that slides between notes | 1 | `GLD` `LEN` `ENV` | - |
| `B01` | Pulse lead in the style of "Axel F": 30% pulse and detuned saw, one octave up | 2 | `PWM` `FLT` `RES` `LEN` `ENV` `GLD` `VIB` `FEG` | - |
| `B02` | Supersaw trance lead (MicroKorg/JP-8000 style): three spread saws one octave up over one at pitch, chorus 1, glide | 4 | `FLT` `RES` `GLD` `CHR` `VIB` `LEN` `ENV` `VCD` | `FLT` (0.10 Hz, off) |
| `C01` | Juno-style PWM pad with chorus 1 and a soft attack | 1 | `PWM` `FLT` `RES` `CHR` `LEN` `ENV` | `PWM` (0.5 Hz, 49), `FLT` (0.10 Hz, off) |
| `C02` | String machine in the style of Jarre's "Oxygène": saw ensemble with chorus 2 and light vibrato | 1 | `FLT` `RES` `CHR` `VIB` `LEN` `ENV` | `FLT` (0.12 Hz, 16) |
| `D01` | Saw poly brass in the style of "Jump", for chords | 1 | `FLT` `RES` `CHR` `LEN` `ENV` `GLD` `VIB` `FEG` | - |
| `D02` | Vibraphone or suitcase electric piano: struck triangle with 5.5 Hz motor tremolo | 1 | `VCD` `VCR` `LEN` `ENV` `FLT` `RES` `CHR` | - |
| `D03` | Rock organ: 8', 4', 5 1/3', and 16' sine drawbars with key click, 6 Hz Leslie-like tremolo, vibrato, and chorus 2 | 4 | `VCR` `VCD` `VIB` `CHR` `FLT` `RES` `LEN` `ENV` | - |
| `E01` | Dark arpeggio in the style of "Stranger Things": a saw and a detuned 40% pulse morphing into each other while the filter slowly opens and closes | 2 | `FLT` `RES` `MOR` `PWM` `CHR` `LEN` `ENV` `GLD` | `FLT` (0.10 Hz, 40), `PWM` (0.7 Hz, 28) |
| `E02` | Surf in the style of Jarre's "Oxygène": held white noise whose filter and slower 0.1 Hz VCA swell drift against each other | 1 | `FLT` `RES` `VCD` `VCR` `LEN` `ENV` `CHR` | `FLT` (0.12 Hz, 45) |

LFO defaults are listed as rate and depth; "off" is depth 0, so only its rate
is preset. Single-voice programs keep note polyphony in PLAY, so `A03`, `C01`,
`C02`, `D01`, and `D02` suit `POLY` and `CHRD`; the multi-voice programs
play one unison stack per note. `C01` and `C02` swell in over about 0.8 s and
are meant for PLAY: in SEQ every enabled step restarts their attack, so lower
`LEN` there. For the `E01` theme-style arpeggio, hold step buttons 1, 3, 5, 7,
and 8 in PLAY with root `C`, `MAJ`, and `UPDN`. `E02` ignores pitch; with
`ENV` 0 and a short `LEN` it plays hi-hat-like noise ticks in SEQ. The PCM
`DRM` program has its own compact data instead of unused oscillator fields.

Melodic programs also store a VCA depth and rate. The VCA processes the
normal mix in SEQ and the live voices in PLAY; `D02`, `D03`, and `E02` enable
it by default, while `B02` stores an 8 Hz rate for its `VCD` macro. `MOR` is the
third macro on `E01`. `MOR` controls waveform-morph speed: `0` is off, while
`1-127` span 0.16-20.32 Hz. A multi-voice preset crossfades only the first two
waveforms in its voice recipes (`A-B-A`); a single-voice preset does not morph.
Morphing crossfades waveform sources only, so each voice keeps its own pitch,
envelope, modulation, and pulse-width settings. While PLAY's background
sequence runs, its voice 0 is neither morphed nor affected by the VCA.

Each preset also stores one square-wave pulse width. It is applied to every
square oscillator in that preset and otherwise has no effect; the stored value
is limited to a musically useful 5-95% duty cycle. `B01` uses 30% and `E01`
40%; the other presets use 50%.

Each melodic preset stores one default glide value (`0-127`) that applies in
both SEQ and PLAY; `0` switches glide off.

Output level is set automatically whenever a note or drum hit starts. One
voice or the `DRM` sample can fill the whole 9-bit PWM range, and the voices
that can sound at the same time share full scale equally, so the mix cannot
clip. In SEQ, one-voice presets and `DRM` therefore play at full scale,
two-voice presets at half per voice, three-voice presets at one third, and
four-voice presets at one quarter. In PLAY, a running background sequence
keeps one quarter and the live voices share the rest: single-voice `POLY`
uses one quarter per note, a unison stack (in `POLY` or an arpeggio) and
`CHRD` divide their share by their voice count, and single-voice arpeggios
use the whole share (full scale, or three quarters with the background
sequence running). Each melodic preset and the `DRM`
program also store a level (`0-255`) that only attenuates this automatic
level; every supplied program uses `255`, the full automatic level.

`CHR` selects a mono Juno-60-style chorus. In SEQ it follows the normal mix;
in PLAY it processes the live voices, leaving voice 0 dry while it plays the
background sequence. `0` is off, `1` is chorus 1, and `2` is chorus 2. Both modes use a 16-bit,
20 kHz delay line swept by a triangle LFO from 1.66 to 5.35 ms, with no
feedback and a 50% wet mix. Chorus 1 runs at 0.513 Hz and chorus 2 at 0.863
Hz. DRONE bypasses the effect.

Each melodic preset also stores a low-pass filter cutoff (`0-127`) and
resonance (`0-127`); `FLT` and `RES` adjust them as direct macros. It is a
resonant two-pole, 12 dB/octave state-variable filter, placed before the VCA
and the chorus on the same signal path: in SEQ it filters the normal mix, in
PLAY the live voices, while the background sequence's voice 0 stays dry.
`0-126` sweep exponentially from about 100 Hz to 3.3 kHz, and `127` leaves the
filter open. Toward the top of the range a low-resonance setting becomes
almost transparent, so the step to `127` is small. `RES` sweeps the resonance
exponentially from a gentle Q of about 1 (`0`) to about 6 (`127`); it does not
self-oscillate and has no effect while the filter is open. The mix has no
headroom, so resonance splits the difference between level and distortion:
it lowers the passband by about 2 dB at `RES` `27`, 4 dB at `64`, and 6.5 dB
at `127`, while the resonant peak rises by up to about 1.5, 4, and 9 dB. Wherever
that peak, or a fast transient, would pass full scale, the filter output is
clipped, so strong harmonics near a resonant cutoff add audible distortion,
most at high `RES`. `A03` and `D03` store cutoff `127`; the other presets
store a cutoff between `52` and `101` and a resonance between `0` and `48`.
Cutoff and resonance changes glide over at most about 0.1 s and 0.05 s, so
moving the controls or switching presets does not click; very fast, deep LFO
sweeps are smoothed by the same limit. `DRM` and DRONE bypass the filter.

Each melodic preset also stores a filter envelope amount (`0-127`, `0` off),
which `FEG` adjusts as a direct macro. Every note start restarts the filter
envelope: it jumps to full level and then decays smoothly to zero in a
quarter of the note's `LEN` time, whatever its `ENV` shape, so the filter
closes while the note is still loud, as with a short filter decay on a larger
synthesizer. It raises the cutoff toward the filter's brightest setting by
the envelope level times the amount. `127` sweeps the envelope's
peak to about 3.3 kHz from any cutoff, and lower values give a finer,
quadratic response, so `64` covers about a quarter of the distance. The
envelope bypasses the cutoff glide, so its attack is immediate, and when it
has decayed the filter rests at the `FLT` cutoff. There is one filter, so the
filter envelope is shared: in PLAY each new live note or arpeggio step
restarts it for all sounding live notes, while the background sequence never
restarts it. With `LEN` HOLD the envelope decays in about 0.5 s. It has no effect while the filter is open, and it is not an LFO target.
`A01` stores `88`, `B01` `80`, and `D01` and `E01` `64`; the other presets
store `0`. `E01` has no free macro slot, so its amount is fixed.

#### DRUM preset

Each step stores one of `KICK`, `SNAR`, or `CHH`; this is still a single eight-step track, not three separate drum tracks. A new hit immediately replaces the previous one.

The samples are unsigned (offset-binary) 9-bit mono PCM at 20 kHz, losslessly bit-packed in flash. They are streamed directly from program memory without a RAM copy. Parameter 3 (`T`) changes playback speed from 0.75x to 1.25x. The direct-macro page's `DEC` parameter applies a decay envelope lasting 25-100% of the pitch-adjusted sample duration.

### PLAY - scale-quantized live performance

PLAY reserves voice 0 for the monophonic background sequence and uses voices
1-3 for live notes.

| Control | Function |
|---|---|
| Parameter pots 1-4 | The four parameters on the selected control page |
| Step pot 1 | Octave range, O1-O5 (C1-C5) |
| Step pot 2 | Background-sequence and arpeggio tempo, 60-190 BPM; shared with SEQ, so it takes effect only after moving once PLAY is entered |
| Step pot 3 | Arpeggio division: `1/2`, `1/4`, `1/4t`, `1/8`, `1/8t`, `1/16`, `1/16t`, `1/32` |
| Step pot 4 | Unused |
| Step pots 5-8 | Unused |
| Step buttons 1-8 | Consecutive notes of the selected scale; MODE + button 1 toggles the background sequence, MODE + button 8 toggles `GEN` |

PLAY packs program, root note, scale, style, then its available direct macros
and LFO rate/depth pairs into four-control pages; empty slots are skipped.
`DRM` is intentionally SEQ-only.

Each PLAY program is melodic. Each voice recipe stores a signed
`detuneCents` pitch offset (`-2400` to `+2400`), so it can use any whole-cent
value as well as semitone, octave, and two-octave intervals. In `POLY`, a
multi-voice preset is played as a single detuned unison stack with each
recipe's waveform, detune, and modulation, using up to four oscillators (or
three with the background sequence running). The arpeggio styles play each
arpeggio note as that same stack. Single-voice presets retain note polyphony
in `POLY`. `CHRD` uses only the first voice recipe.

Preset modulation is a fixed 5 Hz triangle vibrato. Its stored `MOD` value is
neutral at 64; distance from 64 selects depth, so values above and below 64
produce the same centered pitch movement rather than an envelope-driven pitch
sweep.

- `POLY` allows four simultaneous live notes while the background sequence is
  off. With it on, voice 0 remains reserved for the sequence, so POLY allows
  three notes; the next note steals the oldest live voice in either case.
- `CHRD` plays a scale-derived 1-3-5 chord from the most recently held button,
  using the preset's first voice recipe.
- `UP`, `DOWN`, and `UPDN` arpeggiate held buttons at step pot 3's division of the sequencer clock, including triplets; every division restarts in phase with sequencer step 1. Button presses and releases are applied at the next clock, so they cannot retrigger an off-grid note.
- `CUP`, `CDWN`, and `CUDN` arpeggiate the 1-3-5 chord of the most recently held button upward, downward, or up then down, respectively, on that same clock; button changes are likewise clock-quantized.

A live note-off does not cut the voice: it fades from its current level to
silence in about 20 ms, so releasing a held or still-decaying note does not
click. Background-sequence rests fade the same way. Mode changes stop voices
immediately.

The background sequence keeps its position when entering PLAY. Hold **MODE**
and press step button 1 to toggle it. A `DRUM` sequence uses its dedicated PCM
path and does not consume a normal voice.

#### Program macros and LFOs

Programs declare up to eight direct macros and two optional LFO targets, so a
fully assigned program spans up to four parameter pages. Melodic programs also
store each LFO's default rate and depth, which are loaded with the program;
`DRM` LFOs start at depth 0. SEQ
and PLAY keep separate macro and LFO values for their selected programs, and
PLAY's background sequence uses the SEQ program's values. The
LCD's upper row shows the current page's four three-character parameter names;
program, tempo, and PLAY's root note, scale, and style, show their current
values instead (for example `KEY C#  MIN UPDN`). While a parameter is being adjusted, its
field shows the value for about 1.5 s, then returns to its name. In SEQ, the lower row remains the step-gate view; in
PLAY it shows background-sequence status (`*` running, `×` running with
`GEN`, blank off) in its first character, then octave, tempo, and arpeggio
division. The HD44780 character ROM has no `×`, so it is a custom CGRAM
character. A macro slot without an
assigned parameter shows `---`. The first LFO supplies rate/depth pair 1; the
second supplies pair 2. Their LCD rate values are steps 1-94, corresponding to
the same 0.05-20 Hz range as DRONE.
Macro LFOs are resolved outside the audio interrupt;
the ISR receives only the resulting engine values. An LFO may target any
numeric macro declared by its program (`PWM`, `VIB`, `TUN`, `DEC`, `VCD`,
`VCR`, `FLT`, or `RES`); depth sets a centred triangle sweep, with 127 spanning that parameter's
complete valid range before clamping. Drum tune and decay are resolved when a
new hit starts. The melodic programs also expose `ENV` and `LEN` direct macros
for selecting one of the six envelopes and its playback speed: `0` a fast
exponential decay, `1` a smooth decay, `2` a pad shape with a soft attack over
the first quarter and a long sustain, `3` constant level, `4` full level that
then fades linearly, and `5` a strike that drops to about 40% and then decays
slowly. Envelopes `0`, `1`, `3`, `4`, and `5` start at full level. `CHR` is a
direct-only macro and is not an LFO target. `MOR` is also direct-only: it
sets a preset's waveform-morph speed and is inactive for one-voice presets.
`LEN` uses a
logarithmic range from roughly 0.2 s (0) to 6.5 s (126); `LEN` 127 is HOLD and
uses the constant full-level envelope until note-off. HOLD sits next to the
longest length, so turning away from it shortens held notes gradually. Without a `LEN` macro a
program plays its stored envelope length unchanged. Neither envelope macro is
an LFO target.

`NSE` is a generated white-noise source, not a wavetable. It uses an independent
16-bit LFSR for each normal voice, advances at the 20 kHz audio rate, and uses
the selected preset's envelope. Its pitch setting has no
effect. `E02` uses it in the shared SEQ/PLAY preset bank; it is intentionally
unavailable in DRONE.

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

While holding **MODE**, parameter pots 1-4 set independent VCA triangle-LFO rates for oscillator pairs 1-2, 3-4, 5-6, and 7-8. Each offers off plus 94 rates: 0.05-0.20 Hz in 0.01 Hz steps, 0.2-5.0 Hz in 0.1 Hz steps, and 5-20 Hz in 0.5 Hz steps. The LCD shows 0 for off and 1-94 for the rates. The four pair VCAs use the same fine 9-bit linear amplitude scaling as PLAY. Soft takeover applies whenever **MODE** is pressed or released, so a pot moved on one layer does not change the waveform or LFO rate on the other until it moves again.

Leaving DRONE returns to SEQ and restarts the sequence from step 1.

## Wavetable utilities

The wavetables in `src/tables.h` can be replaced with custom waveforms. Audacity is recommended for preparing them: use a mono track with exactly 256 samples and export it as headerless little-endian signed 16-bit PCM RAW.

On Windows, set `RAW_TO_H_OUTPUT_BITS` to `9` near the top of `util/raw_to_h.bat`, then drag the `.raw` file onto the script. It creates a flash-ready `.h` file beside the source. Drag that generated header onto `util/sample_player.bat` to audition it. When copying the array into `src/tables.h`, retain the required table name, `PROGMEM` storage, 256-sample length, and signed 9-bit range.

## Architecture and critical implementation details

- **Real-time audio:** Timer2 compare A interrupts at 20 kHz and only writes the next sample from a 16-sample ring to Timer1, which generates 9-bit Fast PWM on `D9` at 31.25 kHz. After each eight-sample half has played, compare A enables Timer2 compare B, which re-enables interrupts and renders that half while the other plays, so output timing stays exact. Per-block work is shared by eight samples, adding 0.4-0.8 ms of output latency. Voices are rendered in two four-sample sub-blocks, each preceded by the envelope and vibrato update, keeping the 5 kHz per-voice control rate.
- **Assembly renderer:** `src/synth_voices.S` holds the hot paths: voice blocks (wavetable, square, noise, and the two-source morph), drum playback, the low-pass filter, the chorus, the VCA, and the dithered quantizer. Each step is bit-exact with the C formulas documented in `src/synth.h`.
- **Synthesis:** 256-sample wavetables, phase accumulators, four envelope-driven normal voices, and a separate eight-oscillator drone path. Core tables and drum data live in flash.
- **Resource-aware mixing:** the drone engine recalculates one oscillator pair per output sample. Each pair is therefore updated at 5 kHz while the PWM output remains at 20 kHz. This is an intentional quality/CPU trade-off for the 80-600 Hz range.
- **Chorus:** the macro-controlled post-mix effect uses a 108-sample, 16-bit delay line in SRAM and audio-rate fractional interpolation. DRONE bypasses it.
- **Low-pass filter:** a fixed-point Chamberlin state-variable filter with two 16-bit states and 8-bit cutoff and damping coefficients runs over each eight-sample block, before the VCA and the chorus, at about 82 cycles per sample. Its states run at a quarter of the mix scale with saturating arithmetic, so no input can wrap them; its input gain is the damping moved three eighths of the way back toward unity, so resonance keeps more level and overdrives its peak, and its output is clamped to the mix range. At 20 kHz the structure stays stable and well tuned up to about fs/6, which sets the 3.3 kHz upper cutoff. When it is open, it costs nothing and its state follows the mix, so it engages without a step; it opens and closes through its brightest, unity-damping setting. Each coefficient moves one step per block toward its target. The shared filter envelope reads the smooth decay envelope `1` once per block at eight times the voice envelope step, on the same phase rules as the voice envelopes, and adds its share of the remaining distance to the brightest coefficient after that slew, with two 8-bit multiplies.
- **PWM quality:** the internal mix uses additional fixed-point precision before conversion to 9-bit PWM, with quantization-error feedback and dither where appropriate.
- **Output level:** each voice's envelope is scaled by its level at the 5 kHz control rate, so it costs no per-sample work. The levels of all voices that can sound together never exceed the mix's full scale, which keeps the summed bus inside the PWM range without a limiter or clipping stage.
- **Stable controls:** multiplexed analog inputs use settling reads, median sampling, smoothing, movement confirmation, and hysteresis. Buttons are debounced in software.
- **Efficient LCD updates:** a two-line shadow buffer writes only changed characters, avoiding visible flicker and unnecessary time in the control loop.
- **Simulator fidelity:** Timer1 mode 6 is modeled explicitly. Like the hardware, the simulator executes one more instruction after interrupts are enabled before serving one, which GCC's stack-pointer updates rely on. PWM edges feed an exact state-space model of the RC ladder and AC-coupling stage, followed by oversampling and a four-pole anti-alias filter before Web Audio output.

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
