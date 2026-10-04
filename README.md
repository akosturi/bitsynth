# bitsynth

An Arduino Nano based wavetable synth and 8-step sequencer.

## Credits

The synth engine started from Dzl/Illutron's Arduino synth:
https://github.com/dzlonline/the_synth

## Hardware

The sketch is designed for an AVR Arduino with Timer2 PWM support, such as Nano, Uno, or Pro Mini.

Audio is generated with high-speed PWM on Timer2. The default output mode is `CHA`, which uses Arduino pin 11. Use an external analog low-pass/output filter after the PWM pin.

The UI expects four control groups selected by the three mux address pins:

- Top controls: 4 analog settings on `A0`
- Step buttons: 8 digital step toggles on `A1`
- Pitch controls: 8 analog pitch pots on `A2`
- Reset/page button: 1 digital button on `A3`

The LCD is a 16x2 `LiquidCrystal` display on pins `2, 3, 4, 5, 6, 7`.

## Setup

Copy the files into a folder and open `sequencer.ino` with the Arduino IDE.

Install the official `LiquidCrystal` library from Library Manager if your IDE or CLI setup does not already provide it. The synth engine and wavetable data are included locally as `synth.h` and `tables.h`.

QuickStats is no longer required; analog median filtering is handled inside the sketch with integer samples.

## Basic Use

The 8 pitch pots set the notes for the 8 sequencer steps. The 8 step buttons toggle steps on and off. A lit block on the LCD step row shows the currently playing step; dots show enabled steps and blanks show disabled steps.

Short-press the reset button to enable all steps again.

Hold the reset button for about 700 ms to change the top-control page. The pages are:

- `SYN`: main synth controls
- `PRF`: performance controls
- `FX`: output effect controls

## SYN Page

The `SYN` page keeps the original synth controls.

- Pot 1: waveform: `SIN`, `TRI`, `SQR`, `SAW`, `RMP`, `WAV`
- Pot 2: tempo, 10-350 ms per step
- Pot 3: envelope length, 0-127
- Pot 4: pitch modulation, 0-127, with 64 near neutral

`WAV` is the noise wavetable.

## PRF Page

The `PRF` page adds musical performance features.

- Pot 1: voice mode
- Pot 2: pitch scale
- Pot 3: swing amount
- Pot 4: glide amount

Voice modes:

- `ONE`: one oscillator
- `DTN`: second oscillator, slightly detuned
- `OCT`: second oscillator one octave up
- `FIF`: second oscillator a fifth up
- `SUB`: second oscillator one octave down

Scale modes:

- `CHR`: chromatic
- `MAJ`: major
- `MIN`: minor
- `PEN`: major pentatonic
- `BLU`: blues

Swing alternates shorter and longer step intervals while keeping the average tempo close to the selected tempo. Glide slides pitch toward the next step; `0` is instant pitch change and higher values slide more slowly.

## FX Page

The `FX` page controls output sample-hold bitcrushing.

- Pot 1: `HOLD`, 1-16 audio samples
- Pots 2-4: unused

`HOLD 01` is clean output. Higher values hold the PWM sample longer for a rougher, more digital sound.

The synth also applies lightweight PWM quantization noise shaping on pitched waveforms. Noise shaping is automatically bypassed when the noise wavetable is selected.

## Notes

This project pushes a small AVR fairly hard. If you add more DSP, keep the Timer1 audio interrupt short and predictable. Flash usage is still comfortable, but audio quality depends heavily on the external analog output filter and power/noise layout.
