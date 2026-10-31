# BitSynth development instructions

## Project and target

- BitSynth is one Arduino firmware image shared by the physical instrument and the browser simulator. The production target is the PlatformIO `hardware` environment: Arduino Pro or Pro Mini, ATmega328P, 5 V, 16 MHz, Arduino framework, with `SYNTH_FIXED_OUTPUT_MODE=CHA` and `LiquidCrystal` 1.0.7.
- Treat `README.md` as the user-facing behavior and hardware specification, `platformio.ini` as the build source of truth, `src/bitsynth.ino` as the UI/sequencer/control loop, `src/synth.h` as the real-time synthesis/timer engine, and `src/tables.h` as flash-resident wavetables, envelopes, pitch data, and packed drum samples.
- Hardware limits reported by PlatformIO are 30,720 bytes usable flash and 2,048 bytes SRAM. The verified baseline build uses 29,232 bytes flash (95.2%; 1,488 bytes free) and 725 bytes static RAM (35.4%; 1,323 bytes nominally free). Re-measure after changes; static free RAM is not a stack/heap safety guarantee.
- Timer2 owns the 20 kHz audio ISR, giving 800 CPU cycles per sample at 16 MHz. Timer1 owns 9-bit Fast PWM on D9/channel A at 31.25 kHz. Do not repurpose these timers, D9, or their registers without redesigning and validating the audio path and simulator model.
- The three CD74HC4067 multiplexers share address pins D8/D11/D10 and feed parameter pots on A0, step buttons on A1, and step pots on A2. A3 is MODE/PANIC; D2-D7 drive the 16x2 LCD. Keep firmware, `simulator/diagram.json`, the simulator model/UI, schematic, and README consistent when hardware mappings change.

## Change rules

- Prevent regressions in existing functionality. Backward compatibility is not required, but update all affected code, simulator behavior, hardware artifacts, and documentation consistently.
- Prioritize CPU, SRAM, and flash efficiency. Protect the 20 kHz deadline: do not allocate dynamically, block, perform I/O, or add avoidable wide arithmetic or other expensive work in the audio ISR. Keep lookup/sample data in `PROGMEM`, preserve deliberate time-slicing and fixed-point approaches, and use atomic access for multi-byte state shared with the ISR.
- Prefer simple, maintainable, focused changes. Avoid unnecessary dependencies, abstraction, generated bulk, and unrelated refactoring. Flash headroom is especially small.
- Preserve the README's existing language, style, structure, and level of detail. Update it whenever behavior, features, controls, hardware, simulator operation, dependencies, or build/upload instructions change; do not duplicate the README wholesale elsewhere.
- Preserve control robustness unless intentionally changing it: multiplexed analog reads depend on settling/discard reads, median filtering, smoothing, movement confirmation, hysteresis, and soft takeover; buttons depend on debounce; LCD output uses a changed-character shadow buffer.
- Drum data must remain flash-resident signed 9-bit PCM at 20 kHz in the packed representation expected by `readPackedSigned9Sample`; retain or update the compile-time sample/table assertions when formats change.

## Build and validation

- After every code change, build the actual target from the repository root:

  ```sh
  pio run -e hardware
  ```

- Read the build's RAM and flash totals, calculate remaining bytes against 2,048 and 30,720, and report used and remaining capacity. Consider stack depth, local arrays, interrupt nesting/state, and any runtime allocation in addition to PlatformIO's static RAM figure. Do not treat the nominal remainder as wholly available.
- Upload to a connected target when hardware validation is requested or available:

  ```sh
  pio run -e hardware -t upload
  ```

- For simulator checks, first build so `.pio/build/hardware/firmware.hex` is current, then open `simulator/hardware.html` directly in a browser. If local file access is blocked or HTTP is preferred, optionally serve the repository from its root:

  ```sh
  python -m http.server 8000
  ```

  In that case, open `http://localhost:8000/simulator/hardware.html`. Exercise affected controls and modes using either launch method. The simulator is buildless but imports pinned AVR8js from jsDelivr, so its first load needs network access. Use the hidden service mode/cycle meter when audio-ISR load is relevant; an 800-cycle period is the hard budget.
- There is no repository test suite. Run every relevant available check for the changed area and manually exercise affected SEQ, PLAY, DRONE, LCD, control, and audio behavior as appropriate. Clearly separate: successful compilation/size verification, browser-simulator observations, and behavior that still requires the physical ATmega328P instrument, analog controls, LCD, PWM filter, or audio output.
- Never claim a build or test passed unless it was actually run. If tooling, network access, a board, or audio inspection is unavailable, state exactly what was not verified.

## Handoff

Finish with a brief summary of changed files/behavior, commands and checks actually run, flash and static RAM usage plus remaining capacity, hardware tests still needed, and unresolved issues.
