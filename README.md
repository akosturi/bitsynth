# BitSynth

BitSynth is a small wavetable synthesizer and eight-step sequencer built around a single 16 MHz ATmega328P. It is a reimagining of a project I first built about ten years ago: the hardware stays deliberately limited, but the firmware tries to squeeze as much music out of it as possible.

What you get:

- **SEQ** – an eight-step sequencer with swing and optional generative variation (`GEN`)
- **PLAY** – live playing in a scale, with chords and arpeggios over a running background sequence
- **DRONE** – eight free-running oscillators with their own tremolo
- Twelve melodic programs plus a PCM drum kit, a resonant filter, chorus, and a 16x2 LCD

The same firmware runs on the real instrument and in the browser simulator, so you can try everything without building anything.

## Quick start

### Build and upload

Install [Visual Studio Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension (or just PlatformIO Core). No external libraries or Node.js packages are needed.

```sh
pio run -e hardware              # build
pio run -e hardware -t upload    # build and upload
```

The target is an Arduino Pro Mini (ATmega328P, 5 V, 16 MHz); an Uno or Nano wired to the same pins works too. A Pro Mini needs a 5 V USB-to-serial adapter; if PlatformIO does not find it, set `upload_port` in `platformio.ini`. The last build used 30,474 bytes of flash (99.2%) and 1,104 bytes of RAM (53.9%).

### Try it in the browser

After building, open `simulator/hardware.html` in a browser. It loads `.pio/build/hardware/firmware.hex` automatically and runs the real firmware with AVR8js. If local files are blocked, serve the repository root instead:

```sh
python -m http.server 8000
```

and open `http://localhost:8000/simulator/hardware.html`. You can also click the LCD to load an ELF, HEX, or BIN file. The first load needs an internet connection (AVR8js comes from jsDelivr), and you may need to click or press a key once to start the audio.

| Simulator | Action |
|---|---|
| Knobs | Drag vertically, or use the mouse wheel |
| `1`-`8` | Step buttons (hold to keep pressed) |
| `M` | MODE: tap, double-tap, or hold with `1`, `2`, or `8` |

The simulator plays the actual 9-bit PWM output through a model of the analog output filter, so it sounds like the instrument.

## Playing BitSynth

### The MODE button

BitSynth starts in SEQ. **MODE** does three things:

- **Tap** – next parameter page (SEQ and PLAY)
- **Double-tap** (within 350 ms) – next mode: SEQ → PLAY → DRONE → SEQ
- **Hold + something** – a shortcut:

| Shortcut | Where | What it does |
|---|---|---|
| MODE + step button 1 | PLAY | Background sequence on/off |
| MODE + step button 2 | SEQ, PLAY | Swing on/off |
| MODE + step button 8 | SEQ, PLAY | `GEN` on/off |
| MODE + parameter pot | DRONE | Tremolo rate of that oscillator pair |

Holding MODE alone does nothing, and a shortcut also works if the step button lands a moment (up to 25 ms) before MODE.

**Soft takeover:** after a page or layer change, a pot does nothing until you move it, so values never jump.

**Parameter pages:** the four parameter pots work on pages of four. Each mode fills its pages left to right with what the current program offers (see [Parameters](#parameters)); empty slots are skipped. The LCD's top row shows the four names; when you turn a pot, its value appears for about 1.5 s.

### SEQ – step sequencer

| Control | Function |
|---|---|
| Parameter pots | Program, tempo (60-190 BPM), then the program's parameters |
| Step pots 1-8 | Note of each step, C2 to B3 (drum sound in `DRM`) |
| Step buttons 1-8 | Step on/off |

The sequence runs in sixteenth notes. A step that is off fades the previous note out in about 20 ms instead of cutting it.

The LCD's bottom row is a little melody graph: each step is a bar whose height follows its note (C2 at the bottom, B3 full height; in `DRM` kick, snare, and hat are low, middle, and full). An active step is a solid bar, an inactive one a dotted dash, and the current step is a checkerboard. Turning a step pot briefly shows the step and its note at the lower right. A `*` means `GEN` is on.

**Swing** (off at power-up, not shown on the LCD) delays steps 2, 4, 6, and 8 to 65% of each step pair, a light shuffle; the tempo stays the same. It applies to every program, to PLAY's background sequence, and to arpeggios (`1/2`, `1/4`, and `1/8` stay straight).

**GEN** slowly varies a melodic sequence while it plays. At the start of each eight-step cycle it may change one step: it borrows the note of another active step, jumps an octave, or returns home. Step 1 and the rhythm never change and only notes already in the sequence are used, so the key stays. Turning a step pot resets that step; switching `GEN` off restores the original. `DRM` is not affected.

### PLAY – live playing

The step buttons play the notes of a scale, while the SEQ sequence can keep running in the background.

| Control | Function |
|---|---|
| Parameter pots | Program, root note, scale (`MAJ`, `MIN`, `PEN`, `BLU`), style, then the program's parameters |
| Step pot 1 | Octave, O1-O5 (applies from the next note) |
| Step pot 2 | Tempo, shared with SEQ (takes effect once you move it) |
| Step pot 3 | Arpeggio speed: `1/2`, `1/4`, `1/4t`, `1/8`, `1/8t`, `1/16`, `1/16t`, `1/32` |
| Step buttons 1-8 | Consecutive scale notes |

Styles:

- `POLY` – up to four notes at once (three while the background sequence runs); a new note takes over the oldest.
- `CHRD` – a 1-3-5 chord from the most recently held button.
- `UP`, `DOWN`, `UPDN` – arpeggiate the held buttons.
- `CUP`, `CDWN`, `CUDN` – arpeggiate the chord of the most recently held button.

Arpeggios lock to the sequencer clock: button changes take effect on the next arpeggio note, and every division stays in phase with step 1. A multi-voice program plays each note (in `POLY` and in arpeggios) as one thick unison stack; single-voice programs stay polyphonic. Released notes fade out in about 20 ms.

**Background sequence:** the SEQ pattern keeps playing with the SEQ program and its settings (`DRM` too) on its own voices, at a fixed level, without the filter, chorus, or tremolo. It keeps its position when you enter PLAY, and switching it on or off does not restart notes you are holding.

The bottom row shows `▶` (running, blinking with the beat; `*` with `GEN`) or `■` (stopped), then octave, tempo, and arpeggio speed. The top row shows program, root, scale, and style, for example `A01 C#  MIN UPDN`. `DRM` is available only in SEQ.

### DRONE – eight oscillators

| Control | Function |
|---|---|
| Parameter pots 1-4 | Waveform (sine, triangle, square, saw) of oscillator pairs 1-2, 3-4, 5-6, 7-8 |
| MODE + parameter pots 1-4 | Tremolo rate of each pair: off, or 94 steps from 0.05 to 20 Hz (LCD 0-94) |
| Step pots 1-8 | Frequency, 80-600 Hz, unquantized |
| Step buttons 1-8 | Oscillator on/off |

Oscillators fade in and out without clicks. DRONE skips the filter and chorus. Leaving DRONE restarts the sequence from step 1.

## Sounds

Program names start with a category letter: `A` bass, `B` lead, `C` pad, `D` keys, `E` sequence and effect. The descriptions say what each program aims for within this little engine, not a copy of the original.

| Program | Sound | Voices |
|---|---|---|
| `A01` | Funk bass in the style of "Thriller": two beating saws over a square sub-octave | 3 |
| `A02` | Reese bass: two detuned saws over a sine sub-octave, slow filter motion | 3 |
| `A03` | 808 bass: a long, sliding sine | 1 |
| `B01` | Pulse lead in the style of "Axel F" | 2 |
| `B02` | Supersaw trance lead with chorus and glide | 4 |
| `C01` | Juno-style PWM pad with chorus | 1 |
| `C02` | String machine in the style of Jarre's "Oxygène" | 1 |
| `D01` | Saw poly brass in the style of "Jump", for chords | 1 |
| `D02` | Vibraphone or electric piano with motor tremolo | 1 |
| `D03` | Rock organ: four sine drawbars, key click, Leslie-like tremolo | 4 |
| `E01` | Dark arpeggio in the style of "Stranger Things": saw and pulse morphing, filter slowly moving | 2 |
| `E02` | Surf noise in the style of "Oxygène": drifting filtered white noise | 1 |
| `DRM` | Drum kit: `KICK`, `SNAR`, `CHH` per step (SEQ only) | - |

Tips:

- Single-voice programs (`A03`, `C01`, `C02`, `D01`, `D02`) are polyphonic in PLAY, ideal for `POLY` and `CHRD`.
- `C01` and `C02` swell in over about 0.8 s; in SEQ every step restarts the swell, so lower `LEN` there.
- For the `E01` theme, hold step buttons 1, 3, 5, 7, and 8 in PLAY with `C`, `MAJ`, and `UPDN`.
- `E02` ignores pitch; with `ENV` 0 and a short `LEN` it ticks like a hi-hat.
- `DRM` plays one sound per step, and a new hit replaces the previous one.

## Parameters

Each program offers up to eight of these parameters plus two LFOs, so it has up to four pages. SEQ and PLAY remember their own values; changing the program loads its defaults.

| Name | What it does | Range |
|---|---|---|
| `FLT` | Low-pass filter cutoff | 0-126 = about 100 Hz-3.3 kHz, 127 = open |
| `RES` | Filter resonance | Q about 1-6 |
| `FEG` | Filter envelope: opens the filter at each note, then closes it | 0 off - 127 |
| `LEN` | Note length | about 0.2-6.5 s, 127 = HOLD |
| `ENV` | Envelope shape (see below) | 0-5 |
| `GLD` | Glide between notes | 0 off - 127 |
| `VIB` | 5 Hz vibrato depth | 64 none - 127 |
| `PWM` | Square-wave pulse width | 5-95% |
| `CHR` | Juno-style chorus | 0 off, 1, 2 |
| `MOR` | Speed of morphing between the program's first two waveforms | 0 off, 0.16-20 Hz |
| `VCD` | Tremolo depth | 0-256 |
| `VCR` | Tremolo rate | 0 off, 0.01-20 Hz |
| `TUN` | Drum playback speed (`DRM`) | 0.75-1.25x |
| `DEC` | Drum decay (`DRM`) | 25-100% of the sample |
| `RAT` / `DEP` | LFO rate and depth | 1-94 (0.05-20 Hz) / 0-127 |

Envelopes: `0` fast decay, `1` smooth decay, `2` pad (soft attack, long sustain), `3` constant, `4` full level then a linear fade, `5` strike (quick drop to 40%, then slow decay). HOLD sustains until you release the note.

Good to know:

- **LFOs** draw a triangle around the parameter's value. They can modulate `PWM`, `VIB`, `TUN`, `DEC`, `VCD`, `VCR`, `FLT`, and `RES`; depth 127 sweeps the whole range. A slot shown as `---` is unused.
- **Filter:** a resonant 12 dB/octave filter. Resonance does not self-oscillate; high `RES` lowers the level a little and adds some grit where the peak clips. Changes glide smoothly, so sweeps do not click.
- **Filter envelope:** there is one shared filter, so in PLAY each new note restarts the envelope for every sounding note. It closes within a quarter of `LEN` (about 0.5 s with HOLD).
- **Chorus:** a mono, Juno-60-style chorus (1 slower at 0.51 Hz, 2 faster at 0.86 Hz).
- **Morph** works only on multi-voice programs and fades between waveforms; pitch and envelope stay the same.
- **Volume** is automatic: notes and drum hits share the output, and a built-in limiter keeps chords and overlapping notes from clipping.

## Hardware

Three CD74HC4067 multiplexers share their address lines and read the pots and buttons.

| Function | Pin |
|---|---|
| MUX address bits 0, 1, 2 | `D8`, `D11`, `D10` |
| Four parameter pots | MUX to `A0` |
| Eight step buttons | MUX to `A1` |
| Eight step pots | MUX to `A2` |
| MODE button | `A3` |
| 16x2 LCD (`RS`, `E`, `D4`-`D7`) | `D2`-`D7` |
| 9-bit PWM audio | `D9` |

The KiCad project and a wiring picture are in `schematic/`.

### Audio output

`D9` outputs PWM, so it needs this low-pass filter before it reaches an audio input:

```text
D9 PWM -- R1 2.2 kΩ --o-- R2 2.2 kΩ --o-- R3 2.2 kΩ --o-- (+) C4 4.7 µF --o-- VOL 50 kΩ log -- Rout 1 kΩ -- AUDIO OUT
                      |                |                |                         |          |
                   C1 4.7 nF       C2 4.7 nF       C3 6.8 nF                R4 100 kΩ    GND
                      |                |                |                         |
                     GND              GND              GND                       GND
```

The `+` lead of `C4` faces `R3`. The volume pot's top goes to the `C4/R4` node, its bottom to ground, and its wiper through `Rout` to the output. This is a line output: use an amplifier for speakers or headphones.

## Under the hood

- **Audio:** a 20 kHz timer interrupt outputs one sample at a time, while a second one renders the next eight samples in the background (AVR assembly in `src/synth_voices.S`). The output is 9-bit PWM at 31.25 kHz with dither.
- **Voices:** five wavetable voices (four normal, one more for the background sequence), generated square and noise, and a separate eight-oscillator drone engine. Wavetables, envelopes, and drum samples (9-bit, 20 kHz, packed) live in flash.
- **Controls:** analog readings are median-filtered, smoothed, and use hysteresis; buttons are debounced; the LCD only rewrites changed characters. Everything is driven through registers, with no libraries.

## Developer tools

- **Cycle meter:** click the lower-right screw in the simulator to show the audio-interrupt load against its 800-cycle-per-sample budget, plus the worst eight-sample block (MAX). Each reopening starts a new measurement, and rare peaks can take a while to appear.
- **Headless tests:** `util/fullsim.js` runs a scripted session through the whole firmware, and `util/isrbench.js` benchmarks the audio interrupt. Both need Deno; usage is at the top of each file.
- **Custom wavetables:** prepare a 256-sample mono waveform in Audacity and export it as headerless signed 16-bit RAW. Set `RAW_TO_H_OUTPUT_BITS` to `9` in `util/raw_to_h.bat` and drag the `.raw` file onto it; drag the generated `.h` onto `util/sample_player.bat` to listen. Copy the array into `src/tables.h`, keeping its name, `PROGMEM`, 256 samples, and the signed 9-bit range.

## Limitations

- Settings and patterns are not saved; a reset restores the defaults.
- PLAY has three live voices while the background sequence runs, and the background plays at most two voices of a program.
- DRONE has no per-note envelopes.
- Flash is almost full; check the size report after every change.

## Repository layout

```text
src/             Firmware, synth engine, wavetables, envelopes, and drum samples
simulator/       Browser front panel and simulator
schematic/       KiCad project and wiring picture
util/            Wavetable helpers and headless test tools
platformio.ini   Build settings
```

## License

MIT. See `LICENSE`.
