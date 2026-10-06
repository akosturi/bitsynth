# bitsynth

An Arduino Pro Mini based 8-bit wavetable synth with a 16-step performance sequencer.

## Credits

The synth engine started from Dzl/Illutron's Arduino synth:
https://github.com/dzlonline/the_synth

## Hardware

The sketch is designed for a 16 MHz AVR Arduino with Timer1 9-bit PWM support, such as a Pro Mini, Nano, or Uno.

Audio is generated with high-speed 9-bit PWM on Timer1. The default output mode is `CHA`, which uses Arduino pin 9. Timer2 provides the 20 kHz audio interrupt. Use an external analog low-pass/output filter after the PWM pin.

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

QuickStats is not required; analog median filtering is handled inside the sketch with integer samples.

## Performance Controls

The 8 step pots edit the current lane for the visible 8-step range. The 8 step buttons toggle gates for the visible steps. The extra button controls pages and Shift functions. Page order is `OSC`, `O2`, `ENV`, `SEQ`, `PAT`, `FX`, `UTL`.

- Short press: next page
- Double press: previous page
- Hold: Shift mode
- Long hold: panic voices and jump to `UTL`

Shift + step buttons:

- `Shift+B1`: note lane `N`
- `Shift+B2`: gate lane `G`
- `Shift+B3`: probability lane `P`
- `Shift+B4`: toggle range `1-8` / `9-16`
- `Shift+B5`: copy current pattern to RAM clipboard
- `Shift+B6`: paste RAM clipboard to current pattern
- `Shift+B7`: clear current lane in the visible range
- `Shift+B8`: randomize current lane in the visible range

## LCD Dashboard

The default screen is a compact performance dashboard:

```text
P1 N 1-8  126B
x-xOxx-- WAVSIN
```

Top row:

- `P1`: active RAM pattern slot
- `N`, `G`, `P`, `R`, `L`: active lane
- `1-8` / `9-16`: visible step range
- `126B`: tempo in BPM

Bottom row:

- `x`: gated step
- `-`: empty/off step
- `O`: currently playing gated step
- `o`: currently playing empty/off step
- Last six characters: last edited parameter and value

After page or pot edits, the LCD briefly shows a page view and then returns to the dashboard. Pickup hints use `<` or `>` when a live pot has not yet crossed the stored value.

## Pages

The 4 live control pots edit the active page. Soft-takeover applies to these pots after page changes and pattern loads.

### `OSC`

- Pot 1: waveform `SIN`, `TRI`, `SQR`, `SAW`, `RMP`, `NOI`
- Pot 2: play mode `SCL`, `CHD`, `ARP`
- Pot 3: voicing dial, `-12` to `+12`
- Pot 4: global transpose, `-24` to `+24` semitones

`SCL` is the classic one-note scale sequencer. `CHD` builds a three-note chord from the step note and selected scale. `ARP` uses the same chord notes but plays one note per step or ratchet.

The voicing dial shifts chord inversions by moving the lowest note up an octave for positive values, or the highest note down an octave for negative values. The `L` lane adds a per-step voicing offset on top of this global value.

### `O2`

- Pot 1: OSC2 waveform `SIN`, `TRI`, `SQR`, `SAW`, `RMP`, `NOI`
- Pot 2: OSC2 envelope/off `OFF`, `E0`-`E3`
- Pot 3: OSC2 detune amount, `0`-`24`
- Pot 4: OSC2 transpose, `-24` to `+24` semitones

OSC2 is silent when its envelope is `OFF`. When enabled, it uses its own waveform, envelope table, detune, and transpose while sharing the global length, pitch modulation, and glide controls.

### `ENV`

- Pot 1: envelope table `E0`-`E3`
- Pot 2: envelope length, `0`-`127`
- Pot 3: pitch modulation, `0`-`127`, with `64` near neutral
- Pot 4: glide amount, `0`-`127`

### `SEQ`

- Pot 1: tempo, `40`-`240` BPM
- Pot 2: swing amount
- Pot 3: scale `CHR`, `MAJ`, `MIN`, `PEN`, `BLU`
- Pot 4: active lane `N`, `G`, `P`, `R`, `L`

Chord quality follows the scale: `CHR` and `MAJ` make major triads, `MIN` and `BLU` make minor triads, and `PEN` makes a suspended/power-style triad.

### `PAT`

- Pot 1: active RAM pattern slot `P1`-`P4`
- Pot 2: destination/status slot display
- Pot 3: randomize amount
- Pot 4: visible range `1-8` / `9-16`

Pattern slots are RAM-only and do not survive power-off. Copy/paste uses the RAM clipboard from the Shift shortcuts.

### `FX`

- Pot 1: sample-hold/bitcrush amount, `H01`-`H16`
- Pots 2-4: unused

`H01` is clean output. Higher values hold the PWM sample longer for a rougher digital sound.

### `UTL`

Long-hold the extra button to enter `UTL` and panic the voices. In `UTL`, normal step buttons trigger utility actions:

- `B1`: panic voices
- `B2`: initialize the current pattern
- `B3`: turn all gates on in the current pattern
- `B4`: exit to `OSC`

## Sequencer Lanes

- `N` note lane: step pots set notes for the visible range.
- `G` gate lane: step pots set gate on/off by midpoint; step buttons also toggle gates.
- `P` probability lane: step pots set trigger probability from `0`-`100`.
- `R` ratchet lane: step pots set `1`-`4` retriggers inside the step.
- `L` lock lane: step pots set a per-step voicing offset from `-12` to `+12`.

Playback always runs through all 16 steps. The visible range only chooses which 8 steps the pots and buttons are editing. Gate and probability are checked once at the start of a step; ratchets only happen when that first trigger fires.

## Current Limitations

- Pattern slots and clipboard are RAM-only; there is no EEPROM persistence yet.
- There is no `FIL` page because the current synth engine has no real filter stage. `synth.h` keeps a legacy `setFilter()` stub, but it does not affect audio.
- There is no LFO, mixer page, accent lane, modulation lane, or general-purpose parameter-lock lane yet. The `L` lane is intentionally limited to voicing locks to keep RAM and UI pressure low.
- Step pots are direct controls. Soft-takeover is only used on the 4 live page pots.
- In gate lane, step pots continuously represent gate state, so a button toggle may be overwritten if the matching pot remains on the other side of the midpoint.

## Notes

This project pushes a small AVR fairly hard. If you add more DSP, keep the Timer2 audio interrupt short and predictable. Flash usage should stay conservative, and audio quality depends heavily on the external analog output filter and power/noise layout.
