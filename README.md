# bitsynth

An Arduino Pro Mini based 9-bit wavetable synth with a simplified 8-step performance UI.

## Credits

The synth engine started from Dzl/Illutron's Arduino synth:
https://github.com/dzlonline/the_synth

## Hardware

The sketch is designed for a 16 MHz AVR Arduino with Timer1 9-bit PWM support, such as a Pro Mini, Nano, or Uno.

Audio is generated with high-speed 9-bit PWM on Timer1 from 512-sample oscillator wavetables. The default output mode is `CHA`, which uses Arduino pin 9. Timer2 provides the 20 kHz audio interrupt. Use an external analog low-pass/output filter after the PWM pin.

The UI expects four control groups selected by the three mux address pins:

- Mux address pins: `D8`, `D11`, `D10`
- Live control pots: 4 analog controls on `A0`
- Step buttons: 8 digital step controls on `A1`
- Step pots: 8 analog step controls on `A2`
- Extra page/shift/utility button: 1 digital button on `A3`

The LCD is a 16x2 `LiquidCrystal` display on pins `2, 3, 4, 5, 6, 7`.

## Setup

Copy the files into a folder and open `bitsynth.ino` with the Arduino IDE.

Install the official `LiquidCrystal` library from Library Manager if your IDE or CLI setup does not already provide it. The synth engine and wavetable data are included locally as `synth.h` and `tables.h`.


## Performance Controls

The current UI is intentionally reduced to the controls that fit the small display and panel:

- Live pot 1: oscillator waveform `SIN`, `TRI`, `SQR`, `SAW`, `RMP`, `NOI`
- Live pot 2: speed, `40`-`240` BPM
- Live pot 3: envelope length, `0`-`127`
- Live pot 4: pitch modulation, `0`-`127`, with `64` near neutral
- Step buttons 1-8: toggle the matching sequencer step on/off
- Step pots 1-8: set the matching step pitch in semitone increments across four octaves

When a step pot changes, the LCD briefly shows the edited step's note name, for example `C2` or `E1`. If the pitch does not change again, the display returns to the main screen after 500 ms.

The extra button, page switching, Shift shortcuts, utility actions, pattern selection, and alternate sequencer lanes are currently not exposed on the panel.

## LCD Dashboard

The main screen is a compact performance dashboard:

```text
SIN 126  64  64
x-xOxx--
```

Top row:

- Oscillator waveform
- Speed in BPM
- Envelope length
- Pitch modulation

Bottom row:

- `x`: gated step
- `-`: empty/off step
- `O`: currently playing gated step
- `o`: currently playing empty/off step

The active playback loop is currently 8 steps. The pattern data structure still stores 16 steps internally, but steps 9-16 and the range switch are not exposed by the simplified UI.

## Implemented But Not Currently Exposed

The firmware still contains several features that were added before the UI was simplified. They are intentionally kept in the code for later UI work, but they are not reachable from the current panel controls.

### Page System And Shift Shortcuts

The previous page order was `OSC`, `O2`, `ENV`, `SEQ`, `PAT`, `FX`, `UTL`.

Previously, the extra button handled:

- Short press: next page
- Double press: previous page
- Hold: Shift mode
- Long hold: panic voices and jump to `UTL`

Previously, Shift + step buttons handled:

- `Shift+B1`: note lane `N`
- `Shift+B2`: gate lane `G`
- `Shift+B3`: probability lane `P`
- `Shift+B4`: toggle range `1-8` / `9-16`
- `Shift+B5`: copy current pattern to RAM clipboard
- `Shift+B6`: paste RAM clipboard to current pattern
- `Shift+B7`: clear current lane in the visible range
- `Shift+B8`: randomize current lane in the visible range

### `OSC` Page Features

- Pot 2: play mode `SCL`, `CHD`, `ARP`
- Pot 3: voicing dial, `-12` to `+12`
- Pot 4: global transpose, `-24` to `+24` semitones

`SCL` is the classic one-note scale sequencer. `CHD` builds a three-note chord from the step note and selected scale. `ARP` uses the same chord notes but plays one note per step or ratchet.

The voicing dial shifts chord inversions by moving the lowest note up an octave for positive values, or the highest note down an octave for negative values. The `L` lane adds a per-step voicing offset on top of this global value.

The waveform part of this page is still exposed as live pot 1.

### `O2` Page Features

- Pot 1: OSC2 waveform `SIN`, `TRI`, `SQR`, `SAW`, `RMP`, `NOI`
- Pot 2: OSC2 envelope/off `OFF`, `E0`-`E3`
- Pot 3: OSC2 detune amount, `0`-`24`
- Pot 4: OSC2 transpose, `-24` to `+24` semitones

OSC2 is silent when its envelope is `OFF`. When enabled, it uses its own waveform, envelope table, detune, and transpose while sharing the global length, pitch modulation, and glide controls.

### `ENV` Page Features

- Pot 1: envelope table `E0`-`E3`
- Pot 4: glide amount, `0`-`127`

Envelope length and pitch modulation are still exposed as live pots 3 and 4.

### `SEQ` Page Features

- Pot 2: swing amount
- Pot 3: scale `CHR`, `MAJ`, `MIN`, `PEN`, `BLU`
- Pot 4: active lane `N`, `G`, `P`, `R`, `L`

Chord quality follows the scale: `CHR` and `MAJ` make major triads, `MIN` and `BLU` make minor triads, and `PEN` makes a suspended/power-style triad.

Tempo is still exposed as live pot 2.

### `PAT` Page Features

- Pot 1: active RAM pattern slot `P1`-`P4`
- Pot 2: destination/status slot display
- Pot 3: randomize amount
- Pot 4: visible range `1-8` / `9-16`

Pattern slots are RAM-only and do not survive power-off. Copy/paste uses the RAM clipboard from the Shift shortcuts.

### `FX` Page Features

- Pot 1: sample-hold/bitcrush amount, `H01`-`H16`
- Pots 2-4: unused

`H01` is clean output. Higher values hold the PWM sample longer for a rougher digital sound.

### `UTL` Page Features

Long-hold the extra button to enter `UTL` and panic the voices. In `UTL`, normal step buttons trigger utility actions:

- `B1`: panic voices
- `B2`: initialize the current pattern
- `B3`: turn all gates on in the current pattern
- `B4`: exit to `OSC`

## Sequencer Lanes

Only direct note editing and gate toggles are currently exposed.

- `N` note lane: step pots set notes for steps 1-8.
- `G` gate lane: step buttons toggle gates for steps 1-8.
- `P` probability lane: still implemented internally; not currently exposed.
- `R` ratchet lane: still implemented internally; not currently exposed.
- `L` lock lane: still implemented internally; not currently exposed.

Gate and probability are checked once at the start of a step; ratchets only happen when that first trigger fires.

## Current Limitations

- Pattern slots and clipboard are RAM-only; there is no EEPROM persistence yet.
- There is no `FIL` page because the current synth engine has no real filter stage. `synth.h` keeps a legacy `setFilter()` stub, but it does not affect audio.
- There is no LFO, mixer page, accent lane, modulation lane, or general-purpose parameter-lock lane yet. The `L` lane is intentionally limited to voicing locks to keep RAM and UI pressure low.
- The current panel UI exposes only one 8-step loop even though the pattern structure still has 16 step slots.
- Step pots and live pots are direct controls in the simplified UI; soft-takeover is currently not used.

## Notes

This project pushes a small AVR fairly hard. If you add more DSP, keep the Timer2 audio interrupt short and predictable. Flash usage should stay conservative, and audio quality depends heavily on the external analog output filter and power/noise layout.
