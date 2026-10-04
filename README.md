# bitsynth
An Arduino Nano based wavetable synth and 8-step sequencer.

# original synth engine made by
https://github.com/dzlonline/the_synth

# before start
This is a simple Arduino based wavetable synth with live controls and sequence functions. It was originally built for an Arduino Nano.

# start
Copy the files into a folder and open `sequencer.ino` with the Arduino IDE. The synth engine is included locally as `synth.h`/`tables.h`. Install the official `LiquidCrystal` library from Library Manager if your IDE or CLI setup does not already provide it.

If you get compile errors, double-check that you are building for an AVR Arduino with Timer2 support, such as Nano, Uno, or Pro Mini.
