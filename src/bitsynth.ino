#include <LiquidCrystal.h>
#include <avr/pgmspace.h>
#include "synth.h"

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

const uint8_t kMuxBit0Pin = 8;
const uint8_t kMuxBit1Pin = 11;
const uint8_t kMuxBit2Pin = 10;
const uint8_t kSettingsAnalogPin = A0;
const uint8_t kStepButtonPin = A1;
const uint8_t kStepAnalogPin = A2;
const uint8_t kModeButtonPin = A3;

const uint8_t kStepCount = 8;
const uint8_t kSettingCount = 4;
const uint8_t kStepPotsPerLoop = 2;
const uint8_t kDroneWaveCount = 4;
const uint8_t kPresetCount = 18;
const uint8_t kDrumCount = DRUM_SAMPLE_COUNT;
const uint8_t kScaleCount = 4;
const uint8_t kPlayStyleCount = 8;
const uint8_t kAnalogSamples = 5;
const uint8_t kAnalogDiscardSamples = 2;
const uint8_t kAnalogFilterFractionBits = 4;
const uint8_t kAnalogFilterAlphaNumerator = 2;
const uint8_t kAnalogFilterAlphaDenominator = 8;
const uint8_t kAnalogHysteresis = 12;
const uint8_t kAnalogEndpointMargin = 32;
const uint8_t kSettingMovementThreshold = 16;
const uint8_t kStepMovementThreshold = 10;
const uint8_t kDroneMovementThreshold = 8;
const uint8_t kAnalogMovementConfirmations = 2;
const uint8_t kMuxSettleUs = 50;
const uint8_t kButtonDebounceMs = 25;
const uint16_t kModeDoubleTapMs = 350;
const uint16_t kModeIgnoreHoldMs = 650;
const uint16_t kEditViewMs = 900;
const uint8_t kStepNoteMin = 36;
const uint8_t kStepNoteMax = 59;
const uint16_t kDroneFrequencyMin = 80;
const uint16_t kDroneFrequencyMax = 600;
const uint16_t kLfoFrequencyMinHundredths = 5;
const uint16_t kLfoFrequencyMaxHundredths = 2000;
const uint8_t kLfoRateCount = 94;
const uint8_t kPresetVcaModulationCount = 99;

enum InstrumentMode : uint8_t { MODE_SEQ = 0, MODE_PLAY = 1, MODE_DRONE = 2 };
enum ScaleMode : uint8_t { SCALE_MAJOR = 0, SCALE_MINOR = 1, SCALE_PENTATONIC = 2, SCALE_BLUES = 3 };
enum PlayStyle : uint8_t { STYLE_POLY = 0, STYLE_CHORD = 1, STYLE_ARP_UP = 2, STYLE_ARP_DOWN = 3, STYLE_ARP_UPDOWN = 4,
                           STYLE_CHORD_ARP_UP = 5, STYLE_CHORD_ARP_DOWN = 6, STYLE_CHORD_ARP_UPDOWN = 7 };
enum PresetId : uint8_t { PRESET_SINE = 0, PRESET_TRIANGLE, PRESET_SAW,
                          PRESET_SQUARE, PRESET_DRUM, PRESET_NOISE };
enum DrumId : uint8_t { DRUM_KICK = 0, DRUM_SNARE, DRUM_CLOSED_HAT };
enum MacroParam : uint8_t {
  MACRO_NONE = 0, MACRO_PULSE_WIDTH, MACRO_VIBRATO,
  MACRO_DRUM_TUNE, MACRO_DRUM_DECAY, MACRO_VCA_DEPTH, MACRO_VCA_RATE,
  MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH, MACRO_GLIDE, MACRO_CHORUS,
  MACRO_MORPH
};

enum ParameterControl : uint8_t {
  PARAM_NONE = 0,
  PARAM_SEQ_PRESET, PARAM_SEQ_BPM, PARAM_SEQ_TUNE,
  PARAM_PLAY_PRESET, PARAM_PLAY_ROOT, PARAM_PLAY_SCALE, PARAM_PLAY_STYLE,
  PARAM_MACRO_0, PARAM_MACRO_1, PARAM_MACRO_2, PARAM_MACRO_3,
  PARAM_LFO_0_RATE, PARAM_LFO_0_DEPTH, PARAM_LFO_1_RATE, PARAM_LFO_1_DEPTH
};

struct VoiceRecipe {
  uint8_t wave;
  int16_t detuneCents;
  uint8_t envelope;
  uint8_t modulation;
};

// Program data is deliberately split by renderer.  A PCM program never
// carries inactive oscillator, glide, or PLAY-only fields.
struct MelodicPreset {
  char label[5];
  uint8_t voiceCount;
  VoiceRecipe voices[4];
  uint8_t length;
  uint8_t squarePulseWidthPercent;
  uint8_t glide;
  uint16_t vcaDepth;
  uint8_t vcaModulation;
  uint8_t chorusMode;
  uint8_t morphSpeed;
  uint8_t macros[4];
  uint8_t lfoTargets[2];
};

struct DrumPreset {
  char label[5];
  uint8_t decay;
  uint8_t macros[4];
  uint8_t lfoTargets[2];
};

struct SeqControls { uint8_t preset; uint8_t bpm; uint8_t drumTone; };
struct PlayControls { uint8_t preset; uint8_t root; uint8_t scale; uint8_t style; uint8_t octave; };

struct AnalogMotionState {
  uint16_t accepted;
  uint8_t confirmations;
  int8_t direction;
  bool ready;
};

// label, voice count, voices {wave, detune cents, envelope, modulation} x4,
// envelope length, square pulse width, glide, PLAY VCA depth, PLAY VCA LFO rate,
// chorus mode, morph speed, direct macros x4, LFO targets x2
// Available macros: MACRO_NONE, MACRO_PULSE_WIDTH, MACRO_VIBRATO,
// MACRO_DRUM_TUNE, MACRO_DRUM_DECAY, MACRO_VCA_DEPTH, MACRO_VCA_RATE,
// MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH, MACRO_GLIDE, MACRO_CHORUS,
// MACRO_MORPH.
const MelodicPreset kMelodicPresets[] PROGMEM = {
  {"GLA", 2, {{SAW, 0, ENVELOPE3, 64}, {TRIANGLE, 0, ENVELOPE3, 64}, {SAW, 0, ENVELOPE3, 64}, {TRIANGLE, 0, ENVELOPE3, 64}}, 127, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"TRI", 1, {{TRIANGLE, 0, ENVELOPE3, 64}, {TRIANGLE, 0, 0, 64}, {TRIANGLE, 0, 0, 64}, {TRIANGLE, 96, 0, 64}}, 127, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_MORPH, MACRO_CHORUS, MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH}, {MACRO_VIBRATO, MACRO_NONE}},
  {"FLY", 1, {{SAW, 0, ENVELOPE3, 69}, {TRIANGLE, 0, 0, 64}, {TRIANGLE, 0, 0, 64}, {SINE, 0, 0, 64}}, 127, 50, 17, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"SQR", 1, {{SQUARE, 0, ENVELOPE3, 64}, {SQUARE, 0, 0, 64}, {SQUARE, 0, 0, 64}, {SINE, 0, 0, 64}}, 96, 35, 0, 256, 68, SYNTH_CHORUS_OFF, 0, {MACRO_PULSE_WIDTH, MACRO_MORPH, MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH}, {MACRO_VIBRATO, MACRO_NONE}},
  {"NSE", 1, {{NOISE, 0, ENVELOPE0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}, 96, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_MORPH, MACRO_CHORUS, MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH}, {MACRO_NONE, MACRO_NONE}},
  {"SUB", 2, {{SINE, 0, ENVELOPE0, 64}, {SQUARE, -12, ENVELOPE0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}, 102, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"BA1", 2, {{SAW, 0, ENVELOPE1, 64}, {SQUARE, -12, ENVELOPE1, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}, 102, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"PLK", 2, {{TRIANGLE, 0, ENVELOPE1, 64}, {SQUARE, 12, ENVELOPE1, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}, 102, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"BEL", 3, {{SINE, 0, ENVELOPE2, 64}, {SINE, 12, ENVELOPE2, 64}, {TRIANGLE, 19, ENVELOPE2, 64}, {SINE, 0, 0, 64}}, 102, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"LED", 2, {{SAW, 0, ENVELOPE0, 64}, {SAW, 0, ENVELOPE0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}, 102, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"HYS", 4, {{SAW, -18, ENVELOPE3, 62}, {SAW, -6, ENVELOPE3, 63}, {SAW, 6, ENVELOPE3, 65}, {SAW, 18, ENVELOPE3, 66}}, 127, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"PUR", 3, {{SINE, 0, ENVELOPE3, 64}, {SINE, 0, ENVELOPE3, 64}, {SINE, 0, ENVELOPE3, 64}, {SINE, 0, 0, 64}}, 116, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"SOF", 3, {{SINE, 0, ENVELOPE0, 64}, {SINE, 0, ENVELOPE0, 64}, {SINE, 0, ENVELOPE0, 64}, {SINE, 0, 0, 64}}, 102, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"KEY", 3, {{TRIANGLE, 0, ENVELOPE2, 64}, {TRIANGLE, 0, ENVELOPE2, 64}, {TRIANGLE, 0, ENVELOPE2, 64}, {SINE, 0, 0, 64}}, 82, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"PLK", 3, {{SQUARE, 0, ENVELOPE1, 64}, {SQUARE, 0, ENVELOPE1, 64}, {SQUARE, 0, ENVELOPE1, 64}, {SINE, 0, 0, 64}}, 46, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"BA2", 3, {{SAW, 0, ENVELOPE0, 58}, {SAW, 0, ENVELOPE0, 58}, {SAW, 0, ENVELOPE0, 58}, {SINE, 0, 0, 64}}, 92, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"UNI", 3, {{SAW, -9, ENVELOPE3, 64}, {SAW, 0, ENVELOPE3, 64}, {SAW, 9, ENVELOPE3, 64}, {SINE, 0, 0, 64}}, 127, 50, 12, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}},
  {"ORG", 3, {{SQUARE, 0, ENVELOPE3, 64}, {SQUARE, 0, ENVELOPE3, 64}, {SQUARE, 0, ENVELOPE3, 64}, {SINE, 0, 0, 64}}, 116, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, {MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}}
};

const DrumPreset kDrumPreset PROGMEM =
  {"DRM", 127, {MACRO_DRUM_DECAY, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}};
const uint8_t kPlayPresetCount = 18;

const char kDrumLabels[kDrumCount][5] PROGMEM = {"KICK", "SNAR", "CHH"};

const int8_t kScaleOffsets[kScaleCount][kStepCount] PROGMEM = {
  {0, 2, 4, 5, 7, 9, 11, 12}, {0, 2, 3, 5, 7, 8, 10, 12},
  {0, 2, 4, 7, 9, 12, 14, 16}, {0, 3, 5, 6, 7, 10, 12, 15}
};
const uint8_t kScaleLengths[kScaleCount] PROGMEM = {7, 7, 5, 6};
const char kDroneWaveLabels[kDroneWaveCount][4] PROGMEM = {"SIN", "TRI", "SQR", "SAW"};
const char kRootLabels[12][3] PROGMEM = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char kScaleLabels[kScaleCount][4] PROGMEM = {"MAJ", "MIN", "PEN", "BLU"};
const char kStyleLabels[kPlayStyleCount][5] PROGMEM = {"POLY", "CHRD", "UP", "DOWN", "UPDN", "CUP", "CDWN", "CUDN"};
synth edgar;
InstrumentMode instrumentMode = MODE_SEQ;
SeqControls seqControls = {PRESET_SINE, 126, 64};
// PLAY indexes its melodic-only catalogue; SEQ indexes the complete catalogue.
PlayControls playControls = {0, 0, SCALE_MAJOR, STYLE_POLY, 0};
uint8_t stepNotes[kStepCount] = {36, 38, 40, 41, 43, 45, 47, 48};
uint8_t stepDrums[kStepCount] = {DRUM_KICK, DRUM_CLOSED_HAT, DRUM_SNARE, DRUM_CLOSED_HAT,
                                 DRUM_KICK, DRUM_CLOSED_HAT, DRUM_SNARE, DRUM_CLOSED_HAT};
uint8_t seqGateMask = 0xFF;
uint8_t droneGateMask = 0;
uint8_t dronePairWave[kSettingCount] = {SINE, TRIANGLE, SQUARE, SAW};
uint16_t droneLfoRateHundredths[kSettingCount] = {0, 0, 0, 0};
uint16_t droneLfoShiftRaw[kSettingCount] = {0, 0, 0, 0};
uint16_t dronePitchWord[kStepCount] = {0};
uint8_t playGlide = 0;
// SEQ and PLAY keep separate macro and macro-LFO values for their selected
// programs. macroMode selects the program and state the macro helpers use:
// normally the current mode, and SEQ while PLAY triggers its background step.
struct MacroState {
  uint16_t values[4];
  uint8_t lfoRateIndex[2];
  uint8_t lfoDepth[2];
  uint32_t lfoPhase[2];
};
MacroState macroStates[2];
uint8_t macroMode = MODE_SEQ;
MacroState *macroState = &macroStates[MODE_SEQ];
unsigned long lastMacroLfoMs = 0;

uint8_t currentStep = 0;
uint8_t playingStep = 255;
unsigned long lastStepMs = 0;
uint16_t nextStepIntervalMs = 119;
uint16_t normalCurrentPitch[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
uint16_t normalTargetPitch[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
uint8_t normalGlide[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
unsigned long lastGlideMs = 0;

uint8_t heldButtonMask = 0;
uint16_t buttonPressOrder[kStepCount] = {0};
uint16_t pressOrderCounter = 0;
int8_t liveVoiceForButton[kStepCount] = {-1, -1, -1, -1, -1, -1, -1, -1};
int8_t liveButtonForVoice[SYNTH_VOICE_COUNT] = {-1, -1, -1, -1};
uint16_t liveVoiceAge[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
uint8_t arpPosition = 0;
int8_t arpDirection = 1;

uint8_t stepButtonStable[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
uint8_t stepButtonReading[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long stepButtonChangedAt[kStepCount] = {0};
uint8_t modeButtonStable = HIGH;
uint8_t modeButtonReading = HIGH;
unsigned long modeButtonChangedAt = 0;
unsigned long modeButtonPressedAt = 0;
bool modeButtonChordUsed = false;
bool modeChordStepHeld = false;
bool modePageTapPending = false;
bool modeDoubleTapArmed = false;
unsigned long modePageTapAt = 0;
bool playSequencerEnabled = true;
uint8_t parameterPage = 0;
uint16_t parameterPageRaw[4] = {0, 0, 0, 0};
bool parameterPageLocked[4] = {false, false, false, false};

uint16_t filteredSettingRaw[kSettingCount] = {0};
uint16_t filteredStepRaw[kStepCount] = {0};
uint16_t settingValues[kSettingCount] = {0};
bool settingFilterReady[kSettingCount] = {false, false, false, false};
bool stepFilterReady[kStepCount] = {false, false, false, false, false, false, false, false};
AnalogMotionState settingMotion[kSettingCount] = {};
AnalogMotionState stepMotion[kStepCount] = {};
uint8_t settingScanChannel = 0;
uint8_t stepScanChannel = 0;
uint8_t settingReadyMask = 0;
uint16_t lastDroneRaw[kStepCount] = {0};
bool droneRawReady[kStepCount] = {false, false, false, false, false, false, false, false};
bool displayDirty = true;
bool renderedInlineEditVisible = false;
uint8_t editedStep = 255;
uint8_t editedDrone = 255;
uint16_t editedDroneHz = 0;
uint8_t editedDroneLfo = 255;
uint8_t editedPlayShift = 0;
unsigned long inlineEditUntilMs = 0;
char displayFrame[2][17];
char renderedFrame[2][16];
bool renderedFrameReady = false;

void copyProgmemLabel(char *destination, const char *source, uint8_t size) {
  strncpy_P(destination, source, size - 1);
  destination[size - 1] = '\0';
}

void readMelodicPreset(uint8_t programId, MelodicPreset &preset) {
  const uint8_t index = programId - (programId > PRESET_DRUM);
  memcpy_P(&preset, &kMelodicPresets[index], sizeof(MelodicPreset));
}

void readDrumPreset(DrumPreset &preset) {
  memcpy_P(&preset, &kDrumPreset, sizeof(DrumPreset));
}

uint8_t playProgramId() { return playControls.preset + (playControls.preset >= PRESET_DRUM); }

bool isDrumProgram(uint8_t programId) {
  return programId == PRESET_DRUM;
}

void selectMacroMode(uint8_t mode) {
  macroMode = mode;
  macroState = &macroStates[mode];
}

uint8_t macroProgramId() { return macroMode == MODE_PLAY ? playProgramId() : seqControls.preset; }

void programLabel(uint8_t programId, char label[5]) {
  if (isDrumProgram(programId)) {
    DrumPreset preset; readDrumPreset(preset); memcpy(label, preset.label, sizeof(preset.label));
  } else {
    MelodicPreset preset; readMelodicPreset(programId, preset); memcpy(label, preset.label, sizeof(preset.label));
  }
}

uint8_t macroForCurrentProgram(uint8_t slot) {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) {
    DrumPreset preset; readDrumPreset(preset);
    return slot < 4 ? preset.macros[slot] : preset.lfoTargets[slot - 4];
  }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  return slot < 4 ? preset.macros[slot] : preset.lfoTargets[slot - 4];
}

// Voice 0 belongs to the background sequencer when it is running. Otherwise
// POLY can use the complete four-voice normal-oscillator pool.
uint8_t firstLiveVoice() { return playSequencerEnabled ? 1 : 0; }

// The first voice that the current macro edits and macro LFOs may change.
uint8_t firstMacroVoice() { return macroMode == MODE_PLAY ? firstLiveVoice() : 0; }

// The parameter strip is packed left-to-right.  This keeps unavailable
// controls (such as drum tuning outside DRM) off the LCD and out of the page
// cycle without needing a RAM-resident page table.
uint8_t parameterControlCount() {
  uint8_t count = instrumentMode == MODE_SEQ ? 2 : 4;
  if (instrumentMode == MODE_SEQ && isDrumProgram(seqControls.preset)) ++count;
  for (uint8_t slot = 0; slot < 4; ++slot) if (macroForCurrentProgram(slot) != MACRO_NONE) ++count;
  for (uint8_t lfo = 0; lfo < 2; ++lfo) if (macroForCurrentProgram(4 + lfo) != MACRO_NONE) count += 2;
  return count;
}

uint8_t parameterControlAt(uint8_t index) {
  const bool sequence = instrumentMode == MODE_SEQ;
  const uint8_t fixedCount = sequence ? (isDrumProgram(seqControls.preset) ? 3 : 2) : 4;
  if (index < fixedCount) {
    if (sequence) return index == 0 ? PARAM_SEQ_PRESET : (index == 1 ? PARAM_SEQ_BPM : PARAM_SEQ_TUNE);
    return PARAM_PLAY_PRESET + index;
  }
  index -= fixedCount;
  for (uint8_t slot = 0; slot < 4; ++slot) {
    if (macroForCurrentProgram(slot) == MACRO_NONE) continue;
    if (index-- == 0) return PARAM_MACRO_0 + slot;
  }
  for (uint8_t lfo = 0; lfo < 2; ++lfo) {
    if (macroForCurrentProgram(4 + lfo) == MACRO_NONE) continue;
    if (index-- == 0) return PARAM_LFO_0_RATE + lfo * 2;
    if (index-- == 0) return PARAM_LFO_0_DEPTH + lfo * 2;
  }
  return PARAM_NONE;
}

uint8_t parameterControlForPot(uint8_t pot) {
  return parameterControlAt(parameterPage * kSettingCount + pot);
}

uint8_t parameterPageCount() {
  return (parameterControlCount() + kSettingCount - 1) / kSettingCount;
}

// Keep preset pulse widths musically useful while retaining one-byte storage.
uint16_t pulseWidthForPercent(uint8_t percent) {
  return (uint16_t)constrain(percent, 5, 95) * 655U;
}

uint16_t pitchWordForFrequency(uint16_t frequency) {
  return ((uint32_t)frequency * 65535UL + FS / 2) / FS;
}

uint8_t mapIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count);
uint8_t mapByteHysteresis(uint16_t raw, uint8_t current, int16_t outMin, int16_t outMax);

// Maps the reversed control range to 94 stepped LFO rates, reserving the endpoint for off.
uint16_t lfoRateForIndex(uint8_t index) {
  if (index < 16) return kLfoFrequencyMinHundredths + index;
  if (index < 64) return 20 + (uint16_t)(index - 15) * 10;
  return 500 + (uint16_t)(index - 63) * 50;
}

// Preset VCA modulation keeps a compact, editable index while covering the
// full 0.01-20 Hz range: 0.01-0.20 in 0.01 Hz, 0.3-5.0 in 0.1 Hz, then
// 5.5-20.0 in 0.5 Hz increments.
uint16_t presetVcaRateHundredths(uint8_t modulation) {
  if (modulation == 0) return 0;
  if (modulation <= 20) return modulation;
  if (modulation <= 68) return 20 + (uint16_t)(modulation - 20) * 10;
  return 500 + (uint16_t)(min(modulation, (uint8_t)(kPresetVcaModulationCount - 1)) - 68) * 50;
}

uint8_t lfoRateIndexForHundredths(uint16_t rate) {
  if (rate <= 20) return rate - kLfoFrequencyMinHundredths;
  if (rate <= 500) return 15 + (rate - 20) / 10;
  return 63 + (rate - 500) / 50;
}

uint16_t lfoRateForRaw(uint16_t raw, uint16_t current) {
  if (raw <= kAnalogEndpointMargin) return kLfoFrequencyMaxHundredths;
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;
  const uint8_t index = current == 0 ? 0 : lfoRateIndexForHundredths(current);
  return lfoRateForIndex(mapIndexHysteresis(raw, index, kLfoRateCount));
}

// Returns the direct-macro slot assigned to parameter, or 4 when there is none.
uint8_t macroSlot(uint8_t parameter) {
  uint8_t slot = 0;
  while (slot < 4 && macroForCurrentProgram(slot) != parameter) ++slot;
  return slot;
}

uint16_t activeMacroValue(uint8_t parameter, uint16_t fallback) {
  const uint8_t slot = macroSlot(parameter);
  return slot < 4 ? macroState->values[slot] : fallback;
}

uint16_t macroMinimum(uint8_t parameter) {
  switch (parameter) {
    case MACRO_PULSE_WIDTH: return 5;
    case MACRO_VIBRATO: return 64;
    default: return 0;
  }
}

uint16_t macroMaximum(uint8_t parameter) {
  switch (parameter) {
    case MACRO_PULSE_WIDTH: return 95;
    case MACRO_VIBRATO:
    case MACRO_DRUM_TUNE:
    case MACRO_DRUM_DECAY:
    case MACRO_GLIDE: return 127;
    case MACRO_VCA_DEPTH: return 256;
    case MACRO_VCA_RATE: return kPresetVcaModulationCount - 1;
    case MACRO_MORPH: return 127;
    case MACRO_CHORUS: return SYNTH_CHORUS_2;
    default: return 0;
  }
}

// Resolves all LFOs addressed to a numeric macro. At depth 127 an LFO can
// sweep across that macro's complete valid range; clamping preserves ranges
// when the direct macro is not centred. Descriptor/list values are never
// numeric macro targets, so they cannot reach this function.
uint16_t lfoModulatedMacroValue(uint8_t parameter, uint16_t fallback) {
  const uint16_t minimum = macroMinimum(parameter);
  const uint16_t maximum = macroMaximum(parameter);
  if (maximum <= minimum) return fallback;

  int32_t value = constrain(activeMacroValue(parameter, fallback), minimum, maximum);
  const int16_t span = maximum - minimum;
  for (uint8_t lfo = 0; lfo < 2; ++lfo) {
    if (macroForCurrentProgram(4 + lfo) != parameter || macroState->lfoDepth[lfo] == 0) continue;
    const uint8_t phase = macroState->lfoPhase[lfo] >> 24;
    const uint8_t triangle = (phase & 0x80) ? 256U - phase : phase;
    const int16_t centred = (int16_t)triangle * 2 - 127;
    value += ((int32_t)centred * macroState->lfoDepth[lfo] * span) / (127L * 127L);
  }
  return constrain(value, minimum, maximum);
}

uint16_t macroValueForRaw(uint8_t parameter, uint16_t raw) {
  switch (parameter) {
    case MACRO_PULSE_WIDTH: return mapByteHysteresis(raw, activeMacroValue(parameter, 50), 95, 5);
    case MACRO_VIBRATO: return mapByteHysteresis(raw, activeMacroValue(parameter, 64), 127, 64);
    case MACRO_DRUM_TUNE:
    case MACRO_DRUM_DECAY: return mapByteHysteresis(raw, activeMacroValue(parameter, 127), 127, 0);
    case MACRO_GLIDE: return mapByteHysteresis(raw, activeMacroValue(parameter, 0), 127, 0);
    case MACRO_VCA_DEPTH: return mapWithHysteresis(raw, activeMacroValue(parameter, 0), 256, 0);
    case MACRO_VCA_RATE: return mapByteHysteresis(raw, activeMacroValue(parameter, 0), kPresetVcaModulationCount - 1, 0);
    case MACRO_ENVELOPE: return mapIndexHysteresis(raw, activeMacroValue(parameter, 0), SYNTH_ENVELOPE_COUNT);
    case MACRO_ENVELOPE_LENGTH: return mapByteHysteresis(raw, activeMacroValue(parameter, 127), 127, 0);
    case MACRO_CHORUS: return mapIndexHysteresis(raw,
      activeMacroValue(parameter, SYNTH_CHORUS_OFF), SYNTH_CHORUS_2 + 1);
    case MACRO_MORPH: return mapByteHysteresis(raw, activeMacroValue(parameter, 0), 127, 0);
    default: return 0;
  }
}

// LEN is a perceptual control, not the raw EFTWS index: 0-126 span roughly
// 50 ms to 1.6 s, and 127 is a held note. The engine's slowest non-zero
// phase increment is one, so the upper value is necessarily approximate.
const uint8_t kEnvelopeLengthRateAnchors[] PROGMEM = {33, 22, 15, 10, 7, 5, 3, 2, 1};

// LEN 0-126 run from the shortest to the longest envelope and 127 is HOLD, so
// turning away from HOLD reaches the longest lengths first instead of
// cutting held notes off with the shortest one.
const uint8_t kEnvelopeHoldControl = 127;

uint8_t envelopeRateForControl(uint8_t control) {
  if (control >= kEnvelopeHoldControl) return 0;
  const uint8_t segment = control >> 4;
  const uint8_t fraction = control & 0x0F;
  const uint8_t start = pgm_read_byte(&kEnvelopeLengthRateAnchors[segment]);
  const uint8_t end = pgm_read_byte(&kEnvelopeLengthRateAnchors[segment + 1]);
  return start - (((uint16_t)(start - end) * fraction + 8) >> 4);
}

uint8_t envelopeLengthIndexForRate(uint8_t rate) {
  uint8_t bestIndex = 0;
  uint16_t bestDifference = 0xFFFF;
  for (uint8_t index = 0; index < 118; ++index) {
    const uint16_t speed = pgm_read_word(&EFTWS[index]);
    const uint16_t difference = speed > rate ? speed - rate : rate - speed;
    if (difference < bestDifference) { bestDifference = difference; bestIndex = index; }
  }
  return bestIndex;
}

uint8_t envelopeLengthIndexForControl(uint8_t control) {
  return control >= kEnvelopeHoldControl ? 127 : envelopeLengthIndexForRate(envelopeRateForControl(control));
}

uint8_t envelopeLengthControlForPreset(uint8_t length) {
  const uint16_t rate = pgm_read_word(&EFTWS[length]);
  if (rate == 0) return kEnvelopeHoldControl;
  uint8_t bestControl = 0;
  uint16_t bestDifference = 0xFFFF;
  for (uint8_t control = 0; control < kEnvelopeHoldControl; ++control) {
    const uint16_t candidate = envelopeRateForControl(control);
    const uint16_t difference = candidate > rate ? candidate - rate : rate - candidate;
    if (difference < bestDifference) { bestDifference = difference; bestControl = control; }
  }
  return bestControl;
}

// Resolves a melodic preset's envelope and EFTWS length index. Without a LEN
// macro the stored length is used unchanged; LEN 0, or a stored length whose
// speed is zero, holds the constant full-level envelope until note-off.
void presetEnvelope(const MelodicPreset &preset, uint8_t &envelope, uint8_t &length) {
  const uint8_t slot = macroSlot(MACRO_ENVELOPE_LENGTH);
  bool hold;
  if (slot < 4) {
    const uint8_t control = macroState->values[slot];
    length = envelopeLengthIndexForControl(control);
    hold = control >= kEnvelopeHoldControl;
  } else {
    length = preset.length;
    hold = pgm_read_word(&EFTWS[length]) == 0;
  }
  envelope = hold ? ENVELOPE3 : activeMacroValue(MACRO_ENVELOPE, preset.voices[0].envelope);
}

void applyMacroEnvelopeToVoices() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) return;
  MelodicPreset preset; readMelodicPreset(programId, preset);
  uint8_t envelope, length;
  presetEnvelope(preset, envelope, length);
  for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) {
    edgar.setEnvelope(voice, envelope); edgar.setLength(voice, length);
  }
}

void showParameterEdit(uint8_t slot, unsigned long now) {
  editedPlayShift = slot + 1;
  editedStep = 255;
  inlineEditUntilMs = now + kEditViewMs;
  displayDirty = true;
}

void applyCurrentProgramChorus() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) { edgar.configureChorus(SYNTH_CHORUS_OFF); return; }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  edgar.configureChorus(activeMacroValue(MACRO_CHORUS, preset.chorusMode));
}

// Morph macro values span 0 (off) through 0.16-20.32 Hz.  This is deliberately
// direct-only: changing its source list from the foreground keeps it out of
// the macro-LFO update path and preserves the audio ISR's fixed workload.
void applyCurrentProgramMorph() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) { edgar.disablePresetMorph(); return; }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  const uint8_t speed = activeMacroValue(MACRO_MORPH, preset.morphSpeed);
  edgar.configurePresetMorph(preset.voices[0].wave, preset.voices[1].wave,
    (uint16_t)speed << 4, preset.voiceCount > 1);
}

void applyMacroValue(uint8_t slot, uint8_t displaySlot, uint16_t raw, unsigned long now) {
  const uint8_t parameter = macroForCurrentProgram(slot);
  if (parameter == MACRO_NONE) return;
  const uint16_t next = macroValueForRaw(parameter, raw);
  if (next == macroState->values[slot]) return;
  macroState->values[slot] = next;
  if (parameter == MACRO_PULSE_WIDTH) {
    const uint16_t width = pulseWidthForPercent(next);
    for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) edgar.setPulseWidth(voice, width);
  } else if (parameter == MACRO_DRUM_TUNE) {
    seqControls.drumTone = (uint8_t)next;
  } else if (parameter == MACRO_ENVELOPE || parameter == MACRO_ENVELOPE_LENGTH) {
    applyMacroEnvelopeToVoices();
  } else if (parameter == MACRO_GLIDE) {
    playGlide = next;
    for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) {
      normalGlide[voice] = next;
      if (next == 0 && normalCurrentPitch[voice] != 0) {
        normalCurrentPitch[voice] = normalTargetPitch[voice];
        edgar.setPitchWord(voice, normalCurrentPitch[voice]);
      }
    }
  } else if (parameter == MACRO_CHORUS) {
    edgar.configureChorus(next);
  } else if (parameter == MACRO_MORPH) {
    applyCurrentProgramMorph();
  } else if (parameter == MACRO_VCA_DEPTH || parameter == MACRO_VCA_RATE) {
    edgar.configurePlayVca(lfoModulatedMacroValue(MACRO_VCA_DEPTH, 0),
      presetVcaRateHundredths((uint8_t)lfoModulatedMacroValue(MACRO_VCA_RATE, 0)));
  }
  showParameterEdit(displaySlot, now);
}

void applyLfoControl(uint8_t slot, uint8_t displaySlot, uint16_t raw, unsigned long now) {
  const uint8_t lfo = (slot - 4) >> 1;
  const bool depth = (slot & 1) != 0;
  if (macroForCurrentProgram(4 + lfo) == MACRO_NONE) return;
  if (depth) {
    const uint8_t next = mapByteHysteresis(raw, macroState->lfoDepth[lfo], 127, 0);
    if (next == macroState->lfoDepth[lfo]) return;
    macroState->lfoDepth[lfo] = next;
  } else {
    const uint8_t next = mapIndexHysteresis(raw, macroState->lfoRateIndex[lfo], kLfoRateCount);
    if (next == macroState->lfoRateIndex[lfo]) return;
    macroState->lfoRateIndex[lfo] = next;
  }
  showParameterEdit(displaySlot, now);
}

bool parameterPotUnlocked(uint8_t pot, uint16_t raw) {
  if (!parameterPageLocked[pot]) return true;
  if (abs((int16_t)raw - (int16_t)parameterPageRaw[pot]) < kSettingMovementThreshold) return false;
  parameterPageLocked[pot] = false;
  return true;
}

void loadProgramMacroDefaults(uint8_t programId) {
  for (uint8_t slot = 0; slot < 4; ++slot) macroState->values[slot] = 64;
  macroState->lfoDepth[0] = macroState->lfoDepth[1] = 0;
  if (isDrumProgram(programId)) {
    DrumPreset preset; readDrumPreset(preset);
    for (uint8_t slot = 0; slot < 4; ++slot) {
      if (preset.macros[slot] == MACRO_DRUM_TUNE) macroState->values[slot] = seqControls.drumTone;
      if (preset.macros[slot] == MACRO_DRUM_DECAY) macroState->values[slot] = preset.decay;
    }
  } else {
    MelodicPreset preset; readMelodicPreset(programId, preset);
    for (uint8_t slot = 0; slot < 4; ++slot) {
      if (preset.macros[slot] == MACRO_PULSE_WIDTH) macroState->values[slot] = preset.squarePulseWidthPercent;
      if (preset.macros[slot] == MACRO_VIBRATO) macroState->values[slot] = preset.voices[0].modulation;
      if (preset.macros[slot] == MACRO_VCA_DEPTH) macroState->values[slot] = preset.vcaDepth;
      if (preset.macros[slot] == MACRO_VCA_RATE) macroState->values[slot] = preset.vcaModulation;
      if (preset.macros[slot] == MACRO_ENVELOPE) macroState->values[slot] = preset.voices[0].envelope;
      if (preset.macros[slot] == MACRO_ENVELOPE_LENGTH) macroState->values[slot] = envelopeLengthControlForPreset(preset.length);
      if (preset.macros[slot] == MACRO_GLIDE) macroState->values[slot] = preset.glide;
      if (preset.macros[slot] == MACRO_CHORUS) macroState->values[slot] = preset.chorusMode;
      if (preset.macros[slot] == MACRO_MORPH) macroState->values[slot] = preset.morphSpeed;
    }
  }
  applyCurrentProgramChorus();
  applyCurrentProgramMorph();
}

// Macro LFOs are intentionally evaluated in the foreground at 250 Hz.  They
// only write resolved engine values, keeping descriptor lookup and routing out
// of the 20 kHz audio ISR.
void updateMacroLfos(unsigned long now) {
  if ((unsigned long)(now - lastMacroLfoMs) < 4) return;
  const uint8_t elapsed = min((unsigned long)(now - lastMacroLfoMs), 20UL);
  lastMacroLfoMs = now;
  for (uint8_t lfo = 0; lfo < 2; ++lfo) {
    const uint8_t target = macroForCurrentProgram(4 + lfo);
    if (target == MACRO_NONE || macroState->lfoDepth[lfo] == 0) continue;
    macroState->lfoPhase[lfo] += (uint32_t)lfoRateForIndex(macroState->lfoRateIndex[lfo]) * 42950UL * elapsed;
    if (target == MACRO_PULSE_WIDTH) {
      const uint16_t width = pulseWidthForPercent(lfoModulatedMacroValue(target, 50));
      for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) edgar.setPulseWidth(voice, width);
    } else if (target == MACRO_VIBRATO) {
      const uint8_t modulation = lfoModulatedMacroValue(target, 64);
      for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) edgar.setMod(voice, modulation);
    } else if (target == MACRO_VCA_DEPTH || target == MACRO_VCA_RATE) {
      edgar.configurePlayVca(lfoModulatedMacroValue(MACRO_VCA_DEPTH, 0),
        presetVcaRateHundredths((uint8_t)lfoModulatedMacroValue(MACRO_VCA_RATE, 0)));
    }
  }
}

void setup() {
  pinMode(kMuxBit0Pin, OUTPUT); pinMode(kMuxBit1Pin, OUTPUT); pinMode(kMuxBit2Pin, OUTPUT);
  pinMode(kSettingsAnalogPin, INPUT); pinMode(kStepButtonPin, INPUT_PULLUP);
  pinMode(kStepAnalogPin, INPUT); pinMode(kModeButtonPin, INPUT_PULLUP);

  edgar.begin(CHA);
  for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) {
    edgar.setupVoice(voice, SINE, 0, ENVELOPE0, 64, 64);
    edgar.stopVoice(voice);
  }
  edgar.stopAllDrones();
  edgar.setDroneMode(false);
  selectMacroMode(MODE_PLAY);
  loadProgramMacroDefaults(playProgramId());
  selectMacroMode(MODE_SEQ);
  loadProgramMacroDefaults(seqControls.preset);

  const uint16_t lowPitch = pitchWordForFrequency(kDroneFrequencyMin);
  const uint16_t highPitch = pitchWordForFrequency(kDroneFrequencyMax);
  for (uint8_t i = 0; i < kStepCount; ++i) {
    dronePitchWord[i] = lowPitch + ((highPitch - lowPitch) * i) / (kStepCount - 1);
  }

  lcd.begin(16, 2);
  nextStepIntervalMs = 15000U / seqControls.bpm;
  lastStepMs = millis();
}

void loop() {
  unsigned long now = millis();
  runSequencer(now);
  updateGlide(now);
  updateMacroLfos(now);
  scanModeButton(now);
  scanStepButtons(now);
  readControls(now);
  now = millis();
  runSequencer(now);
  updateGlide(now);
  updateMacroLfos(now);
  renderDisplay(now);
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(kMuxBit0Pin, bitRead(channel, 0));
  digitalWrite(kMuxBit1Pin, bitRead(channel, 1));
  digitalWrite(kMuxBit2Pin, bitRead(channel, 2));
  delayMicroseconds(kMuxSettleUs);
}

// Discards settling reads and returns the median to reject analog spikes.
uint16_t readMedianAnalog(uint8_t pin) {
  uint16_t samples[kAnalogSamples];
  for (uint8_t i = 0; i < kAnalogDiscardSamples; ++i) analogRead(pin);
  for (uint8_t i = 0; i < kAnalogSamples; ++i) {
    uint16_t value = analogRead(pin);
    uint8_t at = i;
    while (at > 0 && samples[at - 1] > value) { samples[at] = samples[at - 1]; --at; }
    samples[at] = value;
  }
  return samples[kAnalogSamples / 2];
}

// Smooths an analog reading with a fixed-point low-pass filter.
uint16_t filterAnalog(uint16_t raw, uint16_t &filtered, bool &ready) {
  const uint16_t fixed = raw << kAnalogFilterFractionBits;
  if (!ready) { filtered = fixed; ready = true; return raw; }
  const int16_t delta = (int16_t)fixed - (int16_t)filtered;
  filtered += (delta * kAnalogFilterAlphaNumerator) / kAnalogFilterAlphaDenominator;
  return (filtered + (1 << (kAnalogFilterFractionBits - 1))) >> kAnalogFilterFractionBits;
}

// Accepts sustained directional movement while rejecting control jitter.
uint16_t acceptAnalogMovement(uint16_t raw, AnalogMotionState &state, uint8_t threshold) {
  if (!state.ready) {
    state.accepted = raw;
    state.confirmations = 0;
    state.direction = 0;
    state.ready = true;
    return raw;
  }

  const int16_t delta = (int16_t)raw - (int16_t)state.accepted;
  if (abs(delta) < threshold) {
    state.confirmations = 0;
    state.direction = 0;
    return state.accepted;
  }

  const int8_t direction = delta > 0 ? 1 : -1;
  if (direction != state.direction) {
    state.direction = direction;
    state.confirmations = 1;
  } else if (state.confirmations < kAnalogMovementConfirmations) {
    ++state.confirmations;
  }

  if (state.confirmations >= kAnalogMovementConfirmations) {
    state.accepted = raw;
    state.confirmations = 0;
    state.direction = 0;
  }
  return state.accepted;
}

uint16_t rawForMappedValue(int16_t value, int16_t outMin, int16_t outMax) {
  if (outMin == outMax) return 0;
  return constrain(map(value, outMin, outMax, 0, 1023), 0, 1023);
}

// Maps a control only after it clears the next value boundary and hysteresis.
int16_t mapWithHysteresis(uint16_t raw, int16_t current, int16_t outMin, int16_t outMax) {
  const int16_t low = min(outMin, outMax);
  const int16_t high = max(outMin, outMax);
  if (raw <= kAnalogEndpointMargin) return outMin;
  if (raw >= 1023 - kAnalogEndpointMargin) return outMax;
  const int16_t candidate = constrain(map(raw, 0, 1023, outMin, outMax), low, high);
  if (candidate == current) return current;
  const int16_t next = current + (candidate > current ? 1 : -1);
  const uint16_t currentRaw = rawForMappedValue(current, outMin, outMax);
  const uint16_t nextRaw = rawForMappedValue(next, outMin, outMax);
  const uint16_t boundary = (currentRaw + nextRaw) / 2;
  if ((nextRaw > currentRaw && raw > boundary + kAnalogHysteresis) ||
      (nextRaw < currentRaw && raw + kAnalogHysteresis < boundary)) return candidate;
  return current;
}

uint8_t mapByteHysteresis(uint16_t raw, uint8_t current, int16_t outMin, int16_t outMax) {
  return (uint8_t)mapWithHysteresis(raw, current, outMin, outMax);
}

// Maps a reversed control to a stable discrete index.
uint8_t mapIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count) {
  if (count <= 1) return 0;
  if (raw <= kAnalogEndpointMargin) return count - 1;
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;

  // raw and count are 16-bit on the ATmega328P.  Keep this product wide: the
  // LFO's 94 bins otherwise overflow above roughly two thirds of the pot,
  // making its 0.05-0.20 Hz range unreachable.
  const uint8_t forwardBin = min((uint8_t)(((uint32_t)raw * count) >> 10), (uint8_t)(count - 1));
  const uint8_t candidate = count - 1 - forwardBin;
  if (current >= count || candidate == current) return candidate;

  if (candidate < current) {
    const uint16_t boundary = ((uint16_t)(count - current) << 10) / count;
    if (raw > boundary + kAnalogHysteresis) return candidate;
  } else {
    const uint16_t boundary = ((uint16_t)(count - 1 - current) << 10) / count;
    if (raw + kAnalogHysteresis < boundary) return candidate;
  }
  return current;
}

void revoiceHeldButtons();

void lockParameterPageControls() {
  for (uint8_t pot = 0; pot < kSettingCount; ++pot) {
    parameterPageRaw[pot] = settingValues[pot];
    parameterPageLocked[pot] = true;
  }
}

void applyParameterControl(uint8_t control, uint8_t pot, uint16_t raw, unsigned long now) {
  if (control >= PARAM_MACRO_0 && control <= PARAM_MACRO_3) {
    applyMacroValue(control - PARAM_MACRO_0, pot, raw, now);
    return;
  }
  if (control >= PARAM_LFO_0_RATE && control <= PARAM_LFO_1_DEPTH) {
    applyLfoControl(4 + control - PARAM_LFO_0_RATE, pot, raw, now);
    return;
  }

  uint8_t next;
  switch (control) {
    case PARAM_SEQ_PRESET:
      next = mapIndexHysteresis(raw, seqControls.preset, kPresetCount);
      if (next != seqControls.preset) {
        if (isDrumProgram(seqControls.preset) || isDrumProgram(next)) { edgar.stopDrum(); stopNormalVoices(); }
        seqControls.preset = next; loadProgramMacroDefaults(next); lockParameterPageControls(); showParameterEdit(pot, now);
      }
      break;
    case PARAM_SEQ_BPM:
      next = mapByteHysteresis(raw, seqControls.bpm, 60, 220);
      if (next != seqControls.bpm) { seqControls.bpm = next; showParameterEdit(pot, now); }
      break;
    case PARAM_SEQ_TUNE:
      next = mapByteHysteresis(raw, seqControls.drumTone, 127, 0);
      if (next != seqControls.drumTone) { seqControls.drumTone = next; showParameterEdit(pot, now); }
      break;
    case PARAM_PLAY_PRESET:
      next = mapIndexHysteresis(raw, playControls.preset, kPlayPresetCount);
      if (next != playControls.preset) { playControls.preset = next; loadProgramMacroDefaults(playProgramId()); lockParameterPageControls(); applyLiveConfiguration(); revoiceHeldButtons(); showParameterEdit(pot, now); }
      break;
    case PARAM_PLAY_ROOT:
      next = mapIndexHysteresis(raw, playControls.root, 12);
      if (next != playControls.root) { playControls.root = next; revoiceHeldButtons(); showParameterEdit(pot, now); }
      break;
    case PARAM_PLAY_SCALE:
      next = mapIndexHysteresis(raw, playControls.scale, kScaleCount);
      if (next != playControls.scale) { playControls.scale = next; revoiceHeldButtons(); showParameterEdit(pot, now); }
      break;
    case PARAM_PLAY_STYLE:
      next = mapIndexHysteresis(raw, playControls.style, kPlayStyleCount);
      if (next != playControls.style) { playControls.style = next; arpPosition = 0; arpDirection = 1; revoiceHeldButtons(); showParameterEdit(pot, now); }
      break;
  }
}

void readParameterPageControls(const uint16_t values[], unsigned long now) {
  for (uint8_t pot = 0; pot < kSettingCount; ++pot) {
    const uint8_t control = parameterControlForPot(pot);
    if (control != PARAM_NONE && parameterPotUnlocked(pot, values[pot])) {
      applyParameterControl(control, pot, values[pot], now);
    }
  }
}

// Scans a subset of multiplexed controls each loop to limit latency.
void readControls(unsigned long now) {
  const uint8_t setting = settingScanChannel;
  selectMuxChannel(setting);
  const uint16_t settingFiltered = filterAnalog(
    readMedianAnalog(kSettingsAnalogPin), filteredSettingRaw[setting], settingFilterReady[setting]);
  settingValues[setting] = acceptAnalogMovement(
    settingFiltered, settingMotion[setting], kSettingMovementThreshold);
  settingReadyMask |= 1 << setting;
  settingScanChannel = (settingScanChannel + 1) % kSettingCount;

  if (settingReadyMask == ((1 << kSettingCount) - 1)) {
    if (instrumentMode != MODE_DRONE) readParameterPageControls(settingValues, now);
    else readDroneControls(settingValues, now);
  }

  for (uint8_t i = 0; i < kStepPotsPerLoop; ++i) {
    const uint8_t step = stepScanChannel;
    if (instrumentMode == MODE_SEQ) readSequencerStepPot(step, now);
    else if (instrumentMode == MODE_PLAY) readPlayStepPot(step, now);
    else readDronePitchPot(step, now);
    stepScanChannel = (stepScanChannel + 1) % kStepCount;
  }
}

// MODE + a DRONE parameter pot edits that pair's amplitude-LFO rate.
void readDroneControls(const uint16_t values[], unsigned long now) {
  if (modeButtonStable == LOW) {
    for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
      if (values[pair] == droneLfoShiftRaw[pair]) continue;
      droneLfoShiftRaw[pair] = values[pair];
      const uint16_t rate = lfoRateForRaw(values[pair], droneLfoRateHundredths[pair]);
      if (rate == droneLfoRateHundredths[pair]) continue;
      droneLfoRateHundredths[pair] = rate;
      edgar.setDroneLfoRate(pair, rate);
      modeButtonChordUsed = true;
      editedDroneLfo = pair;
      editedDrone = 255;
      inlineEditUntilMs = now + kEditViewMs;
      displayDirty = true;
    }
    return;
  }
  for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
    const uint8_t next = mapIndexHysteresis(values[pair], dronePairWave[pair], kDroneWaveCount);
    if (next != dronePairWave[pair]) {
      dronePairWave[pair] = next; edgar.setDroneWave(pair * 2, next); edgar.setDroneWave(pair * 2 + 1, next);
      displayDirty = true;
    }
  }
}

void readSequencerStepPot(uint8_t step, unsigned long now) {
  selectMuxChannel(step);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAnalogPin), filteredStepRaw[step], stepFilterReady[step]);
  const bool firstRead = !stepMotion[step].ready;
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[step], kStepMovementThreshold);
  const bool drumMode = isDrumProgram(seqControls.preset);
  const uint8_t current = drumMode ? stepDrums[step] : stepNotes[step];
  const uint8_t next = drumMode
    ? mapIndexHysteresis(raw, current, kDrumCount)
    : (firstRead
        ? (uint8_t)constrain(map(raw, 0, 1023, kStepNoteMax, kStepNoteMin), kStepNoteMin, kStepNoteMax)
        : mapByteHysteresis(raw, current, kStepNoteMax, kStepNoteMin));
  if (next != current) {
    if (drumMode) stepDrums[step] = next;
    else stepNotes[step] = next;
    editedStep = step;
    editedPlayShift = 0;
    editedDrone = 255;
    inlineEditUntilMs = now + kEditViewMs;
    displayDirty = true;
  }
}

void readPlayStepPot(uint8_t step, unsigned long now) {
  selectMuxChannel(step);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAnalogPin), filteredStepRaw[step], stepFilterReady[step]);
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[step], kStepMovementThreshold);
  if (step == 0) {
    const uint8_t next = mapIndexHysteresis(raw, playControls.octave, 4);
    if (next != playControls.octave) { playControls.octave = next; revoiceHeldButtons(); displayDirty = true; }
  }
}


void readDronePitchPot(uint8_t voice, unsigned long now) {
  const uint16_t lowPitch = pitchWordForFrequency(kDroneFrequencyMin);
  const uint16_t highPitch = pitchWordForFrequency(kDroneFrequencyMax);
  selectMuxChannel(voice);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAnalogPin), filteredStepRaw[voice], stepFilterReady[voice]);
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[voice], kDroneMovementThreshold);
  const bool showEdit = droneRawReady[voice];
  if (showEdit && raw == lastDroneRaw[voice]) return;
  droneRawReady[voice] = true;
  lastDroneRaw[voice] = raw;
  dronePitchWord[voice] = highPitch - ((uint32_t)(highPitch - lowPitch) * raw) / 1023;
  edgar.setDronePitchWord(voice, dronePitchWord[voice]);
  const uint16_t hz = ((uint32_t)dronePitchWord[voice] * FS + 32767UL) / 65535UL;
  if (showEdit) {
    editedDrone = voice;
      editedDroneLfo = 255;
    editedStep = 255;
    editedDroneHz = hz;
    inlineEditUntilMs = now + kEditViewMs;
    displayDirty = true;
  }
}

// One short MODE tap pages through controls; two quick taps change mode.
void scanModeButton(unsigned long now) {
  const uint8_t reading = digitalRead(kModeButtonPin);
  if (reading != modeButtonReading) { modeButtonReading = reading; modeButtonChangedAt = now; }
  if ((unsigned long)(now - modeButtonChangedAt) >= kButtonDebounceMs && reading != modeButtonStable) {
    modeButtonStable = reading;
    if (reading == LOW) {
      modeButtonPressedAt = now;
      modeButtonChordUsed = false;
      // The second tap counts when it is pressed within the window after the
      // first release; requiring its release there was too tight for a real,
      // debounced button.
      modeDoubleTapArmed = modePageTapPending &&
        (unsigned long)(now - modePageTapAt) <= kModeDoubleTapMs;
      if (instrumentMode == MODE_DRONE) {
        for (uint8_t pair = 0; pair < kSettingCount; ++pair) droneLfoShiftRaw[pair] = settingValues[pair];
      }
    } else if (!modeButtonChordUsed &&
               (unsigned long)(now - modeButtonPressedAt) < kModeIgnoreHoldMs) {
      if (modeDoubleTapArmed) {
        modeDoubleTapArmed = false;
        modePageTapPending = false;
        changeMode((InstrumentMode)((instrumentMode + 1) % 3), now);
      } else {
        modePageTapPending = true;
        modePageTapAt = now;
      }
    } else if (!modeButtonChordUsed) {
      // A held MODE is only a modifier; it is never a standalone gesture.
      modePageTapPending = false;
    }
  }
  if (modeButtonStable == HIGH && modePageTapPending &&
      (unsigned long)(now - modePageTapAt) > kModeDoubleTapMs) {
    modePageTapPending = false;
    if (instrumentMode == MODE_DRONE) {
      // DRONE retains its dedicated controls and has no performance pages.
      displayDirty = true;
    } else {
      parameterPage = (parameterPage + 1) % parameterPageCount();
      lockParameterPageControls();
      editedPlayShift = 0;
      editedStep = 255;
      displayDirty = true;
    }
  }
}

void scanStepButtons(unsigned long now) {
  for (uint8_t button = 0; button < kStepCount; ++button) {
    selectMuxChannel(button);
    const uint8_t reading = digitalRead(kStepButtonPin);
    if (reading != stepButtonReading[button]) { stepButtonReading[button] = reading; stepButtonChangedAt[button] = now; }
    if ((unsigned long)(now - stepButtonChangedAt[button]) >= kButtonDebounceMs && reading != stepButtonStable[button]) {
      stepButtonStable[button] = reading; handleStepButton(button, reading == LOW);
    }
  }
}

// Routes step buttons to gates, drone toggles, or live notes by mode.
void handleStepButton(uint8_t button, bool pressed) {
  if (button == 0 && modeChordStepHeld) {
    if (!pressed) modeChordStepHeld = false;
    return;
  }
  if (instrumentMode == MODE_PLAY && button == 0 && pressed && modeButtonStable == LOW) {
    playSequencerEnabled = !playSequencerEnabled;
    edgar.setVoiceZeroBackground(playSequencerEnabled);
    modeButtonChordUsed = true;
    modeChordStepHeld = true;
    modePageTapPending = false;
    stopNormalVoice(0); edgar.stopDrum();
    revoiceHeldButtons();
    displayDirty = true;
    return;
  }
  if (instrumentMode == MODE_SEQ) {
    if (pressed) { seqGateMask ^= (1 << button); displayDirty = true; }
    return;
  }
  if (instrumentMode == MODE_DRONE) {
    if (pressed) {
      droneGateMask ^= (1 << button); edgar.setDroneGate(button, bitRead(droneGateMask, button)); displayDirty = true;
    }
    return;
  }
  if (pressed) {
    heldButtonMask |= (1 << button); buttonPressOrder[button] = ++pressOrderCounter; handleLivePress(button);
  } else {
    heldButtonMask &= ~(1 << button); handleLiveRelease(button);
  }
  displayDirty = true;
}

// Stops the previous engine and initializes the selected mode safely.
void changeMode(InstrumentMode nextMode, unsigned long now) {
  if (nextMode == instrumentMode) return;
  const InstrumentMode previous = instrumentMode;
  if (previous == MODE_DRONE) { edgar.stopAllDrones(); edgar.setDroneMode(false); }
  if (previous == MODE_PLAY) {
    clearHeldNotes();
    edgar.disablePresetMorph();
    edgar.disablePlayVca();
  }
  edgar.stopDrum();
  stopNormalVoices();
  instrumentMode = nextMode;
  if (nextMode != MODE_DRONE) selectMacroMode(nextMode);
  edgar.setVoiceZeroBackground(nextMode == MODE_PLAY && playSequencerEnabled);
  parameterPage = 0;
  editedStep = 255;
  editedPlayShift = 0;
  lockParameterPageControls();

  if (nextMode == MODE_DRONE) {
    for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
      edgar.setDroneWave(pair * 2, dronePairWave[pair]);
      edgar.setDroneWave(pair * 2 + 1, dronePairWave[pair]);
      edgar.setDroneLfoRate(pair, droneLfoRateHundredths[pair]);
    }
    for (uint8_t voice = 0; voice < kStepCount; ++voice) {
      droneRawReady[voice] = false;
      edgar.setDronePitchWord(voice, dronePitchWord[voice]);
      edgar.setDroneGate(voice, bitRead(droneGateMask, voice));
    }
    edgar.setDroneMode(true);
  } else if (nextMode == MODE_PLAY) {
    applyLiveConfiguration();
  } else {
    applyCurrentProgramChorus();
    applyCurrentProgramMorph();
    if (previous == MODE_DRONE) {
      currentStep = 0;
      playingStep = 0;
      if (bitRead(seqGateMask, 0)) triggerSequencerStep(0, false);
      currentStep = 1;
      lastStepMs = now;
      nextStepIntervalMs = bpmStepMs();
    }
  }
  displayDirty = true;
}

void stopNormalVoice(uint8_t voice) {
  edgar.stopVoice(voice); normalCurrentPitch[voice] = 0; normalTargetPitch[voice] = 0; normalGlide[voice] = 0;
}

void stopNormalVoices() {
  for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
}

uint16_t pitchForNote(int16_t note) {
  note = constrain(note, 0, SYNTH_MIDI_NOTE_MAX);
  return pgm_read_word(&PITCHS[note]);
}

// Presets may specify every integer cent from -1200 to +1200. A nearby pitch
// table value keeps the control-rate calculation compact; the residual is
// then applied as a fixed-point detune correction.
uint16_t pitchForNoteWithDetuneCents(int16_t note, int16_t detuneCents) {
  detuneCents = constrain(detuneCents, -1200, 1200);
  const int16_t semitones = detuneCents >= 0 ?
    (detuneCents + 50) / 100 : (detuneCents - 50) / 100;
  const int16_t residualCents = detuneCents - semitones * 100;
  const uint16_t basePitch = pitchForNote(note + semitones);
  const int32_t result = basePitch +
    ((int32_t)basePitch * residualCents * 578L) / 1000000L;
  return constrain(result, 1, 65535);
}

void triggerNormalVoice(uint8_t voice, uint16_t pitch, uint8_t glide) {
  normalTargetPitch[voice] = pitch; normalGlide[voice] = glide;
  if (glide == 0 || normalCurrentPitch[voice] == 0) {
    normalCurrentPitch[voice] = pitch; edgar.setPitchWord(voice, pitch);
  }
  edgar.trigger(voice);
}

// Moves active voices toward their target pitch once per millisecond.
void updateGlide(unsigned long now) {
  if ((unsigned long)(now - lastGlideMs) < 1) return;
  lastGlideMs = now;
  for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) {
    if (normalGlide[voice] == 0 || normalCurrentPitch[voice] == 0 || normalCurrentPitch[voice] == normalTargetPitch[voice]) continue;
    const int32_t difference = (int32_t)normalTargetPitch[voice] - normalCurrentPitch[voice];
    const uint8_t divisor = map(normalGlide[voice], 1, 127, 2, 32);
    int32_t amount = difference / divisor;
    if (amount == 0) amount = difference > 0 ? 1 : -1;
    normalCurrentPitch[voice] += amount; edgar.setPitchWord(voice, normalCurrentPitch[voice]);
  }
}

// Builds a sequencer voice stack from the selected preset.
void triggerSequencerNote(uint8_t note, bool backgroundOnly) {
  edgar.stopDrum();
  MelodicPreset preset; readMelodicPreset(seqControls.preset, preset);
  const uint8_t voiceCount = backgroundOnly ? 1 : preset.voiceCount;
  uint8_t envelope, envelopeLength;
  presetEnvelope(preset, envelope, envelopeLength);
  const uint8_t glide = activeMacroValue(MACRO_GLIDE, preset.glide);
  const uint16_t pulseWidth = pulseWidthForPercent(
    lfoModulatedMacroValue(MACRO_PULSE_WIDTH, preset.squarePulseWidthPercent));

  for (uint8_t voice = 0; voice < voiceCount; ++voice) {
    const VoiceRecipe &recipe = preset.voices[voice];
    edgar.setWave(voice, recipe.wave); edgar.setEnvelope(voice, envelope);
    edgar.setPulseWidth(voice, pulseWidth);
    edgar.setLength(voice, envelopeLength);
    edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, recipe.modulation));
    triggerNormalVoice(voice, pitchForNoteWithDetuneCents(note, recipe.detuneCents), glide);
  }
  if (!backgroundOnly) for (uint8_t voice = voiceCount; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
}

void triggerDrum(uint8_t drum, bool backgroundOnly) {
  if (backgroundOnly) stopNormalVoice(0);
  else stopNormalVoices();
  normalCurrentPitch[0] = 0;
  normalTargetPitch[0] = 0;
  normalGlide[0] = 0;
  DrumPreset preset; readDrumPreset(preset);
  const uint8_t decay = lfoModulatedMacroValue(MACRO_DRUM_DECAY, preset.decay);
  const uint8_t tone = lfoModulatedMacroValue(MACRO_DRUM_TUNE, seqControls.drumTone);
  edgar.triggerDrum(min(drum, (uint8_t)(kDrumCount - 1)), tone, decay);
}

void triggerSequencerStep(uint8_t step, bool backgroundOnly) {
  if (isDrumProgram(seqControls.preset)) triggerDrum(stepDrums[step], backgroundOnly);
  else triggerSequencerNote(stepNotes[step], backgroundOnly);
}

uint16_t bpmStepMs() { return 15000U / max((uint8_t)1, seqControls.bpm); }

// Advances the shared sequencer without accumulating large timing drift.
void runSequencer(unsigned long now) {
  if (instrumentMode == MODE_DRONE || (unsigned long)(now - lastStepMs) < nextStepIntervalMs) return;
  lastStepMs += nextStepIntervalMs;
  nextStepIntervalMs = bpmStepMs();
  if ((unsigned long)(now - lastStepMs) >= nextStepIntervalMs) lastStepMs = now;
  playingStep = currentStep;
  if (instrumentMode == MODE_PLAY) {
    if (playSequencerEnabled) {
      if (bitRead(seqGateMask, currentStep)) {
        selectMacroMode(MODE_SEQ);
        triggerSequencerStep(currentStep, true);
        selectMacroMode(MODE_PLAY);
      } else { stopNormalVoice(0); edgar.stopDrum(); }
    }
  } else if (bitRead(seqGateMask, currentStep)) {
    triggerSequencerStep(currentStep, false);
  } else {
    edgar.stopDrum(); stopNormalVoices();
  }
  currentStep = (currentStep + 1) % kStepCount;
  if (instrumentMode == MODE_PLAY && playControls.style >= STYLE_ARP_UP) advanceArpeggio();
  if (instrumentMode == MODE_SEQ) displayDirty = true;
}

void applyLiveConfiguration() {
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  applyCurrentProgramChorus();
  const uint16_t pulseWidth = pulseWidthForPercent(lfoModulatedMacroValue(MACRO_PULSE_WIDTH, preset.squarePulseWidthPercent));
  uint8_t envelope, envelopeLength;
  presetEnvelope(preset, envelope, envelopeLength);
  playGlide = activeMacroValue(MACRO_GLIDE, preset.glide);
  edgar.configurePlayVca(lfoModulatedMacroValue(MACRO_VCA_DEPTH, preset.vcaDepth),
    presetVcaRateHundredths((uint8_t)lfoModulatedMacroValue(MACRO_VCA_RATE, preset.vcaModulation)));
  applyCurrentProgramMorph();
  for (uint8_t voice = firstLiveVoice(); voice < SYNTH_VOICE_COUNT; ++voice) {
    edgar.setWave(voice, preset.voices[0].wave);
    edgar.setEnvelope(voice, envelope);
    edgar.setPulseWidth(voice, pulseWidth);
    edgar.setLength(voice, envelopeLength); edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, preset.voices[0].modulation));
  }
}

uint8_t liveNoteForButton(uint8_t button) {
  const int8_t offset = pgm_read_byte(&kScaleOffsets[playControls.scale][button]);
  return constrain(36 + playControls.octave * 12 + playControls.root + offset, 0, 127);
}

int8_t scaleDegreeOffset(uint8_t scale, uint8_t degree) {
  const uint8_t length = pgm_read_byte(&kScaleLengths[scale]);
  return pgm_read_byte(&kScaleOffsets[scale][degree % length]) + (degree / length) * 12;
}

int8_t latestHeldButton() {
  int8_t latest = -1; uint16_t latestOrder = 0;
  for (uint8_t button = 0; button < kStepCount; ++button) {
    if (bitRead(heldButtonMask, button) && buttonPressOrder[button] >= latestOrder) {
      latestOrder = buttonPressOrder[button]; latest = button;
    }
  }
  return latest;
}

void resetLiveAssignments() {
  for (uint8_t button = 0; button < kStepCount; ++button) liveVoiceForButton[button] = -1;
  for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) {
    liveButtonForVoice[voice] = -1; liveVoiceAge[voice] = 0;
  }
}

void clearHeldNotes() {
  heldButtonMask = 0;
  for (uint8_t voice = firstLiveVoice(); voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
  resetLiveAssignments();
}

// Assigns a free live voice or steals the oldest active one.
void triggerPolyButton(uint8_t button) {
  const uint8_t firstVoice = firstLiveVoice();
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  // A multi-voice program is a unison stack in POLY.  It consumes the whole
  // live pool: four oscillators normally, or three while voice 0 sequences.
  if (preset.voiceCount > 1) {
    const uint8_t availableVoiceCount = SYNTH_VOICE_COUNT - firstVoice;
    const uint8_t stackVoiceCount = min(preset.voiceCount, availableVoiceCount);
    const uint8_t note = liveNoteForButton(button);
    for (uint8_t stackVoice = 0; stackVoice < stackVoiceCount; ++stackVoice) {
      const uint8_t voice = firstVoice + stackVoice;
      const VoiceRecipe recipe = preset.voices[stackVoice];
      edgar.setWave(voice, recipe.wave);
      edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, recipe.modulation));
      triggerNormalVoice(voice, pitchForNoteWithDetuneCents(note, recipe.detuneCents), playGlide);
    }
    for (uint8_t voice = firstVoice + stackVoiceCount; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
    return;
  }
  int8_t voice = -1;
  for (uint8_t candidate = firstVoice; candidate < SYNTH_VOICE_COUNT; ++candidate) {
    if (liveButtonForVoice[candidate] < 0) { voice = candidate; break; }
  }
  if (voice < 0) {
    voice = firstVoice;
    for (uint8_t candidate = firstVoice + 1; candidate < SYNTH_VOICE_COUNT; ++candidate) {
      if (liveVoiceAge[candidate] < liveVoiceAge[voice]) voice = candidate;
    }
    const int8_t stolenButton = liveButtonForVoice[voice];
    if (stolenButton >= 0) liveVoiceForButton[stolenButton] = -1;
  }
  liveButtonForVoice[voice] = button; liveVoiceForButton[button] = voice; liveVoiceAge[voice] = buttonPressOrder[button];
  triggerNormalVoice(voice, pitchForNote(liveNoteForButton(button)), playGlide);
}

void triggerHeldChord() {
  const int8_t button = latestHeldButton();
  if (button < 0) {
    for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
    return;
  }
  const int16_t root = 36 + playControls.octave * 12 + playControls.root;
  for (uint8_t chordVoice = 0; chordVoice < 3; ++chordVoice) {
    const int8_t offset = scaleDegreeOffset(playControls.scale, button + chordVoice * 2);
    triggerNormalVoice(chordVoice + 1, pitchForNote(root + offset), playGlide);
  }
}

uint8_t collectHeldButtons(uint8_t buttons[kStepCount]) {
  uint8_t count = 0;
  for (uint8_t button = 0; button < kStepCount; ++button) if (bitRead(heldButtonMask, button)) buttons[count++] = button;
  return count;
}

// Advances the current arpeggio pattern across held buttons, or the latest held chord.
void advanceArpeggio() {
  uint8_t buttons[kStepCount];
  const uint8_t count = collectHeldButtons(buttons);
  const uint8_t voice = 1;
  for (uint8_t other = voice + 1; other < SYNTH_VOICE_COUNT; ++other) stopNormalVoice(other);
  if (count == 0) { stopNormalVoice(voice); arpPosition = 0; arpDirection = 1; return; }

  const bool chordArpeggio = playControls.style >= STYLE_CHORD_ARP_UP;
  const uint8_t noteCount = chordArpeggio ? 3 : count;
  const int8_t chordButton = chordArpeggio ? latestHeldButton() : -1;
  const int16_t chordRoot = 36 + playControls.octave * 12 + playControls.root;

  if (chordArpeggio) {
    arpPosition %= noteCount;
    uint8_t chordVoice = arpPosition;
    if (playControls.style == STYLE_CHORD_ARP_DOWN) chordVoice = noteCount - 1 - arpPosition;
    else if (playControls.style == STYLE_CHORD_ARP_UPDOWN && noteCount > 1) {
      if (arpPosition >= noteCount) arpPosition = noteCount - 1;
      chordVoice = arpPosition;
      if (arpDirection > 0 && arpPosition >= noteCount - 1) arpDirection = -1;
      else if (arpDirection < 0 && arpPosition == 0) arpDirection = 1;
      arpPosition += arpDirection;
      triggerNormalVoice(voice, pitchForNote(chordRoot + scaleDegreeOffset(playControls.scale, chordButton + chordVoice * 2)), playGlide);
      return;
    }
    triggerNormalVoice(voice, pitchForNote(chordRoot + scaleDegreeOffset(playControls.scale, chordButton + chordVoice * 2)), playGlide);
    arpPosition = (arpPosition + 1) % noteCount;
    return;
  }

  if (playControls.style == STYLE_ARP_DOWN) {
    arpPosition %= count;
    triggerNormalVoice(voice, pitchForNote(liveNoteForButton(buttons[count - 1 - arpPosition])), playGlide);
    arpPosition = (arpPosition + 1) % count;
    return;
  }
  if (playControls.style == STYLE_ARP_UPDOWN && count > 1) {
    if (arpPosition >= count) arpPosition = count - 1;
    triggerNormalVoice(voice, pitchForNote(liveNoteForButton(buttons[arpPosition])), playGlide);
    if (arpDirection > 0 && arpPosition >= count - 1) arpDirection = -1;
    else if (arpDirection < 0 && arpPosition == 0) arpDirection = 1;
    arpPosition += arpDirection;
    return;
  }
  arpPosition %= count;
  triggerNormalVoice(voice, pitchForNote(liveNoteForButton(buttons[arpPosition])), playGlide);
  arpPosition = (arpPosition + 1) % count;
}

void handleLivePress(uint8_t button) {
  applyLiveConfiguration();
  if (playControls.style == STYLE_POLY) triggerPolyButton(button);
  else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  // Arpeggios read heldButtonMask only from runSequencer(), so a key pressed
  // between clocks cannot retrigger an off-grid note.
  else if ((heldButtonMask & ~(1 << button)) == 0) { arpPosition = 0; arpDirection = 1; }
}

void handleLiveRelease(uint8_t button) {
  if (playControls.style == STYLE_POLY) {
    MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
    if (preset.voiceCount > 1) {
      revoiceHeldButtons();
      return;
    }
    const int8_t voice = liveVoiceForButton[button];
    if (voice >= firstLiveVoice()) {
      stopNormalVoice(voice); liveButtonForVoice[voice] = -1; liveVoiceForButton[button] = -1;
    }
  } else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  // Keep the current arp note until the next sequencer clock, where the
  // updated held-button set is applied.
}

// Rebuilds live voice assignments after a play setting changes.
void revoiceHeldButtons() {
  if (instrumentMode != MODE_PLAY) return;
  for (uint8_t voice = firstLiveVoice(); voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
  resetLiveAssignments(); applyLiveConfiguration();
  if (playControls.style == STYLE_POLY) {
    uint8_t ordered[kStepCount];
    uint8_t count = 0;
    for (uint8_t button = 0; button < kStepCount; ++button) if (bitRead(heldButtonMask, button)) ordered[count++] = button;
    for (uint8_t i = 0; i < count; ++i) {
      for (uint8_t j = i + 1; j < count; ++j) {
        if (buttonPressOrder[ordered[j]] < buttonPressOrder[ordered[i]]) {
          const uint8_t swap = ordered[i]; ordered[i] = ordered[j]; ordered[j] = swap;
        }
      }
      triggerPolyButton(ordered[i]);
    }
  } else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  else { arpPosition = 0; arpDirection = 1; advanceArpeggio(); }
}

void resetDisplayFrame() {
  for (uint8_t row = 0; row < 2; ++row) {
    memset(displayFrame[row], ' ', 16);
    displayFrame[row][16] = '\0';
  }
}

void frameChar(uint8_t row, uint8_t column, char value) {
  if (row < 2 && column < 16) displayFrame[row][column] = value;
}

void frameText(uint8_t row, uint8_t column, const char *text) {
  while (row < 2 && column < 16 && *text) displayFrame[row][column++] = *text++;
}

void frameProgmemText(uint8_t row, uint8_t column, const char *source) {
  while (row < 2 && column < 16) {
    const char value = pgm_read_byte(source++);
    if (!value) break;
    displayFrame[row][column++] = value;
  }
}

void frameText(uint8_t row, uint8_t column, const __FlashStringHelper *text) {
  frameProgmemText(row, column, (const char *)text);
}

void frameNumber(uint8_t row, uint8_t column, uint16_t value) {
  char text[6];
  utoa(value, text, 10);
  frameText(row, column, text);
}

// Writes only LCD regions that differ from the rendered frame.
void flushDisplayFrame() {
  for (uint8_t row = 0; row < 2; ++row) {
    uint8_t column = 0;
    while (column < 16) {
      while (column < 16 && renderedFrameReady &&
             displayFrame[row][column] == renderedFrame[row][column]) ++column;
      if (column >= 16) break;
      lcd.setCursor(column, row);
      while (column < 16 && (!renderedFrameReady ||
             displayFrame[row][column] != renderedFrame[row][column])) {
        lcd.write(displayFrame[row][column]);
        renderedFrame[row][column] = displayFrame[row][column];
        ++column;
      }
    }
  }
  renderedFrameReady = true;
}

// Chooses the active view and refreshes it only when needed.
void renderDisplay(unsigned long now) {
  const bool inlineEditVisible = (long)(inlineEditUntilMs - now) > 0 &&
    ((instrumentMode != MODE_DRONE && (editedStep < kStepCount || editedPlayShift != 0)) ||
     (instrumentMode == MODE_DRONE &&
      (editedDrone < kStepCount || editedDroneLfo < kSettingCount)));
  if (!displayDirty && inlineEditVisible == renderedInlineEditVisible) return;

  resetDisplayFrame();
  if (instrumentMode == MODE_SEQ) renderSequencerDashboard(inlineEditVisible);
  else if (instrumentMode == MODE_PLAY) renderPlayDashboard(inlineEditVisible);
  else renderDroneDashboard(inlineEditVisible);
  flushDisplayFrame();

  renderedInlineEditVisible = inlineEditVisible;
  displayDirty = false;
}

void frameMacroName(uint8_t column, uint8_t parameter) {
  switch (parameter) {
    case MACRO_PULSE_WIDTH: frameText(0, column, F("PWM")); break;
    case MACRO_VIBRATO: frameText(0, column, F("VIB")); break;
    case MACRO_DRUM_TUNE: frameText(0, column, F("TUN")); break;
    case MACRO_DRUM_DECAY: frameText(0, column, F("DEC")); break;
    case MACRO_VCA_DEPTH: frameText(0, column, F("VCD")); break;
    case MACRO_VCA_RATE: frameText(0, column, F("VCR")); break;
    case MACRO_ENVELOPE: frameText(0, column, F("ENV")); break;
    case MACRO_ENVELOPE_LENGTH: frameText(0, column, F("LEN")); break;
    case MACRO_GLIDE: frameText(0, column, F("GLD")); break;
    case MACRO_CHORUS: frameText(0, column, F("CHR")); break;
    case MACRO_MORPH: frameText(0, column, F("MOR")); break;
    default: frameText(0, column, F("---")); break;
  }
}

void renderParameterName(uint8_t pot) {
  const uint8_t column = pot * 4;
  const uint8_t control = parameterControlForPot(pot);
  if (control >= PARAM_MACRO_0 && control <= PARAM_MACRO_3) {
    frameMacroName(column, macroForCurrentProgram(control - PARAM_MACRO_0));
    return;
  }
  if (control >= PARAM_LFO_0_RATE && control <= PARAM_LFO_1_DEPTH) {
    frameText(0, column, (control & 1) ? F("DEP") : F("RAT"));
    return;
  }
  switch (control) {
    case PARAM_SEQ_PRESET:
    case PARAM_PLAY_PRESET: frameText(0, column, F("PRS")); break;
    case PARAM_SEQ_BPM: frameText(0, column, F("BPM")); break;
    case PARAM_SEQ_TUNE: frameText(0, column, F("TUN")); break;
    case PARAM_PLAY_ROOT: frameText(0, column, F("ROT")); break;
    case PARAM_PLAY_SCALE: frameText(0, column, F("SCL")); break;
    case PARAM_PLAY_STYLE: frameText(0, column, F("STY")); break;
    default: frameText(0, column, F("---")); break;
  }
}

void renderParameterValue(uint8_t pot) {
  if (pot >= 4) return;
  const uint8_t column = pot * 4;
  frameText(0, column, F("   "));
  const uint8_t control = parameterControlForPot(pot);
  if (control >= PARAM_MACRO_0 && control <= PARAM_MACRO_3) {
    frameNumber(0, column, macroState->values[control - PARAM_MACRO_0]);
  } else if (control >= PARAM_LFO_0_RATE && control <= PARAM_LFO_1_DEPTH) {
    const uint8_t lfo = (control - PARAM_LFO_0_RATE) >> 1;
    frameNumber(0, column, (control & 1) ? macroState->lfoDepth[lfo] : macroState->lfoRateIndex[lfo] + 1);
  } else if (control == PARAM_SEQ_PRESET || control == PARAM_PLAY_PRESET) {
    char label[5]; programLabel(control == PARAM_SEQ_PRESET ? seqControls.preset : playProgramId(), label); frameText(0, column, label);
  } else if (control == PARAM_SEQ_BPM) {
    frameNumber(0, column, seqControls.bpm);
  } else if (control == PARAM_SEQ_TUNE) {
    frameNumber(0, column, seqControls.drumTone);
  } else if (control == PARAM_PLAY_ROOT) {
    frameProgmemText(0, column, kRootLabels[playControls.root]);
  } else if (control == PARAM_PLAY_SCALE) {
    frameProgmemText(0, column, kScaleLabels[playControls.scale]);
  } else if (control == PARAM_PLAY_STYLE) {
    frameProgmemText(0, column, kStyleLabels[playControls.style]);
  }
}

void renderSequencerDashboard(bool inlineEditVisible) {
  for (uint8_t pot = 0; pot < 4; ++pot) renderParameterName(pot);
  for (uint8_t step = 0; step < kStepCount; ++step) {
    const bool enabled = bitRead(seqGateMask, step);
    frameChar(1, step, step == playingStep ? (enabled ? 'O' : 'o') : (enabled ? 'x' : '-'));
  }
  if (inlineEditVisible && editedStep < kStepCount) {
    frameChar(1, 9, 'S'); frameChar(1, 10, '1' + editedStep); frameChar(1, 11, ':');
    if (isDrumProgram(seqControls.preset)) frameProgmemText(1, 12, kDrumLabels[stepDrums[editedStep]]);
    else {
      const uint8_t note = stepNotes[editedStep];
      const char *root = kRootLabels[note % 12];
      frameChar(1, 12, pgm_read_byte(root));
      const bool sharp = pgm_read_byte(root + 1) == '#';
      if (sharp) frameChar(1, 13, '#');
      frameChar(1, sharp ? 14 : 13, '0' + note / 12 - 1);
    }
  } else if (inlineEditVisible && editedPlayShift > 0) renderParameterValue(editedPlayShift - 1);
}

void renderPlayDashboard(bool inlineEditVisible) {
  for (uint8_t pot = 0; pot < 4; ++pot) renderParameterName(pot);
  frameChar(1, 12, 'O'); frameChar(1, 13, '2' + playControls.octave);
  frameText(1, 14, playSequencerEnabled ? F("+") : F("-"));
  if (inlineEditVisible && editedPlayShift > 0) renderParameterValue(editedPlayShift - 1);
}

void renderDroneDashboard(bool inlineEditVisible) {
  frameText(0, 0, F("DRN "));
  for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
    frameChar(0, 4 + pair * 3, pgm_read_byte(&kDroneWaveLabels[dronePairWave[pair]][0]));
    frameChar(0, 5 + pair * 3, pgm_read_byte(&kDroneWaveLabels[dronePairWave[pair]][1]));
  }
  for (uint8_t voice = 0; voice < kStepCount; ++voice) {
    frameChar(1, voice, bitRead(droneGateMask, voice) ? 'x' : '-');
  }
  if (inlineEditVisible) {
    if (editedDroneLfo < kSettingCount) {
      frameChar(1, 9, 'L'); frameChar(1, 10, '1' + editedDroneLfo); frameChar(1, 11, ':');
      const uint16_t rate = droneLfoRateHundredths[editedDroneLfo];
      frameNumber(1, 12, rate == 0 ? 0 : lfoRateIndexForHundredths(rate) + 1);
    } else {
      frameChar(1, 9, 'D'); frameChar(1, 10, '1' + editedDrone); frameChar(1, 11, ':');
      frameNumber(1, 12, editedDroneHz);
    }
  }
}
