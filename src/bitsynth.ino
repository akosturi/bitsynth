#include <avr/pgmspace.h>
#include <util/delay.h>
#include "synth.h"

const uint8_t kMuxPortMask = _BV(PB0) | _BV(PB2) | _BV(PB3);
const uint8_t kSettingsAdcChannel = 0;
const uint8_t kStepButtonBit = PC1;
const uint8_t kStepAdcChannel = 2;
const uint8_t kModeButtonBit = PC3;
const uint8_t kLcdRs = _BV(PD2);
const uint8_t kLcdEnable = _BV(PD3);

const uint8_t kStepCount = 8;
const uint8_t kSettingCount = 4;
const uint8_t kStepPotsPerLoop = 2;
const uint8_t kDroneWaveCount = 4;
const uint8_t kDrumCount = DRUM_SAMPLE_COUNT;
const uint8_t kScaleCount = 4;
const uint8_t kPlayStyleCount = 8;
const uint8_t kAnalogSamples = 5;
const uint8_t kAnalogDiscardSamples = 2;
const uint8_t kAnalogFilterFractionBits = 4;
const uint8_t kAnalogFilterAlphaNumerator = 2;
const uint8_t kAnalogFilterAlphaDenominator = 8;
const uint8_t kAnalogHysteresis = 12;
const uint8_t kAnalogEndpointMargin = 16;
const uint8_t kSettingMovementThreshold = 16;
const uint8_t kStepMovementThreshold = 10;
const uint8_t kDroneMovementThreshold = 8;
const uint8_t kAnalogMovementConfirmations = 2;
const uint8_t kMuxSettleUs = 50;
const uint8_t kButtonDebounceMs = 25;
const uint16_t kModeDoubleTapMs = 350;
const uint16_t kModeIgnoreHoldMs = 650;
const uint16_t kEditViewMs = 1500;
const uint8_t kBpmMin = 60;
const uint8_t kBpmMax = 190;
const uint8_t kSwingPercent = 65;
const uint8_t kStepNoteMin = 36;
const uint8_t kStepNoteMax = 59;
const uint16_t kDroneFrequencyMin = 80;
const uint16_t kDroneFrequencyMax = 600;
const uint16_t kLfoFrequencyMinHundredths = 5;
const uint16_t kLfoFrequencyMaxHundredths = 2000;
const uint8_t kLfoRateCount = 94;
const uint8_t kPresetVcaModulationCount = 99;
const uint8_t kFilterOpen = 127;

enum InstrumentMode : uint8_t { MODE_SEQ = 0, MODE_PLAY = 1, MODE_DRONE = 2 };
enum ScaleMode : uint8_t { SCALE_MAJOR = 0, SCALE_MINOR = 1, SCALE_PENTATONIC = 2, SCALE_BLUES = 3 };
enum PlayStyle : uint8_t { STYLE_POLY = 0, STYLE_CHORD = 1, STYLE_ARP_UP = 2, STYLE_ARP_DOWN = 3, STYLE_ARP_UPDOWN = 4,
                           STYLE_CHORD_ARP_UP = 5, STYLE_CHORD_ARP_DOWN = 6, STYLE_CHORD_ARP_UPDOWN = 7 };
enum DrumId : uint8_t { DRUM_KICK = 0, DRUM_SNARE, DRUM_CLOSED_HAT };
enum MacroParam : uint8_t {
  MACRO_NONE = 0, MACRO_PULSE_WIDTH, MACRO_VIBRATO,
  MACRO_DRUM_TUNE, MACRO_DRUM_DECAY, MACRO_VCA_DEPTH, MACRO_VCA_RATE,
  MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH, MACRO_GLIDE, MACRO_CHORUS,
  MACRO_MORPH, MACRO_FILTER, MACRO_RESONANCE, MACRO_FILTER_ENVELOPE
};

const uint8_t kMacroSlotCount = 8;
const uint8_t kMacroLfoCount = 2;

enum ParameterControl : uint8_t {
  PARAM_NONE = 0,
  PARAM_SEQ_PRESET, PARAM_SEQ_BPM, PARAM_SEQ_TUNE,
  PARAM_PLAY_PRESET, PARAM_PLAY_ROOT, PARAM_PLAY_SCALE, PARAM_PLAY_STYLE,
  PARAM_MACRO_0,
  PARAM_LFO_0_RATE = PARAM_MACRO_0 + kMacroSlotCount, PARAM_LFO_0_DEPTH, PARAM_LFO_1_RATE, PARAM_LFO_1_DEPTH
};
static_assert((PARAM_LFO_0_RATE & 1) == 0, "LFO rate controls must be even");

struct VoiceRecipe {
  uint8_t wave;
  int16_t detuneCents;
  uint8_t envelope;
  uint8_t modulation;
};

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
  uint8_t filterCutoff;
  uint8_t filterResonance;
  uint8_t filterEnvelopeAmount;
  uint8_t macros[kMacroSlotCount];
  uint8_t lfoTargets[kMacroLfoCount];
  uint8_t lfoRates[kMacroLfoCount];
  uint8_t lfoDepths[kMacroLfoCount];
  uint8_t level;
};

struct DrumPreset {
  char label[5];
  uint8_t decay;
  uint8_t macros[kMacroSlotCount];
  uint8_t lfoTargets[kMacroLfoCount];
  uint8_t level;
};
static_assert(offsetof(MelodicPreset, lfoTargets) == offsetof(MelodicPreset, macros) + kMacroSlotCount,
              "melodic macro and LFO slots must be contiguous");
static_assert(offsetof(DrumPreset, lfoTargets) == offsetof(DrumPreset, macros) + kMacroSlotCount,
              "drum macro and LFO slots must be contiguous");

struct SeqControls { uint8_t preset; uint8_t bpm; uint8_t drumTone; };
struct PlayControls { uint8_t preset; uint8_t root; uint8_t scale; uint8_t style; uint8_t octave; uint8_t arpDivision; };

struct AnalogMotionState {
  uint16_t accepted;
  uint8_t confirmations;
  int8_t direction;
  bool ready;
};

// Melodic presets. Template (one MelodicPreset per entry):
//
//   {"LBL", voiceCount,
//    {{wave, detuneCents, envelope, mod}, {voice 1}, {voice 2}, {voice 3}},
//    len, pw, glide, vcaDepth, vcaRate, chorus, morph, cutoff, res, feg,
//    {macro slot 0, slot 1, slot 2, slot 3, slot 4, slot 5, slot 6, slot 7},
//    {lfo0Target, lfo1Target},
//    {lfo0Rate, lfo1Rate}, {lfo0Depth, lfo1Depth}, level},
//
// label       LCD name, max 4 characters.
// voiceCount  1: one voice per note from voices[0] (POLY/CHORD/ARP use several notes).
//             2-4: every note stacks voices[0..voiceCount-1]; unused recipes are ignored.
// voices[]    wave: SINE/TRIANGLE/SQUARE/SAW/NOISE.
//             detuneCents: pitch offset from the note (1200 = +1 octave, 702 = fifth).
//             envelope: ENVELOPE1-5 shape; voices[0] is the ENV macro's start value.
//             mod: vibrato, 64 = off, distance from 64 = depth (0-127); voices[0] seeds VIB.
// len         Envelope length 0-127; +12 doubles the time; a zero-step length means HOLD.
// pw          SQUARE pulse width, 5-95 %.
// glide       Portamento 0-127, 0 = off.
// vcaDepth    Tremolo depth 0-256, 0 = off.
// vcaRate     Tremolo rate index 0-98, 0 = off (presetVcaRateHundredths: 0.01-20 Hz).
// chorus      SYNTH_CHORUS_OFF, SYNTH_CHORUS_1 (slow), SYNTH_CHORUS_2 (fast).
// morph       Wave morph speed voices[0].wave -> voices[1].wave, 0-127, 0 = off.
// cutoff      Low-pass cutoff 0-127, 127 = open (filter bypassed).
// res         Resonance 0-127.
// feg         Filter envelope amount 0-127, 0 = off.
// macros[8]   MACRO_* on the eight macro slots in order; MACRO_NONE leaves a slot empty.
// lfoTarget   MACRO_* modulated by LFO 0/1 with a triangle; MACRO_NONE = unused.
// lfoRate     Index 0-93 (lfoRateForIndex: 0.05-20 Hz); the LCD shows index + 1.
// lfoDepth    0-127, 0 = LFO idle.
// level       Output level scale 0-255, 255 = full voice budget.
const MelodicPreset kMelodicPresets[] PROGMEM = {
  {"A01", 3,
   {{SAW, 0, ENVELOPE5, 64}, {SAW, 8, ENVELOPE5, 64}, {SQUARE, -1200, ENVELOPE5, 64}, {SINE, 0, ENVELOPE5, 64}},
   72, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, 52, 24, 88,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_FILTER_ENVELOPE, MACRO_NONE, MACRO_NONE},
   {MACRO_NONE, MACRO_NONE},
   {0, 0}, {0, 0}, 255},
  {"A02", 3,
   {{SAW, -14, ENVELOPE4, 64}, {SAW, 14, ENVELOPE4, 64}, {SINE, -1200, ENVELOPE4, 64}, {SINE, 0, ENVELOPE4, 64}},
   106, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, 60, 16, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_NONE, MACRO_NONE, MACRO_NONE},
   {MACRO_FILTER, MACRO_NONE},
   {16, 0}, {12, 0}, 255},
  {"A03", 1,
   {{SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}},
   90, 50, 72, 0, 0, SYNTH_CHORUS_OFF, 0, 127, 0, 0,
   {MACRO_GLIDE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE},
   {MACRO_NONE, MACRO_NONE},
   {0, 0}, {0, 0}, 255},
  {"B01", 2,
   {{SQUARE, 1200, ENVELOPE5, 64}, {SAW, 1205, ENVELOPE5, 64}, {SINE, 0, ENVELOPE5, 64}, {SINE, 0, ENVELOPE5, 64}},
   87, 30, 0, 0, 0, SYNTH_CHORUS_OFF, 0, 88, 16, 80,
   {MACRO_PULSE_WIDTH, MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_VIBRATO, MACRO_FILTER_ENVELOPE},
   {MACRO_NONE, MACRO_NONE},
   {0, 0}, {0, 0}, 255},
  {"B02", 4,
   {{SAW, 1200, ENVELOPE4, 64}, {SAW, 1185, ENVELOPE4, 64}, {SAW, 1215, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}},
   106, 50, 12, 0, 74, SYNTH_CHORUS_1, 0, 101, 0, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_GLIDE, MACRO_CHORUS, MACRO_VIBRATO, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_VCA_DEPTH},
   {MACRO_FILTER, MACRO_NONE},
   {5, 0}, {0, 0}, 255},
  {"C01", 1,
   {{SQUARE, 0, ENVELOPE2, 64}, {SQUARE, 0, ENVELOPE2, 64}, {SQUARE, 0, ENVELOPE2, 64}, {SQUARE, 0, ENVELOPE2, 64}},
   99, 50, 0, 0, 0, SYNTH_CHORUS_1, 0, 84, 16, 0,
   {MACRO_PULSE_WIDTH, MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_NONE, MACRO_NONE},
   {MACRO_PULSE_WIDTH, MACRO_FILTER},
   {18, 5}, {49, 0}, 255},
  {"C02", 1,
   {{SAW, 0, ENVELOPE2, 66}, {SAW, 0, ENVELOPE2, 66}, {SAW, 0, ENVELOPE2, 66}, {SAW, 0, ENVELOPE2, 66}},
   99, 50, 0, 0, 0, SYNTH_CHORUS_2, 0, 80, 0, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_VIBRATO, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_NONE, MACRO_NONE},
   {MACRO_FILTER, MACRO_NONE},
   {7, 0}, {16, 0}, 255},
  {"D01", 1,
   {{SAW, 0, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}},
   106, 50, 0, 0, 0, SYNTH_CHORUS_1, 0, 88, 0, 64,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_VIBRATO, MACRO_FILTER_ENVELOPE},
   {MACRO_NONE, MACRO_NONE},
   {0, 0}, {0, 0}, 255},
  {"D02", 1,
   {{TRIANGLE, 0, ENVELOPE5, 64}, {TRIANGLE, 0, ENVELOPE5, 64}, {TRIANGLE, 0, ENVELOPE5, 64}, {TRIANGLE, 0, ENVELOPE5, 64}},
   99, 50, 0, 96, 69, SYNTH_CHORUS_OFF, 0, 90, 0, 0,
   {MACRO_VCA_DEPTH, MACRO_VCA_RATE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_NONE},
   {MACRO_NONE, MACRO_NONE},
   {0, 0}, {0, 0}, 255},
  {"D03", 4,
   {{SINE, 0, ENVELOPE3, 66}, {SINE, 1200, ENVELOPE3, 66}, {SINE, 702, ENVELOPE3, 66}, {SINE, -1200, ENVELOPE3, 66}},
   127, 50, 0, 40, 70, SYNTH_CHORUS_2, 0, 127, 0, 0,
   {MACRO_VCA_RATE, MACRO_VCA_DEPTH, MACRO_VIBRATO, MACRO_CHORUS, MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE},
   {MACRO_NONE, MACRO_NONE},
   {0, 0}, {0, 0}, 255},
  {"E01", 2,
   {{SAW, 0, ENVELOPE1, 64}, {SQUARE, 6, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}},
   66, 40, 0, 0, 0, SYNTH_CHORUS_OFF, 1, 72, 48, 64,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_MORPH, MACRO_PULSE_WIDTH, MACRO_CHORUS, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE},
   {MACRO_FILTER, MACRO_PULSE_WIDTH},
   {5, 20}, {40, 28}, 255},
  {"E02", 1,
   {{NOISE, 0, ENVELOPE3, 64}, {NOISE, 0, ENVELOPE3, 64}, {NOISE, 0, ENVELOPE3, 64}, {NOISE, 0, ENVELOPE3, 64}},
   127, 50, 0, 200, 10, SYNTH_CHORUS_OFF, 0, 68, 48, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_VCA_DEPTH, MACRO_VCA_RATE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_CHORUS, MACRO_NONE},
   {MACRO_FILTER, MACRO_NONE},
   {7, 0}, {45, 0}, 255}
};
const uint8_t kMelodicPresetCount = sizeof(kMelodicPresets) / sizeof(kMelodicPresets[0]);
const uint8_t PRESET_DRUM = kMelodicPresetCount;
const uint8_t kPresetCount = kMelodicPresetCount + 1;
const uint8_t kPlayPresetCount = kMelodicPresetCount;

const DrumPreset kDrumPreset PROGMEM =
  {"DRM", 127, {MACRO_DRUM_DECAY, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE}, {MACRO_NONE, MACRO_NONE}, 255};

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
const uint8_t kArpDivisionCount = 8;
const uint8_t kArpDivisionTicks[kArpDivisionCount] PROGMEM = {48, 24, 16, 12, 8, 6, 4, 3};
const char kArpDivisionLabels[kArpDivisionCount][6] PROGMEM = {"1/2", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32"};
const uint8_t kPlayOctaveCount = 5;
synth edgar;
InstrumentMode instrumentMode = MODE_SEQ;
SeqControls seqControls = {0, 126, 64};
PlayControls playControls = {0, 0, SCALE_MAJOR, STYLE_POLY, 1, 5};
uint8_t stepNotes[kStepCount] = {36, 38, 40, 41, 43, 45, 47, 48};
uint8_t stepDrums[kStepCount] = {DRUM_KICK, DRUM_CLOSED_HAT, DRUM_SNARE, DRUM_CLOSED_HAT,
                                 DRUM_KICK, DRUM_CLOSED_HAT, DRUM_SNARE, DRUM_CLOSED_HAT};
uint8_t seqGateMask = 0xFF;
uint8_t droneGateMask = 0;
uint8_t dronePairWave[kSettingCount] = {SINE, TRIANGLE, SQUARE, SAW};
uint16_t droneLfoRateHundredths[kSettingCount] = {0, 0, 0, 0};
uint16_t dronePitchWord[kStepCount] = {0};
uint8_t playGlide = 0;
struct MacroState {
  uint16_t values[kMacroSlotCount];
  uint8_t lfoRateIndex[kMacroLfoCount];
  uint8_t lfoDepth[kMacroLfoCount];
  uint32_t lfoPhase[kMacroLfoCount];
};
MacroState macroStates[2];
uint8_t macroMode = MODE_SEQ;
MacroState *macroState = &macroStates[MODE_SEQ];
unsigned long lastMacroLfoMs = 0;

uint8_t currentStep = 0;
uint8_t playingStep = 255;
unsigned long lastStepMs = 0;
uint16_t nextStepIntervalMs = 119;
const uint16_t kVoiceLevelBudget = SYNTH_LEVEL_BUDGET * 2;
uint8_t backgroundVoiceLevel = 79;
uint8_t sequenceVoiceLevel = 79;
uint8_t liveVoiceLevel = 79;
uint16_t normalCurrentPitch[SYNTH_VOICE_COUNT] = {0};
uint16_t normalTargetPitch[SYNTH_VOICE_COUNT] = {0};
uint8_t normalGlide[SYNTH_VOICE_COUNT] = {0};
unsigned long lastGlideMs = 0;

uint8_t heldButtonMask = 0;
uint16_t buttonPressOrder[kStepCount] = {0};
uint16_t pressOrderCounter = 0;
int8_t liveVoiceForButton[kStepCount] = {-1, -1, -1, -1, -1, -1, -1, -1};
int8_t liveButtonForVoice[SYNTH_VOICE_COUNT] = {-1, -1, -1, -1, -1};
uint16_t liveVoiceAge[SYNTH_VOICE_COUNT] = {0};
uint8_t arpPosition = 0;
int8_t arpDirection = 1;
uint8_t arpTick = 0;
bool playBpmPotLocked = true;
uint16_t playBpmPotRaw = 0;

uint8_t stepButtonStable[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
uint8_t stepButtonReading[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long stepButtonChangedAt[kStepCount] = {0};
uint8_t modeButtonStable = HIGH;
uint8_t modeButtonReading = HIGH;
unsigned long modeButtonChangedAt = 0;
unsigned long modeButtonPressedAt = 0;
bool modeButtonChordUsed = false;
uint8_t modeChordStep = 255;
bool modePageTapPending = false;
bool modeDoubleTapArmed = false;
unsigned long modePageTapAt = 0;
bool playSequencerEnabled = true;
bool swingEnabled = false;
bool generativeEnabled = false;
uint8_t genSource[kStepCount] = {0, 1, 2, 3, 4, 5, 6, 7};
uint8_t genRandom = 1;
const char kGenChar = '*';
const uint8_t kGlyphPlaceholder = 0x80;
const uint8_t kGlyphBar = 0x08;
const uint8_t kGlyphPlayhead = 0x10;
const uint8_t kGlyphPlay = 0x20;
const uint8_t kGlyphStop = 0x21;
const uint8_t kGlyphKeyEnd = 0x22;
const uint8_t kGlyphIcons[2][8] PROGMEM = {
  {0x08, 0x0C, 0x0E, 0x0F, 0x0E, 0x0C, 0x08, 0x00},
  {0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00},
};
uint8_t glyphKeys[8];
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
  memcpy_P(&preset, &kMelodicPresets[programId], sizeof(MelodicPreset));
}

void readDrumPreset(DrumPreset &preset) {
  memcpy_P(&preset, &kDrumPreset, sizeof(DrumPreset));
}

uint8_t playProgramId() { return playControls.preset; }

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

bool isMacroControl(uint8_t control) { return (uint8_t)(control - PARAM_MACRO_0) < kMacroSlotCount; }
bool isLfoControl(uint8_t control) { return (uint8_t)(control - PARAM_LFO_0_RATE) < kMacroLfoCount * 2; }

// Returns a direct macro (slots 0-7) or LFO target (slots 8-9) of the current program.
uint8_t macroForCurrentProgram(uint8_t slot) {
  const uint8_t programId = macroProgramId();
  const uint8_t *slots = isDrumProgram(programId) ? kDrumPreset.macros :
    kMelodicPresets[programId].macros;
  return pgm_read_byte(slots + slot);
}

// Live PLAY voices end at voice 3 while the background sequence runs, otherwise at 4.
uint8_t liveVoiceEnd() { return playSequencerEnabled ? SYNTH_BACKGROUND_VOICE : SYNTH_NORMAL_VOICE_COUNT; }

// End of the voices that macro edits and macro LFOs change.
uint8_t macroVoiceEnd() { return macroMode == MODE_PLAY ? liveVoiceEnd() : SYNTH_VOICE_COUNT; }

// Counts the current mode's controls; unused macro and LFO slots are skipped.
uint8_t parameterControlCount() {
  uint8_t count = instrumentMode == MODE_SEQ ? 2 : 4;
  if (instrumentMode == MODE_SEQ && isDrumProgram(seqControls.preset)) ++count;
  for (uint8_t slot = 0; slot < kMacroSlotCount; ++slot) if (macroForCurrentProgram(slot) != MACRO_NONE) ++count;
  for (uint8_t lfo = 0; lfo < kMacroLfoCount; ++lfo) if (macroForCurrentProgram(kMacroSlotCount + lfo) != MACRO_NONE) count += 2;
  return count;
}

// Returns the control at a position of the packed parameter pages.
uint8_t parameterControlAt(uint8_t index) {
  const bool sequence = instrumentMode == MODE_SEQ;
  const uint8_t fixedCount = sequence ? (isDrumProgram(seqControls.preset) ? 3 : 2) : 4;
  if (index < fixedCount) {
    if (sequence) return index == 0 ? PARAM_SEQ_PRESET : (index == 1 ? PARAM_SEQ_BPM : PARAM_SEQ_TUNE);
    return PARAM_PLAY_PRESET + index;
  }
  index -= fixedCount;
  for (uint8_t slot = 0; slot < kMacroSlotCount; ++slot) {
    if (macroForCurrentProgram(slot) == MACRO_NONE) continue;
    if (index-- == 0) return PARAM_MACRO_0 + slot;
  }
  for (uint8_t lfo = 0; lfo < kMacroLfoCount; ++lfo) {
    if (macroForCurrentProgram(kMacroSlotCount + lfo) == MACRO_NONE) continue;
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

// Converts a 5-95% duty cycle to a 16-bit pulse width.
uint16_t pulseWidthForPercent(uint8_t percent) {
  return (uint16_t)constrain(percent, 5, 95) * 655U;
}

// Converts Hz to a 20 kHz phase step.
uint16_t pitchWordForFrequency(uint16_t frequency) {
  return ((uint32_t)frequency * 65535UL + FS / 2) / FS;
}

uint8_t mapIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count);
uint8_t mapByteHysteresis(uint16_t raw, uint8_t current, int16_t outMin, int16_t outMax);

// Maps LFO rate index 0-93 to 0.05-20 Hz, in hundredths.
uint16_t lfoRateForIndex(uint8_t index) {
  if (index < 16) return kLfoFrequencyMinHundredths + index;
  if (index < 64) return 20 + (uint16_t)(index - 15) * 10;
  return 500 + (uint16_t)(index - 63) * 50;
}

// Maps a preset VCA rate index to 0.01-20 Hz, in hundredths; 0 is off.
uint16_t presetVcaRateHundredths(uint8_t modulation) {
  if (modulation == 0) return 0;
  if (modulation <= 20) return modulation;
  if (modulation <= 68) return 20 + (uint16_t)(modulation - 20) * 10;
  return 500 + (uint16_t)(min(modulation, (uint8_t)(kPresetVcaModulationCount - 1)) - 68) * 50;
}

// Inverse of lfoRateForIndex().
uint8_t lfoRateIndexForHundredths(uint16_t rate) {
  if (rate <= 20) return rate - kLfoFrequencyMinHundredths;
  if (rate <= 500) return 15 + (rate - 20) / 10;
  return 63 + (rate - 500) / 50;
}

// Maps a reversed pot to an LFO rate; the low endpoint is off.
uint16_t lfoRateForRaw(uint16_t raw, uint16_t current) {
  if (raw <= kAnalogEndpointMargin) return kLfoFrequencyMaxHundredths;
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;
  const uint8_t index = current == 0 ? 0 : lfoRateIndexForHundredths(current);
  return lfoRateForIndex(mapIndexHysteresis(raw, index, kLfoRateCount));
}

// Returns the slot holding a macro, or kMacroSlotCount if there is none.
uint8_t macroSlot(uint8_t parameter) {
  uint8_t slot = 0;
  while (slot < kMacroSlotCount && macroForCurrentProgram(slot) != parameter) ++slot;
  return slot;
}

// Returns a macro's current value, or fallback if the program lacks it.
uint16_t activeMacroValue(uint8_t parameter, uint16_t fallback) {
  const uint8_t slot = macroSlot(parameter);
  return slot < kMacroSlotCount ? macroState->values[slot] : fallback;
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
    case MACRO_MORPH:
    case MACRO_FILTER:
    case MACRO_RESONANCE: return 127;
    case MACRO_CHORUS: return SYNTH_CHORUS_2;
    default: return 0;
  }
}

// Returns a macro value swept by the LFOs that target it, clamped to its range.
uint16_t lfoModulatedMacroValue(uint8_t parameter, uint16_t fallback) {
  const uint16_t minimum = macroMinimum(parameter);
  const uint16_t maximum = macroMaximum(parameter);
  if (maximum <= minimum) return fallback;

  int32_t value = constrain(activeMacroValue(parameter, fallback), minimum, maximum);
  const int16_t span = maximum - minimum;
  for (uint8_t lfo = 0; lfo < kMacroLfoCount; ++lfo) {
    if (macroForCurrentProgram(kMacroSlotCount + lfo) != parameter || macroState->lfoDepth[lfo] == 0) continue;
    const uint8_t phase = macroState->lfoPhase[lfo] >> 24;
    const uint8_t triangle = (phase & 0x80) ? 256U - phase : phase;
    const int16_t centred = (int16_t)triangle * 2 - 127;
    value += ((int32_t)centred * macroState->lfoDepth[lfo] * span) / (127L * 127L);
  }
  return constrain(value, minimum, maximum);
}

// Maps a pot reading to a macro's value range.
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
    case MACRO_FILTER: return mapByteHysteresis(raw, activeMacroValue(parameter, kFilterOpen), 127, 0);
    case MACRO_RESONANCE:
    case MACRO_FILTER_ENVELOPE: return mapByteHysteresis(raw, activeMacroValue(parameter, 0), 127, 0);
    default: return 0;
  }
}

const uint8_t kEnvelopeLengthRateAnchors[] PROGMEM = {33, 22, 15, 10, 7, 5, 3, 2, 1};

const uint8_t kEnvelopeHoldControl = 127;

// Interpolates a descending nine-anchor flash table at index 0-127.
uint8_t descendingAnchorValue(const uint8_t *anchors, uint8_t index) {
  const uint8_t segment = index >> 4;
  const uint8_t fraction = index & 0x0F;
  const uint8_t start = pgm_read_byte(&anchors[segment]);
  const uint8_t end = pgm_read_byte(&anchors[segment + 1]);
  return start - (((uint16_t)(start - end) * fraction + 8) >> 4);
}

// Maps LEN 0-126 to an envelope step (about 0.2-6.5 s); 127 is HOLD.
uint8_t envelopeRateForControl(uint8_t control) {
  if (control >= kEnvelopeHoldControl) return 0;
  return descendingAnchorValue(kEnvelopeLengthRateAnchors, control);
}

const uint8_t kFilterCoefficientAnchors[] PROGMEM = {255, 169, 110, 72, 46, 30, 19, 12, 8};
const uint8_t kFilterDampingAnchors[] PROGMEM = {128, 102, 82, 65, 52, 42, 33, 27, 21};

// Sends the program's cutoff, resonance, and filter envelope amount to the engine.
void applyCurrentProgramFilter() {
  const uint8_t programId = macroProgramId();
  uint8_t cutoff = kFilterOpen;
  uint8_t resonance = 0;
  uint8_t envelopeAmount = 0;
  if (!isDrumProgram(programId)) {
    MelodicPreset preset; readMelodicPreset(programId, preset);
    cutoff = lfoModulatedMacroValue(MACRO_FILTER, preset.filterCutoff);
    resonance = lfoModulatedMacroValue(MACRO_RESONANCE, preset.filterResonance);
    envelopeAmount = activeMacroValue(MACRO_FILTER_ENVELOPE, preset.filterEnvelopeAmount);
  }
  edgar.setBusFilter(cutoff >= kFilterOpen ? 0 :
    descendingAnchorValue(kFilterCoefficientAnchors, kFilterOpen - cutoff),
    descendingAnchorValue(kFilterDampingAnchors, resonance),
    (uint8_t)((uint16_t)(envelopeAmount * envelopeAmount) >> 6));
}

// Finds the length index whose envelope step is closest to rate.
uint8_t envelopeLengthIndexForRate(uint8_t rate) {
  uint8_t bestIndex = 0;
  uint16_t bestDifference = 0xFFFF;
  for (uint8_t index = 0; index < 118; ++index) {
    const uint16_t speed = envelopeStepForLength(index);
    const uint16_t difference = speed > rate ? speed - rate : rate - speed;
    if (difference < bestDifference) { bestDifference = difference; bestIndex = index; }
  }
  return bestIndex;
}

uint8_t envelopeLengthIndexForControl(uint8_t control) {
  return control >= kEnvelopeHoldControl ? 127 : envelopeLengthIndexForRate(envelopeRateForControl(control));
}

// Finds the LEN value closest to a stored preset length.
uint8_t envelopeLengthControlForPreset(uint8_t length) {
  const uint16_t rate = envelopeStepForLength(length);
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

// Resolves a preset's envelope and length from ENV, LEN, and HOLD.
void presetEnvelope(const MelodicPreset &preset, uint8_t &envelope, uint8_t &length) {
  const uint8_t slot = macroSlot(MACRO_ENVELOPE_LENGTH);
  bool hold;
  if (slot < kMacroSlotCount) {
    const uint8_t control = macroState->values[slot];
    length = envelopeLengthIndexForControl(control);
    hold = control >= kEnvelopeHoldControl;
  } else {
    length = preset.length;
    hold = envelopeStepForLength(length) == 0;
  }
  envelope = hold ? ENVELOPE3 : activeMacroValue(MACRO_ENVELOPE, preset.voices[0].envelope);
}

// Applies the current ENV and LEN to the macro-controlled voices.
void applyMacroEnvelopeToVoices() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) return;
  MelodicPreset preset; readMelodicPreset(programId, preset);
  uint8_t envelope, length;
  presetEnvelope(preset, envelope, length);
  for (uint8_t voice = 0; voice < macroVoiceEnd(); ++voice) {
    edgar.setEnvelope(voice, envelope); edgar.setLength(voice, length);
  }
}

// Shows a parameter's value on the LCD for a moment.
void showParameterEdit(uint8_t slot, unsigned long now) {
  editedPlayShift = slot + 1;
  editedStep = 255;
  inlineEditUntilMs = now + kEditViewMs;
  displayDirty = true;
}

// Sends the program's VCA depth and rate, including macros and LFOs.
void applyCurrentProgramVca() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) { edgar.disablePlayVca(); return; }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  edgar.configurePlayVca(lfoModulatedMacroValue(MACRO_VCA_DEPTH, preset.vcaDepth),
    presetVcaRateHundredths((uint8_t)lfoModulatedMacroValue(MACRO_VCA_RATE, preset.vcaModulation)));
}

// Applies the program's filter, VCA, and chorus.
void applyCurrentProgramBusEffects() {
  applyCurrentProgramFilter();
  applyCurrentProgramVca();
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) { edgar.configureChorus(SYNTH_CHORUS_OFF); return; }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  edgar.configureChorus(activeMacroValue(MACRO_CHORUS, preset.chorusMode));
}

// Configures the waveform morph between the first two voice recipes.
void applyCurrentProgramMorph() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) { edgar.disablePresetMorph(); return; }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  const uint8_t speed = activeMacroValue(MACRO_MORPH, preset.morphSpeed);
  edgar.configurePresetMorph(preset.voices[0].wave, preset.voices[1].wave,
    (uint16_t)speed << 4, preset.voiceCount > 1);
}

// Applies a pot to a direct macro and updates the affected engine state.
void applyMacroValue(uint8_t slot, uint8_t displaySlot, uint16_t raw, unsigned long now) {
  const uint8_t parameter = macroForCurrentProgram(slot);
  if (parameter == MACRO_NONE) return;
  const uint16_t next = macroValueForRaw(parameter, raw);
  if (next == macroState->values[slot]) return;
  macroState->values[slot] = next;
  if (parameter == MACRO_PULSE_WIDTH) {
    const uint16_t width = pulseWidthForPercent(next);
    for (uint8_t voice = 0; voice < macroVoiceEnd(); ++voice) edgar.setPulseWidth(voice, width);
  } else if (parameter == MACRO_DRUM_TUNE) {
    seqControls.drumTone = (uint8_t)next;
  } else if (parameter == MACRO_ENVELOPE || parameter == MACRO_ENVELOPE_LENGTH) {
    applyMacroEnvelopeToVoices();
  } else if (parameter == MACRO_GLIDE) {
    playGlide = next;
    for (uint8_t voice = 0; voice < macroVoiceEnd(); ++voice) {
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
  } else if (parameter >= MACRO_FILTER) {
    applyCurrentProgramFilter();
  } else if (parameter == MACRO_VCA_DEPTH || parameter == MACRO_VCA_RATE) {
    applyCurrentProgramVca();
  }
  showParameterEdit(displaySlot, now);
}

// Applies a pot to an LFO rate (even controls) or depth (odd controls).
void applyLfoControl(uint8_t control, uint8_t displaySlot, uint16_t raw, unsigned long now) {
  const uint8_t lfo = control >> 1;
  const bool depth = (control & 1) != 0;
  if (macroForCurrentProgram(kMacroSlotCount + lfo) == MACRO_NONE) return;
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

// Soft takeover: a pot acts only after it moves away from its page-change position.
bool parameterPotUnlocked(uint8_t pot, uint16_t raw) {
  if (!parameterPageLocked[pot]) return true;
  if (abs((int16_t)raw - (int16_t)parameterPageRaw[pot]) < kSettingMovementThreshold) return false;
  parameterPageLocked[pot] = false;
  return true;
}

// Loads a program's default macro and LFO values.
void loadProgramMacroDefaults(uint8_t programId) {
  for (uint8_t slot = 0; slot < kMacroSlotCount; ++slot) macroState->values[slot] = 64;
  macroState->lfoDepth[0] = macroState->lfoDepth[1] = 0;
  if (isDrumProgram(programId)) {
    DrumPreset preset; readDrumPreset(preset);
    for (uint8_t slot = 0; slot < kMacroSlotCount; ++slot) {
      if (preset.macros[slot] == MACRO_DRUM_TUNE) macroState->values[slot] = seqControls.drumTone;
      if (preset.macros[slot] == MACRO_DRUM_DECAY) macroState->values[slot] = preset.decay;
    }
  } else {
    MelodicPreset preset; readMelodicPreset(programId, preset);
    memcpy(macroState->lfoRateIndex, preset.lfoRates, kMacroLfoCount);
    memcpy(macroState->lfoDepth, preset.lfoDepths, kMacroLfoCount);
    for (uint8_t slot = 0; slot < kMacroSlotCount; ++slot) {
      if (preset.macros[slot] == MACRO_PULSE_WIDTH) macroState->values[slot] = preset.squarePulseWidthPercent;
      if (preset.macros[slot] == MACRO_VIBRATO) macroState->values[slot] = preset.voices[0].modulation;
      if (preset.macros[slot] == MACRO_VCA_DEPTH) macroState->values[slot] = preset.vcaDepth;
      if (preset.macros[slot] == MACRO_VCA_RATE) macroState->values[slot] = preset.vcaModulation;
      if (preset.macros[slot] == MACRO_ENVELOPE) macroState->values[slot] = preset.voices[0].envelope;
      if (preset.macros[slot] == MACRO_ENVELOPE_LENGTH) macroState->values[slot] = envelopeLengthControlForPreset(preset.length);
      if (preset.macros[slot] == MACRO_GLIDE) macroState->values[slot] = preset.glide;
      if (preset.macros[slot] == MACRO_CHORUS) macroState->values[slot] = preset.chorusMode;
      if (preset.macros[slot] == MACRO_MORPH) macroState->values[slot] = preset.morphSpeed;
      if (preset.macros[slot] == MACRO_FILTER) macroState->values[slot] = preset.filterCutoff;
      if (preset.macros[slot] == MACRO_RESONANCE) macroState->values[slot] = preset.filterResonance;
      if (preset.macros[slot] == MACRO_FILTER_ENVELOPE) macroState->values[slot] = preset.filterEnvelopeAmount;
    }
  }
  applyCurrentProgramBusEffects();
  applyCurrentProgramMorph();
}

// Advances the macro LFOs at 250 Hz, outside the audio ISR.
void updateMacroLfos(unsigned long now) {
  if ((unsigned long)(now - lastMacroLfoMs) < 4) return;
  const uint8_t elapsed = min((unsigned long)(now - lastMacroLfoMs), 20UL);
  lastMacroLfoMs = now;
  for (uint8_t lfo = 0; lfo < kMacroLfoCount; ++lfo) {
    const uint8_t target = macroForCurrentProgram(kMacroSlotCount + lfo);
    if (target == MACRO_NONE || macroState->lfoDepth[lfo] == 0) continue;
    macroState->lfoPhase[lfo] += (uint32_t)lfoRateForIndex(macroState->lfoRateIndex[lfo]) * 42950UL * elapsed;
    if (target == MACRO_PULSE_WIDTH) {
      const uint16_t width = pulseWidthForPercent(lfoModulatedMacroValue(target, 50));
      for (uint8_t voice = 0; voice < macroVoiceEnd(); ++voice) edgar.setPulseWidth(voice, width);
    } else if (target == MACRO_VIBRATO) {
      const uint8_t modulation = lfoModulatedMacroValue(target, 64);
      for (uint8_t voice = 0; voice < macroVoiceEnd(); ++voice) edgar.setMod(voice, modulation);
    } else if (target == MACRO_VCA_DEPTH || target == MACRO_VCA_RATE) {
      applyCurrentProgramVca();
    } else if (target == MACRO_FILTER || target == MACRO_RESONANCE) {
      applyCurrentProgramFilter();
    }
  }
}

void setup() {
  DDRB |= kMuxPortMask;
  PORTC |= _BV(kStepButtonBit) | _BV(kModeButtonBit);

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

  lcdBegin();
  nextStepIntervalMs = stepIntervalMs(1);
  lastStepMs = millis();
}

// Runs the timing-critical tasks before and after the slower control scan.
void loop() {
  unsigned long now = millis();
  runSequencer(now);
  runArpeggio(now);
  updateGlide(now);
  updateMacroLfos(now);
  scanModeButton(now);
  scanStepButtons(now);
  readControls(now);
  now = millis();
  runSequencer(now);
  runArpeggio(now);
  updateGlide(now);
  updateMacroLfos(now);
  renderDisplay(now);
}

// Selects a mux channel (bits 0/1/2 on PB0/PB3/PB2) and waits for it to settle.
void selectMuxChannel(uint8_t channel) {
  PORTB = (PORTB & ~kMuxPortMask) | (channel & 0x05) | ((channel & 0x02) << 2);
  _delay_us(kMuxSettleUs);
}

uint16_t readAdc() {
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC)) {}
  return ADC;
}

// Discards settling reads and returns the median to reject spikes.
uint16_t readMedianAnalog(uint8_t channel) {
  uint16_t samples[kAnalogSamples];
  ADMUX = _BV(REFS0) | channel;
  for (uint8_t i = 0; i < kAnalogDiscardSamples; ++i) readAdc();
  for (uint8_t i = 0; i < kAnalogSamples; ++i) {
    uint16_t value = readAdc();
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

// Accepts sustained directional movement and rejects jitter.
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

// Returns the pot position of a mapped value.
uint16_t rawForMappedValue(int16_t value, int16_t outMin, int16_t outMax) {
  if (outMin == outMax) return 0;
  return constrain(map(value, outMin, outMax, 0, 1023), 0, 1023);
}

static_assert(kAnalogEndpointMargin == 16, "usableAnalogRaw scales the 991-count usable span");

// Stretches the span between the endpoint margins to 0-1022.
uint16_t usableAnalogRaw(uint16_t raw) {
  raw -= kAnalogEndpointMargin;
  return raw + (raw >> 5);
}

// Maps a pot to a range; the value changes only past a boundary plus hysteresis.
int16_t mapWithHysteresis(uint16_t raw, int16_t current, int16_t outMin, int16_t outMax) {
  const int16_t low = min(outMin, outMax);
  const int16_t high = max(outMin, outMax);
  if (raw <= kAnalogEndpointMargin) return outMin;
  if (raw >= 1023 - kAnalogEndpointMargin) return outMax;
  raw = usableAnalogRaw(raw);
  const int16_t candidate = constrain(map(raw, 0, 1023, outMin, outMax), low, high);
  if (candidate == current) return current;
  const int16_t next = current + (candidate > current ? 1 : -1);
  const uint16_t currentRaw = rawForMappedValue(current, outMin, outMax);
  const uint16_t nextRaw = rawForMappedValue(next, outMin, outMax);
  const uint16_t boundary = (currentRaw + nextRaw) / 2;
  const bool rising = nextRaw > currentRaw;
  if (rising ? raw <= boundary + kAnalogHysteresis : raw + kAnalogHysteresis >= boundary) return current;
  const int16_t settled = constrain(map(rising ? raw - kAnalogHysteresis : raw + kAnalogHysteresis,
                                        0, 1023, outMin, outMax), low, high);
  return candidate > current ? max(settled, next) : min(settled, next);
}

uint8_t mapByteHysteresis(uint16_t raw, uint8_t current, int16_t outMin, int16_t outMax) {
  return (uint8_t)mapWithHysteresis(raw, current, outMin, outMax);
}

// Maps a pot to a reversed index; the 32-bit product avoids overflow.
uint8_t reversedIndexForRaw(uint16_t raw, uint8_t count) {
  return count - 1 - min((uint8_t)(((uint32_t)raw * count) >> 10), (uint8_t)(count - 1));
}

// Maps a reversed pot to a stable discrete index.
uint8_t mapIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count) {
  if (count <= 1) return 0;
  if (raw <= kAnalogEndpointMargin) return count - 1;
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;
  raw = usableAnalogRaw(raw);

  const uint8_t candidate = reversedIndexForRaw(raw, count);
  if (current >= count || candidate == current) return candidate;

  const bool lower = candidate < current;
  const uint16_t boundary = ((uint32_t)(count - current - !lower) << 10) / count;
  if (lower) {
    if (raw <= boundary + kAnalogHysteresis) return current;
    return min(reversedIndexForRaw(raw - kAnalogHysteresis, count), (uint8_t)(current - 1));
  }
  if (raw + kAnalogHysteresis >= boundary) return current;
  return max(reversedIndexForRaw(raw + kAnalogHysteresis, count), (uint8_t)(current + 1));
}

// Rebuilds the live voices after a PLAY setting changes.
void revoiceHeldButtons();
void stopNormalVoice(uint8_t voice, bool release = false);

// Arms soft takeover for the four parameter pots.
void lockParameterPageControls() {
  for (uint8_t pot = 0; pot < kSettingCount; ++pot) {
    parameterPageRaw[pot] = settingValues[pot];
    parameterPageLocked[pot] = true;
  }
}

// Applies a parameter pot to its control on the current page.
void applyParameterControl(uint8_t control, uint8_t pot, uint16_t raw, unsigned long now) {
  if (isMacroControl(control)) {
    applyMacroValue(control - PARAM_MACRO_0, pot, raw, now);
    return;
  }
  if (isLfoControl(control)) {
    applyLfoControl(control - PARAM_LFO_0_RATE, pot, raw, now);
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
      next = mapByteHysteresis(raw, seqControls.bpm, kBpmMax, kBpmMin);
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

// Scans one parameter pot and two step pots per loop to limit latency.
void readControls(unsigned long now) {
  const uint8_t setting = settingScanChannel;
  selectMuxChannel(setting);
  const uint16_t settingFiltered = filterAnalog(
    readMedianAnalog(kSettingsAdcChannel), filteredSettingRaw[setting], settingFilterReady[setting]);
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

// DRONE parameter pots set pair waveforms, or LFO rates while MODE is held.
void readDroneControls(const uint16_t values[], unsigned long now) {
  if (modeButtonStable == LOW) {
    for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
      if (!parameterPotUnlocked(pair, values[pair])) continue;
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
    if (!parameterPotUnlocked(pair, values[pair])) continue;
    const uint8_t next = mapIndexHysteresis(values[pair], dronePairWave[pair], kDroneWaveCount);
    if (next != dronePairWave[pair]) {
      dronePairWave[pair] = next; edgar.setDroneWave(pair * 2, next); edgar.setDroneWave(pair * 2 + 1, next);
      displayDirty = true;
    }
  }
}

// Reads a SEQ step pot as a note, or a drum in DRM.
void readSequencerStepPot(uint8_t step, unsigned long now) {
  selectMuxChannel(step);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAdcChannel), filteredStepRaw[step], stepFilterReady[step]);
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
    else { stepNotes[step] = next; genSource[step] = step; }
    editedStep = step;
    editedPlayShift = 0;
    editedDrone = 255;
    inlineEditUntilMs = now + kEditViewMs;
    displayDirty = true;
  }
}

// Reads PLAY step pots 1-3 and 8: octave, tempo, arpeggio division, and background balance.
void readPlayStepPot(uint8_t step, unsigned long now) {
  selectMuxChannel(step);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAdcChannel), filteredStepRaw[step], stepFilterReady[step]);
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[step], kStepMovementThreshold);
  if (step == 0) {
    const uint8_t next = mapIndexHysteresis(raw, playControls.octave, kPlayOctaveCount);
    if (next != playControls.octave) { playControls.octave = next; displayDirty = true; }
  } else if (step == 1) {
    if (playBpmPotLocked) {
      if (abs((int16_t)raw - (int16_t)playBpmPotRaw) < kStepMovementThreshold) return;
      playBpmPotLocked = false;
    }
    const uint8_t next = mapByteHysteresis(raw, seqControls.bpm, kBpmMax, kBpmMin);
    if (next != seqControls.bpm) { seqControls.bpm = next; displayDirty = true; }
  } else if (step == 2) {
    const uint8_t next = mapIndexHysteresis(raw, playControls.arpDivision, kArpDivisionCount);
    if (next != playControls.arpDivision) { playControls.arpDivision = next; displayDirty = true; }
  } else if (step == 7) {
    const uint8_t next = mapByteHysteresis(raw, backgroundVoiceLevel, 255, 0);
    if (next == backgroundVoiceLevel) return;
    backgroundVoiceLevel = next;
    if (playSequencerEnabled) applyLiveConfiguration();
  }
}

// Reads a DRONE step pot as oscillator frequency.
void readDronePitchPot(uint8_t voice, unsigned long now) {
  const uint16_t lowPitch = pitchWordForFrequency(kDroneFrequencyMin);
  const uint16_t highPitch = pitchWordForFrequency(kDroneFrequencyMax);
  selectMuxChannel(voice);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAdcChannel), filteredStepRaw[voice], stepFilterReady[voice]);
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

// Debounces MODE: one tap pages, two quick taps change mode, holding is a modifier.
void scanModeButton(unsigned long now) {
  const uint8_t reading = (PINC >> kModeButtonBit) & 1;
  if (reading != modeButtonReading) { modeButtonReading = reading; modeButtonChangedAt = now; }
  if ((unsigned long)(now - modeButtonChangedAt) >= kButtonDebounceMs && reading != modeButtonStable) {
    modeButtonStable = reading;
    if (instrumentMode == MODE_DRONE) lockParameterPageControls();
    if (reading == LOW) {
      modeButtonPressedAt = now;
      modeButtonChordUsed = modeChordStep != 255;
      modeDoubleTapArmed = modePageTapPending &&
        (unsigned long)(now - modePageTapAt) <= kModeDoubleTapMs;
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
      modePageTapPending = false;
    }
  }
  if (modeButtonStable == HIGH && modePageTapPending &&
      (unsigned long)(now - modePageTapAt) > kModeDoubleTapMs) {
    modePageTapPending = false;
    if (instrumentMode == MODE_DRONE) {
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

// Debounces the eight step buttons.
void scanStepButtons(unsigned long now) {
  for (uint8_t button = 0; button < kStepCount; ++button) {
    selectMuxChannel(button);
    const uint8_t reading = (PINC >> kStepButtonBit) & 1;
    if (reading != stepButtonReading[button]) { stepButtonReading[button] = reading; stepButtonChangedAt[button] = now; }
    if ((unsigned long)(now - stepButtonChangedAt[button]) >= kButtonDebounceMs && reading != stepButtonStable[button]) {
      stepButtonStable[button] = reading; handleStepButton(button, reading == LOW);
    }
  }
}

// Routes step buttons to MODE combinations, gates, drone toggles, or live notes.
void handleStepButton(uint8_t button, bool pressed) {
  if (button == modeChordStep) {
    if (!pressed) modeChordStep = 255;
    return;
  }
  if (pressed && (modeButtonStable == LOW || modeButtonReading == LOW) && instrumentMode != MODE_DRONE &&
      (button == 7 || button == 1 || (button == 0 && instrumentMode == MODE_PLAY))) {
    modeButtonChordUsed = true;
    modeChordStep = button;
    modePageTapPending = false;
    if (button == 7) {
      generativeEnabled = !generativeEnabled;
      for (uint8_t step = 0; step < kStepCount; ++step) genSource[step] = step;
      genRandom = (genRandom ^ TCNT0) | 1;
    } else if (button == 1) {
      swingEnabled = !swingEnabled;
    } else togglePlaySequencer();
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

// Stops the previous engine and initializes the selected mode.
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
  edgar.setBackgroundVoices(nextMode == MODE_PLAY && playSequencerEnabled);
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
    playBpmPotLocked = true;
    playBpmPotRaw = stepMotion[1].accepted;
    arpTick = playingStep < kStepCount ? playingStep * 6 : 0;
    applyLiveConfiguration();
  } else {
    applyCurrentProgramBusEffects();
    applyCurrentProgramMorph();
    if (previous == MODE_DRONE) {
      currentStep = 0;
      playingStep = 0;
      if (bitRead(seqGateMask, 0)) triggerSequencerStep(0, false);
      currentStep = 1;
      lastStepMs = now;
      nextStepIntervalMs = stepIntervalMs(0);
    }
  }
  displayDirty = true;
}

// Stops a voice, or fades it out when release is set.
void stopNormalVoice(uint8_t voice, bool release) {
  if (release) edgar.releaseVoice(voice); else edgar.stopVoice(voice);
  normalCurrentPitch[voice] = 0; normalTargetPitch[voice] = 0; normalGlide[voice] = 0;
}

void stopNormalVoices() {
  for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
}

uint16_t pitchForNote(int16_t note) {
  note = constrain(note, 0, SYNTH_MIDI_NOTE_MAX);
  return pitchWordForNote(note);
}

// Returns a note's pitch word detuned by -2400 to +2400 cents.
uint16_t pitchForNoteWithDetuneCents(int16_t note, int16_t detuneCents) {
  detuneCents = constrain(detuneCents, -2400, 2400);
  const int16_t semitones = detuneCents >= 0 ?
    (detuneCents + 50) / 100 : (detuneCents - 50) / 100;
  const int16_t residualCents = detuneCents - semitones * 100;
  const uint16_t basePitch = pitchForNote(note + semitones);
  const int32_t result = basePitch +
    ((int32_t)basePitch * residualCents * 578L) / 1000000L;
  return constrain(result, 1, 65535);
}

// Splits a level budget evenly between voices, capped at full scale and scaled by the preset level.
__attribute__((noinline)) uint8_t outputLevel(uint16_t budget, uint8_t voices, uint8_t presetLevel) {
  uint16_t share = budget / voices;
  if (share > 255) share = 255;
  return (share * (presetLevel + 1U)) >> 8;
}

// Starts a voice at its live or sequence level, gliding when enabled.
void triggerNormalVoice(uint8_t voice, uint16_t pitch, uint8_t glide) {
  normalTargetPitch[voice] = pitch; normalGlide[voice] = glide;
  if (glide == 0 || normalCurrentPitch[voice] == 0) {
    normalCurrentPitch[voice] = pitch; edgar.setPitchWord(voice, pitch);
  }
  edgar.trigger(voice, instrumentMode == MODE_PLAY && voice < liveVoiceEnd() ?
    liveVoiceLevel : sequenceVoiceLevel);
}

// Moves gliding voices toward their target pitch once per millisecond.
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

// Plays a sequencer note as the preset's voice stack; the PLAY background uses at most two voices.
void triggerSequencerNote(uint8_t note, bool backgroundOnly) {
  edgar.stopDrum();
  MelodicPreset preset; readMelodicPreset(seqControls.preset, preset);
  const uint8_t firstVoice = backgroundOnly ? SYNTH_BACKGROUND_VOICE : 0;
  const uint8_t voiceCount = backgroundOnly ?
    min(preset.voiceCount, SYNTH_BACKGROUND_VOICE_COUNT) : preset.voiceCount;
  uint8_t envelope, envelopeLength;
  presetEnvelope(preset, envelope, envelopeLength);
  const uint8_t glide = activeMacroValue(MACRO_GLIDE, preset.glide);
  const uint16_t pulseWidth = pulseWidthForPercent(
    lfoModulatedMacroValue(MACRO_PULSE_WIDTH, preset.squarePulseWidthPercent));
  sequenceVoiceLevel = backgroundOnly ? outputLevel(backgroundVoiceLevel, 1, preset.level) :
    outputLevel(kVoiceLevelBudget, voiceCount, preset.level);

  for (uint8_t voice = firstVoice + voiceCount; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
  for (uint8_t voice = firstVoice; voice < firstVoice + voiceCount; ++voice) {
    const VoiceRecipe &recipe = preset.voices[voice - firstVoice];
    edgar.setWave(voice, recipe.wave); edgar.setEnvelope(voice, envelope);
    edgar.setPulseWidth(voice, pulseWidth);
    edgar.setLength(voice, envelopeLength);
    edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, recipe.modulation));
    triggerNormalVoice(voice, pitchForNoteWithDetuneCents(note, recipe.detuneCents), glide);
  }
}

void triggerDrum(uint8_t drum, bool backgroundOnly) {
  for (uint8_t voice = backgroundOnly ? SYNTH_BACKGROUND_VOICE : 0; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
  DrumPreset preset; readDrumPreset(preset);
  const uint8_t decay = lfoModulatedMacroValue(MACRO_DRUM_DECAY, preset.decay);
  const uint8_t tone = lfoModulatedMacroValue(MACRO_DRUM_TUNE, seqControls.drumTone);
  edgar.triggerDrum(min(drum, (uint8_t)(kDrumCount - 1)), tone, decay,
    outputLevel(backgroundOnly ? backgroundVoiceLevel * 13U / 8 : kVoiceLevelBudget, 1, preset.level));
}

// Returns a step's note as GEN currently plays it.
uint8_t sequencerStepNote(uint8_t step) {
  const uint8_t source = genSource[step];
  return stepNotes[source & 7] + (source & 0x80 ? 12 : 0);
}

void triggerSequencerStep(uint8_t step, bool backgroundOnly) {
  if (isDrumProgram(seqControls.preset)) triggerDrum(stepDrums[step], backgroundOnly);
  else triggerSequencerNote(sequencerStepNote(step), backgroundOnly);
}

// 8-bit xorshift for GEN.
uint8_t nextGenRandom() {
  genRandom ^= genRandom << 7; genRandom ^= genRandom >> 5; genRandom ^= genRandom << 3;
  return genRandom;
}

// GEN: changes at most one step per cycle; step 1, the rhythm, and the pitch set stay.
void mutateGenerativeSteps() {
  const uint8_t roll = nextGenRandom();
  const uint8_t step = roll & 7;
  const uint8_t action = roll >> 5;
  if (step == 0 || action < 4) return;
  if (action == 6) { genSource[step] ^= 0x80; return; }
  if (action == 7) { genSource[step] = step; return; }
  uint8_t source = nextGenRandom() & 7;
  for (uint8_t tries = 0; tries < kStepCount && !bitRead(seqGateMask, source); ++tries) source = (source + 1) & 7;
  genSource[step] = source;
}

// Returns a step's length in ms; swing shifts time within each step pair.
uint16_t stepIntervalMs(uint8_t step) {
  const uint16_t pair = 30000U / seqControls.bpm;
  const uint16_t first = swingEnabled ? pair * kSwingPercent / 100U : pair / 2;
  return (step & 1) ? pair - first : first;
}

// Advances the sequencer without accumulating timing drift.
void runSequencer(unsigned long now) {
  if (instrumentMode == MODE_DRONE || (unsigned long)(now - lastStepMs) < nextStepIntervalMs) return;
  lastStepMs += nextStepIntervalMs;
  nextStepIntervalMs = stepIntervalMs(currentStep);
  if ((unsigned long)(now - lastStepMs) >= nextStepIntervalMs) lastStepMs = now;
  if (currentStep == 0 && generativeEnabled) mutateGenerativeSteps();
  playingStep = currentStep;
  if (instrumentMode == MODE_PLAY) {
    if (playSequencerEnabled) {
      if (bitRead(seqGateMask, currentStep)) {
        selectMacroMode(MODE_SEQ);
        triggerSequencerStep(currentStep, true);
        selectMacroMode(MODE_PLAY);
      } else releaseBackgroundVoices();
    }
  } else if (bitRead(seqGateMask, currentStep)) {
    triggerSequencerStep(currentStep, false);
  } else {
    edgar.releaseDrum();
    for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
  }
  currentStep = (currentStep + 1) % kStepCount;
  if (instrumentMode == MODE_SEQ || !(playingStep & 1)) displayDirty = true;
}

// Clocks arpeggios at six ticks per sequencer step.
void runArpeggio(unsigned long now) {
  if (instrumentMode != MODE_PLAY || playingStep >= kStepCount) return;
  uint8_t sub = (uint16_t)(now - lastStepMs) * 6U / nextStepIntervalMs;
  if (sub > 5) sub = 5;
  const uint8_t tick = playingStep * 6 + sub;
  if (tick == arpTick) return;
  const uint8_t division = pgm_read_byte(&kArpDivisionTicks[playControls.arpDivision]);
  const bool due = tick < arpTick || tick / division != arpTick / division;
  arpTick = tick;
  if (due && playControls.style >= STYLE_ARP_UP) advanceArpeggio();
}

// Applies the PLAY program to the live voices and moves ringing live notes to their level.
void applyLiveConfiguration() {
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  applyCurrentProgramBusEffects();
  const uint16_t pulseWidth = pulseWidthForPercent(lfoModulatedMacroValue(MACRO_PULSE_WIDTH, preset.squarePulseWidthPercent));
  uint8_t envelope, envelopeLength;
  presetEnvelope(preset, envelope, envelopeLength);
  playGlide = activeMacroValue(MACRO_GLIDE, preset.glide);
  uint8_t liveVoices = 1;
  if (playControls.style == STYLE_CHORD) liveVoices = 3;
  else if (playControls.style == STYLE_POLY || preset.voiceCount > 1) {
    liveVoices = min(preset.voiceCount > 1 ? preset.voiceCount : SYNTH_NORMAL_VOICE_COUNT,
      liveVoiceEnd());
  }
  liveVoiceLevel = outputLevel(playSequencerEnabled ?
    kVoiceLevelBudget - backgroundVoiceLevel : kVoiceLevelBudget, liveVoices, preset.level);
  applyCurrentProgramMorph();
  for (uint8_t voice = 0; voice < liveVoiceEnd(); ++voice) {
    edgar.setWave(voice, preset.voices[0].wave);
    edgar.setEnvelope(voice, envelope);
    edgar.setPulseWidth(voice, pulseWidth); edgar.setLevel(voice, liveVoiceLevel);
    edgar.setLength(voice, envelopeLength); edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, preset.voices[0].modulation));
  }
}

// Returns the scale note for a PLAY button.
uint8_t liveNoteForButton(uint8_t button) {
  const int8_t offset = pgm_read_byte(&kScaleOffsets[playControls.scale][button]);
  return constrain(24 + playControls.octave * 12 + playControls.root + offset, 0, 127);
}

// Returns a scale degree's semitone offset, wrapping into higher octaves.
int8_t scaleDegreeOffset(uint8_t scale, uint8_t degree) {
  const uint8_t length = pgm_read_byte(&kScaleLengths[scale]);
  return pgm_read_byte(&kScaleOffsets[scale][degree % length]) + (degree / length) * 12;
}

// Returns the most recently pressed held button, or -1.
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
  for (uint8_t voice = 0; voice < liveVoiceEnd(); ++voice) stopNormalVoice(voice);
  resetLiveAssignments();
}

// Plays one note as a multi-voice program's unison stack across the live voices.
void triggerLiveStack(const MelodicPreset &preset, int16_t note, bool release) {
  const uint8_t liveEnd = liveVoiceEnd();
  const uint8_t stackVoiceCount = min(preset.voiceCount, liveEnd);
  for (uint8_t voice = 0; voice < stackVoiceCount; ++voice) {
    const VoiceRecipe recipe = preset.voices[voice];
    edgar.setWave(voice, recipe.wave);
    edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, recipe.modulation));
    triggerNormalVoice(voice, pitchForNoteWithDetuneCents(note, recipe.detuneCents), playGlide);
  }
  for (uint8_t voice = stackVoiceCount; voice < liveEnd; ++voice) stopNormalVoice(voice, release);
}

// Assigns a free live voice, or steals the oldest one.
void triggerPolyButton(uint8_t button) {
  const uint8_t liveEnd = liveVoiceEnd();
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  if (preset.voiceCount > 1) { triggerLiveStack(preset, liveNoteForButton(button), false); return; }
  int8_t voice = -1;
  for (uint8_t candidate = 0; candidate < liveEnd; ++candidate) {
    if (liveButtonForVoice[candidate] < 0) { voice = candidate; break; }
  }
  if (voice < 0) {
    voice = 0;
    for (uint8_t candidate = 1; candidate < liveEnd; ++candidate) {
      if (liveVoiceAge[candidate] < liveVoiceAge[voice]) voice = candidate;
    }
    const int8_t stolenButton = liveButtonForVoice[voice];
    if (stolenButton >= 0) liveVoiceForButton[stolenButton] = -1;
  }
  liveButtonForVoice[voice] = button; liveVoiceForButton[button] = voice; liveVoiceAge[voice] = buttonPressOrder[button];
  triggerNormalVoice(voice, pitchForNote(liveNoteForButton(button)), playGlide);
}

// Plays the 1-3-5 chord of the latest held button.
void triggerHeldChord() {
  const int8_t button = latestHeldButton();
  if (button < 0) {
    for (uint8_t voice = 0; voice < 3; ++voice) stopNormalVoice(voice, true);
    return;
  }
  const int16_t root = 24 + playControls.octave * 12 + playControls.root;
  for (uint8_t chordVoice = 0; chordVoice < 3; ++chordVoice) {
    const int8_t offset = scaleDegreeOffset(playControls.scale, button + chordVoice * 2);
    triggerNormalVoice(chordVoice, pitchForNote(root + offset), playGlide);
  }
}

uint8_t collectHeldButtons(uint8_t buttons[kStepCount]) {
  uint8_t count = 0;
  for (uint8_t button = 0; button < kStepCount; ++button) if (bitRead(heldButtonMask, button)) buttons[count++] = button;
  return count;
}

// Sounds an arpeggio note: one voice, or a multi-voice program's unison stack.
void triggerArpeggioNote(int16_t note) {
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  if (preset.voiceCount > 1) { triggerLiveStack(preset, note, true); return; }
  for (uint8_t voice = 1; voice < liveVoiceEnd(); ++voice) stopNormalVoice(voice, true);
  triggerNormalVoice(0, pitchForNote(note), playGlide);
}

// Advances the arpeggio across the held buttons, or the latest held chord.
void advanceArpeggio() {
  uint8_t buttons[kStepCount];
  const uint8_t count = collectHeldButtons(buttons);
  if (count == 0) {
    for (uint8_t voice = 0; voice < liveVoiceEnd(); ++voice) stopNormalVoice(voice, true);
    arpPosition = 0; arpDirection = 1; return;
  }

  const bool chordArpeggio = playControls.style >= STYLE_CHORD_ARP_UP;
  const uint8_t noteCount = chordArpeggio ? 3 : count;
  const bool down = playControls.style == STYLE_ARP_DOWN || playControls.style == STYLE_CHORD_ARP_DOWN;
  const bool upDown = playControls.style == STYLE_ARP_UPDOWN || playControls.style == STYLE_CHORD_ARP_UPDOWN;
  uint8_t position;
  if (upDown && noteCount > 1) {
    if (arpPosition >= noteCount) arpPosition = noteCount - 1;
    position = arpPosition;
    if (arpDirection > 0 && arpPosition >= noteCount - 1) arpDirection = -1;
    else if (arpDirection < 0 && arpPosition == 0) arpDirection = 1;
    arpPosition += arpDirection;
  } else {
    arpPosition %= noteCount;
    position = down ? noteCount - 1 - arpPosition : arpPosition;
    arpPosition = (arpPosition + 1) % noteCount;
  }
  triggerArpeggioNote(chordArpeggio ?
    24 + playControls.octave * 12 + playControls.root + scaleDegreeOffset(playControls.scale, latestHeldButton() + position * 2) :
    liveNoteForButton(buttons[position]));
}

// Plays a PLAY press; arpeggios pick it up at their next clock.
void handleLivePress(uint8_t button) {
  applyLiveConfiguration();
  if (playControls.style == STYLE_POLY) triggerPolyButton(button);
  else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  else if ((heldButtonMask & ~(1 << button)) == 0) { arpPosition = 0; arpDirection = 1; }
}

// Releases a PLAY note; arpeggios pick it up at their next clock.
void handleLiveRelease(uint8_t button) {
  if (playControls.style == STYLE_POLY) {
    MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
    if (preset.voiceCount > 1) {
      revoiceHeldButtons();
      return;
    }
    const int8_t voice = liveVoiceForButton[button];
    if (voice >= 0) {
      stopNormalVoice(voice, true); liveButtonForVoice[voice] = -1; liveVoiceForButton[button] = -1;
    }
  } else if (playControls.style == STYLE_CHORD) triggerHeldChord();
}

// Fades PLAY's background notes or drum.
void releaseBackgroundVoices() {
  for (uint8_t voice = SYNTH_BACKGROUND_VOICE; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
  edgar.releaseDrum();
}

// Toggles PLAY's background sequence without restarting ringing live notes.
void togglePlaySequencer() {
  playSequencerEnabled = !playSequencerEnabled;
  const int8_t button = liveButtonForVoice[SYNTH_BACKGROUND_VOICE];
  if (button >= 0) { liveVoiceForButton[button] = -1; liveButtonForVoice[SYNTH_BACKGROUND_VOICE] = -1; }
  releaseBackgroundVoices();
  edgar.setBackgroundVoices(playSequencerEnabled);
  applyLiveConfiguration();
}

void revoiceHeldButtons() {
  if (instrumentMode != MODE_PLAY) return;
  for (uint8_t voice = 0; voice < liveVoiceEnd(); ++voice) stopNormalVoice(voice, true);
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

// Latches one LCD nibble (bits 7-4) with RS (bit 2).
void lcdNibble(uint8_t value) {
  PORTD = value;
  PORTD = value | kLcdEnable;
  _delay_us(1);
  PORTD = value;
}

// Writes an LCD command (rs = 0) or data byte, then waits out its execution time.
void lcdWrite(uint8_t value, uint8_t rs) {
  lcdNibble((value & 0xF0) | rs);
  lcdNibble((uint8_t)(value << 4) | rs);
  _delay_us(53);
}

const uint8_t kLcdInitCommands[] PROGMEM = {
  0x28,
  0x0C,
  0x06,
  0x01,
};

// Initializes the HD44780 in 4-bit mode.
void lcdBegin() {
  DDRD |= 0xFC;
  _delay_ms(50);
  for (uint8_t i = 0; i < 3; ++i) { lcdNibble(0x30); _delay_ms(4.1); }
  lcdNibble(0x20);
  _delay_us(53);
  for (uint8_t i = 0; i < sizeof(kLcdInitCommands); ++i) {
    lcdWrite(pgm_read_byte(&kLcdInitCommands[i]), 0);
    _delay_ms(2);
  }
  memset(glyphKeys, 0xFF, sizeof(glyphKeys));
}

// Loads a CGRAM glyph: a bar or dash (height in bits 0-2), the playhead, or an icon.
void loadGlyph(uint8_t slot, uint8_t key) {
  lcdWrite(0x40 | (slot << 3), 0);
  const uint8_t top = 7 - (key & 7);
  for (uint8_t row = 0; row < 8; ++row) {
    uint8_t bits;
    if (key >= kGlyphPlay) bits = pgm_read_byte(&kGlyphIcons[key - kGlyphPlay][row]);
    else if (key & kGlyphPlayhead) bits = (row & 1) ? 0x0A : 0x15;
    else if (key & kGlyphBar) bits = row >= top ? 0x1F : 0;
    else bits = row == top ? 0x15 : 0;
    lcdWrite(bits, kLcdRs);
  }
}

// Maps glyph placeholders to CGRAM codes 8-15, reusing glyphs already loaded.
void resolveGlyphs() {
  char *const frame = &displayFrame[0][0];
  uint8_t used = 0;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < sizeof(displayFrame); ++i) {
      const uint8_t key = (uint8_t)frame[i] - kGlyphPlaceholder;
      if (key >= kGlyphKeyEnd) continue;
      uint8_t slot = 8, spare = 0, bit = 1;
      for (uint8_t s = 0; s < 8; ++s, bit <<= 1) {
        if (glyphKeys[s] == key) slot = s;
        if (!(used & bit)) spare = s;
      }
      if (slot == 8) {
        if (!pass) continue;
        slot = spare; loadGlyph(slot, key); glyphKeys[slot] = key;
      }
      used |= 1 << slot;
      frame[i] = 8 + slot;
    }
  }
}

// Writes only the LCD characters that changed.
void flushDisplayFrame() {
  for (uint8_t row = 0; row < 2; ++row) {
    uint8_t column = 0;
    while (column < 16) {
      while (column < 16 && renderedFrameReady &&
             displayFrame[row][column] == renderedFrame[row][column]) ++column;
      if (column >= 16) break;
      lcdWrite(0x80 | (row ? 0x40 : 0) | column, 0);
      while (column < 16 && (!renderedFrameReady ||
             displayFrame[row][column] != renderedFrame[row][column])) {
        lcdWrite(displayFrame[row][column], kLcdRs);
        renderedFrame[row][column] = displayFrame[row][column];
        ++column;
      }
    }
  }
  renderedFrameReady = true;
}

// Redraws the active view only when needed.
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
  resolveGlyphs();
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
    case MACRO_FILTER: frameText(0, column, F("FLT")); break;
    case MACRO_RESONANCE: frameText(0, column, F("RES")); break;
    case MACRO_FILTER_ENVELOPE: frameText(0, column, F("FEG")); break;
    default: frameText(0, column, F("---")); break;
  }
}

// Draws a pot's parameter name, or the value of self-describing controls.
void renderParameterName(uint8_t pot) {
  const uint8_t column = pot * 4;
  const uint8_t control = parameterControlForPot(pot);
  if (isMacroControl(control)) {
    frameMacroName(column, macroForCurrentProgram(control - PARAM_MACRO_0));
    return;
  }
  if (isLfoControl(control)) {
    frameText(0, column, (control & 1) ? F("DEP") : F("RAT"));
    return;
  }
  switch (control) {
    case PARAM_SEQ_TUNE: frameText(0, column, F("TUN")); break;
    case PARAM_SEQ_PRESET:
    case PARAM_PLAY_PRESET:
    case PARAM_SEQ_BPM:
    case PARAM_PLAY_ROOT:
    case PARAM_PLAY_SCALE:
    case PARAM_PLAY_STYLE: renderParameterValue(pot); break;
    default: frameText(0, column, F("---")); break;
  }
}

void renderParameterValue(uint8_t pot) {
  if (pot >= 4) return;
  const uint8_t column = pot * 4;
  frameText(0, column, F("   "));
  const uint8_t control = parameterControlForPot(pot);
  if (isMacroControl(control)) {
    frameNumber(0, column, macroState->values[control - PARAM_MACRO_0]);
  } else if (isLfoControl(control)) {
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

// Draws SEQ: parameter names and the step contour.
void renderSequencerDashboard(bool inlineEditVisible) {
  for (uint8_t pot = 0; pot < 4; ++pot) renderParameterName(pot);
  const bool drums = isDrumProgram(seqControls.preset);
  for (uint8_t step = 0; step < kStepCount; ++step) {
    const uint8_t height = drums ? stepDrums[step] * 3 + 1
      : min((uint8_t)((uint8_t)(sequencerStepNote(step) - kStepNoteMin) * 86U >> 8), (uint8_t)7);
    frameChar(1, step, kGlyphPlaceholder | (step == playingStep ? kGlyphPlayhead
      : height | (bitRead(seqGateMask, step) ? kGlyphBar : 0)));
  }
  if (generativeEnabled) frameChar(1, 8, kGenChar);
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

// Draws PLAY: parameter names, sequence status, octave, tempo, and division.
void renderPlayDashboard(bool inlineEditVisible) {
  for (uint8_t pot = 0; pot < 4; ++pot) renderParameterName(pot);
  frameChar(1, 0, !playSequencerEnabled ? kGlyphPlaceholder | kGlyphStop
    : (playingStep & 2) ? ' ' : generativeEnabled ? kGenChar : kGlyphPlaceholder | kGlyphPlay);
  frameChar(1, 1, 'O'); frameChar(1, 2, '1' + playControls.octave);
  frameNumber(1, seqControls.bpm < 100 ? 5 : 4, seqControls.bpm); frameText(1, 7, F("BPM"));
  frameProgmemText(1, 11, kArpDivisionLabels[playControls.arpDivision]);
  if (inlineEditVisible && editedPlayShift > 0) renderParameterValue(editedPlayShift - 1);
}

// Draws DRONE: pair waveforms and oscillator gates.
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
