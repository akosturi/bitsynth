#include <avr/pgmspace.h>
#include <util/delay.h>
#include "synth.h"

// Direct port I/O. Mux S0/S1/S2 = D8/D11/D10 (PB0/PB3/PB2); A0/A2 are the
// parameter/step pot ADC channels, A1/A3 the step/MODE buttons (PC1/PC3).
// The write-only 4-bit LCD uses RS/E/D4-D7 = D2-D7 (PD2-PD7); D0/D1 are unused.
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
const uint8_t kAnalogEndpointMargin = 32;
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
// Swing starts each even-numbered step at this percentage of its step pair.
const uint8_t kSwingPercent = 65;
const uint8_t kStepNoteMin = 36;
const uint8_t kStepNoteMax = 59;
const uint16_t kDroneFrequencyMin = 80;
const uint16_t kDroneFrequencyMax = 600;
const uint16_t kLfoFrequencyMinHundredths = 5;
const uint16_t kLfoFrequencyMaxHundredths = 2000;
const uint8_t kLfoRateCount = 94;
const uint8_t kPresetVcaModulationCount = 99;
// Filter cutoff control value that leaves the bus low-pass open.
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

// Direct-macro slots per program. The two LFO target slots follow them, so
// slot indexes kMacroSlotCount and kMacroSlotCount + 1 address the LFOs.
const uint8_t kMacroSlotCount = 8;
const uint8_t kMacroLfoCount = 2;

enum ParameterControl : uint8_t {
  PARAM_NONE = 0,
  PARAM_SEQ_PRESET, PARAM_SEQ_BPM, PARAM_SEQ_TUNE,
  PARAM_PLAY_PRESET, PARAM_PLAY_ROOT, PARAM_PLAY_SCALE, PARAM_PLAY_STYLE,
  PARAM_MACRO_0,
  // Rate/depth pairs; the LCD relies on rate controls being even.
  PARAM_LFO_0_RATE = PARAM_MACRO_0 + kMacroSlotCount, PARAM_LFO_0_DEPTH, PARAM_LFO_1_RATE, PARAM_LFO_1_DEPTH
};
static_assert((PARAM_LFO_0_RATE & 1) == 0, "LFO rate controls must be even");

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
// macroForCurrentProgram() reads macro and LFO slots as one flash byte run.
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

// label, voice count, voices {wave, detune cents (-2400 to +2400), envelope,
// modulation} x4, envelope length, square pulse width, glide, VCA depth,
// VCA LFO rate, chorus mode, morph speed, filter cutoff (0 about 100 Hz -
// 126 about 3.3 kHz, 127 open), filter resonance (0 Q about 1 - 127 Q about
// 6), filter envelope amount (0 off - 127), direct macros x8, LFO targets x2, LFO rate
// indexes x2 (0-15 = 0.05-0.20 Hz, 16-63 = 0.3-5.0 Hz, 64-93 = 5.5-20 Hz),
// LFO depths x2 (0 off), output level (255 keeps the automatic level; lower
// values only attenuate the preset). Every voice uses voice 0's envelope.
// Available macros: MACRO_NONE, MACRO_PULSE_WIDTH, MACRO_VIBRATO,
// MACRO_DRUM_TUNE, MACRO_DRUM_DECAY, MACRO_VCA_DEPTH, MACRO_VCA_RATE,
// MACRO_ENVELOPE, MACRO_ENVELOPE_LENGTH, MACRO_GLIDE, MACRO_CHORUS,
// MACRO_MORPH, MACRO_FILTER, MACRO_RESONANCE, MACRO_FILTER_ENVELOPE. An LFO that targets PWM or VIB should also have
// that direct macro, so it sweeps around the preset value.
// Letters group the catalogue: A bass, B lead, C pad, D keys, E sequence/effect.
const MelodicPreset kMelodicPresets[] PROGMEM = {
  // A01 funk bass: a Minimoog-style line in the manner of "Thriller"; two
  // slowly beating saws over a square sub-octave with a punchy decay.
  {"A01", 3, {{SAW, 0, ENVELOPE5, 64}, {SAW, 8, ENVELOPE5, 64}, {SQUARE, -1200, ENVELOPE5, 64}, {SINE, 0, ENVELOPE5, 64}},
   72, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, 52, 24, 88,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_FILTER_ENVELOPE, MACRO_NONE, MACRO_NONE},
   {MACRO_NONE, MACRO_NONE}, {0, 0}, {0, 0}, 255},
  // A02 Reese bass: drum and bass detuned saws over a sine sub-octave; a slow
  // filter LFO adds motion, and a faster LFO 1 rate turns it into a wobble.
  {"A02", 3, {{SAW, -14, ENVELOPE4, 64}, {SAW, 14, ENVELOPE4, 64}, {SINE, -1200, ENVELOPE4, 64}, {SINE, 0, ENVELOPE4, 64}},
   106, 50, 0, 0, 0, SYNTH_CHORUS_OFF, 0, 60, 16, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_NONE, MACRO_NONE, MACRO_NONE},
   {MACRO_FILTER, MACRO_NONE}, {16, 0}, {12, 0}, 255},
  // A03 808 bass: a long sine with pitch slides between notes.
  {"A03", 1, {{SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}},
   90, 50, 72, 0, 0, SYNTH_CHORUS_OFF, 0, 127, 0, 0,
   {MACRO_GLIDE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE, MACRO_NONE},
   {MACRO_NONE, MACRO_NONE}, {0, 0}, {0, 0}, 255},
  // B01 pulse lead: the bright, staccato "Axel F" style; a 30% pulse and a
  // slightly detuned saw, one octave up.
  {"B01", 2, {{SQUARE, 1200, ENVELOPE5, 64}, {SAW, 1205, ENVELOPE5, 64}, {SINE, 0, ENVELOPE5, 64}, {SINE, 0, ENVELOPE5, 64}},
   87, 30, 0, 0, 0, SYNTH_CHORUS_OFF, 0, 88, 16, 80,
   {MACRO_PULSE_WIDTH, MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_VIBRATO, MACRO_FILTER_ENVELOPE},
   {MACRO_NONE, MACRO_NONE}, {0, 0}, {0, 0}, 255},
  // B02 supersaw: a trance lead in the MicroKorg/JP-8000 manner; three spread
  // saws one octave up plus one at the played octave, with chorus and glide.
  // VCD raises a fast 8 Hz VCA pulse; LFO 1 depth adds a slow filter sweep.
  {"B02", 4, {{SAW, 1200, ENVELOPE4, 64}, {SAW, 1185, ENVELOPE4, 64}, {SAW, 1215, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}},
   106, 50, 12, 0, 74, SYNTH_CHORUS_1, 0, 101, 0, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_GLIDE, MACRO_CHORUS, MACRO_VIBRATO, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_VCA_DEPTH},
   {MACRO_FILTER, MACRO_NONE}, {5, 0}, {0, 0}, 255},
  // C01 PWM pad: a Juno-style pulse-width-modulated pad with chorus 1 and a
  // soft attack; LFO 2 depth adds a slow filter sweep.
  {"C01", 1, {{SQUARE, 0, ENVELOPE2, 64}, {SQUARE, 0, ENVELOPE2, 64}, {SQUARE, 0, ENVELOPE2, 64}, {SQUARE, 0, ENVELOPE2, 64}},
   99, 50, 0, 0, 0, SYNTH_CHORUS_1, 0, 84, 16, 0,
   {MACRO_PULSE_WIDTH, MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_NONE, MACRO_NONE},
   {MACRO_PULSE_WIDTH, MACRO_FILTER}, {18, 5}, {49, 0}, 255},
  // C02 string machine: Jean-Michel Jarre's "Oxygene" style ensemble strings;
  // a mellow saw with chorus 2, light vibrato, and a slow filter drift.
  {"C02", 1, {{SAW, 0, ENVELOPE2, 66}, {SAW, 0, ENVELOPE2, 66}, {SAW, 0, ENVELOPE2, 66}, {SAW, 0, ENVELOPE2, 66}},
   99, 50, 0, 0, 0, SYNTH_CHORUS_2, 0, 80, 0, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_VIBRATO, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_NONE, MACRO_NONE},
   {MACRO_FILTER, MACRO_NONE}, {7, 0}, {16, 0}, 255},
  // D01 poly brass: the Oberheim-style saw brass of "Jump", for chords.
  {"D01", 1, {{SAW, 0, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}, {SAW, 0, ENVELOPE4, 64}},
   106, 50, 0, 0, 0, SYNTH_CHORUS_1, 0, 88, 0, 64,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE, MACRO_VIBRATO, MACRO_FILTER_ENVELOPE},
   {MACRO_NONE, MACRO_NONE}, {0, 0}, {0, 0}, 255},
  // D02 vibraphone: a struck, long-decaying triangle with motor tremolo,
  // which also passes for a suitcase electric piano.
  {"D02", 1, {{TRIANGLE, 0, ENVELOPE5, 64}, {TRIANGLE, 0, ENVELOPE5, 64}, {TRIANGLE, 0, ENVELOPE5, 64}, {TRIANGLE, 0, ENVELOPE5, 64}},
   99, 50, 0, 96, 69, SYNTH_CHORUS_OFF, 0, 90, 0, 0,
   {MACRO_VCA_DEPTH, MACRO_VCA_RATE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_FILTER, MACRO_RESONANCE, MACRO_CHORUS, MACRO_NONE},
   {MACRO_NONE, MACRO_NONE}, {0, 0}, {0, 0}, 255},
  // D03 rock organ: 8', 4', 5 1/3', and 16' sine drawbars with key click,
  // a fast Leslie-like VCA tremolo, vibrato, and chorus 2. VCR sets the speed.
  {"D03", 4, {{SINE, 0, ENVELOPE3, 66}, {SINE, 1200, ENVELOPE3, 66}, {SINE, 702, ENVELOPE3, 66}, {SINE, -1200, ENVELOPE3, 66}},
   127, 50, 0, 40, 70, SYNTH_CHORUS_2, 0, 127, 0, 0,
   {MACRO_VCA_RATE, MACRO_VCA_DEPTH, MACRO_VIBRATO, MACRO_CHORUS, MACRO_FILTER, MACRO_RESONANCE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE},
   {MACRO_NONE, MACRO_NONE}, {0, 0}, {0, 0}, 255},
  // E01 dark arpeggio: the "Stranger Things" style sequence; a saw and a
  // detuned pulse slowly morph between each other while LFO 1 opens and
  // closes the filter and LFO 2 modulates the pulse width.
  {"E01", 2, {{SAW, 0, ENVELOPE1, 64}, {SQUARE, 6, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}, {SINE, 0, ENVELOPE1, 64}},
   66, 40, 0, 0, 0, SYNTH_CHORUS_OFF, 1, 72, 48, 64,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_MORPH, MACRO_PULSE_WIDTH, MACRO_CHORUS, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_GLIDE},
   {MACRO_FILTER, MACRO_PULSE_WIDTH}, {5, 20}, {40, 28}, 255},
  // E02 surf: the sea and wind noise of Jarre's "Oxygene"; held white noise
  // whose filter LFO and slower VCA drift against each other. A short LEN
  // with ENV 0 turns it into hi-hat-like ticks in SEQ.
  {"E02", 1, {{NOISE, 0, ENVELOPE3, 64}, {NOISE, 0, ENVELOPE3, 64}, {NOISE, 0, ENVELOPE3, 64}, {NOISE, 0, ENVELOPE3, 64}},
   127, 50, 0, 200, 10, SYNTH_CHORUS_OFF, 0, 68, 48, 0,
   {MACRO_FILTER, MACRO_RESONANCE, MACRO_VCA_DEPTH, MACRO_VCA_RATE, MACRO_ENVELOPE_LENGTH, MACRO_ENVELOPE, MACRO_CHORUS, MACRO_NONE},
   {MACRO_FILTER, MACRO_NONE}, {7, 0}, {45, 0}, 255}
};
const uint8_t kMelodicPresetCount = sizeof(kMelodicPresets) / sizeof(kMelodicPresets[0]);
// SEQ lists every melodic program and then DRM; PLAY omits DRM.
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
// Arpeggio divisions in 24-PPQN ticks. 48 ticks equal one eight-step
// sequencer cycle, so every division restarts in phase with step 1.
const uint8_t kArpDivisionCount = 8;
const uint8_t kArpDivisionTicks[kArpDivisionCount] PROGMEM = {48, 24, 16, 12, 8, 6, 4, 3};
const char kArpDivisionLabels[kArpDivisionCount][6] PROGMEM = {"1/2", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32"};
const uint8_t kPlayOctaveCount = 5;
synth edgar;
InstrumentMode instrumentMode = MODE_SEQ;
SeqControls seqControls = {0, 126, 64};
// PLAY indexes its melodic-only catalogue; SEQ indexes the complete catalogue.
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
// SEQ and PLAY keep separate macro and macro-LFO values for their selected
// programs. macroMode selects the program and state the macro helpers use:
// normally the current mode, and SEQ while PLAY triggers its background step.
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
// One voice or the drum reaches full PWM scale, so every voice that can sound
// at once shares SYNTH_LEVEL_BUDGET. PLAY's background voice keeps a fixed
// share of about three tenths, so it stands out over the live voices and
// live style changes never need to rescale a ringing sequence.
const uint8_t kBackgroundVoiceLevel = 79;
uint8_t sequenceVoiceLevel = kBackgroundVoiceLevel;
uint8_t liveVoiceLevel = kBackgroundVoiceLevel;
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
// GEN plays each step from a source step (bits 0-2), optionally an octave up
// (bit 7). The identity map is the programmed sequence.
bool generativeEnabled = false;
uint8_t genSource[kStepCount] = {0, 1, 2, 3, 4, 5, 6, 7};
uint8_t genRandom = 1;
// GEN uses the ROM's centred 5x5 asterisk, leaving all of CGRAM free.
const char kGenChar = '*';
// Rendered frames hold kGlyphPlaceholder | key; resolveGlyphs() loads each key
// into a CGRAM slot and writes its mirror code 8-15, so a frame has no NUL.
// Bar keys: bits 0-2 are height - 1, kGlyphBar a solid bar (else a dotted
// ghost dash at the bar's top). kGlyphPlayhead alone is a plain checkerboard.
// Icons follow.
const uint8_t kGlyphPlaceholder = 0x80;
const uint8_t kGlyphBar = 0x08;
const uint8_t kGlyphPlayhead = 0x10;
const uint8_t kGlyphPlay = 0x20;
const uint8_t kGlyphStop = 0x21;
const uint8_t kGlyphKeyEnd = 0x22;
const uint8_t kGlyphIcons[2][8] PROGMEM = {
  {0x08, 0x0C, 0x0E, 0x0F, 0x0E, 0x0C, 0x08, 0x00},  // Play.
  {0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00},  // Stop.
};
uint8_t glyphKeys[8];  // Key loaded in each CGRAM slot; 0xFF is unknown.
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

// Slots 0-7 are direct macros and kMacroSlotCount + lfo are LFO targets.
uint8_t macroForCurrentProgram(uint8_t slot) {
  const uint8_t programId = macroProgramId();
  const uint8_t *slots = isDrumProgram(programId) ? kDrumPreset.macros :
    kMelodicPresets[programId].macros;
  return pgm_read_byte(slots + slot);
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
  for (uint8_t slot = 0; slot < kMacroSlotCount; ++slot) if (macroForCurrentProgram(slot) != MACRO_NONE) ++count;
  for (uint8_t lfo = 0; lfo < kMacroLfoCount; ++lfo) if (macroForCurrentProgram(kMacroSlotCount + lfo) != MACRO_NONE) count += 2;
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

// Returns the direct-macro slot assigned to parameter, or kMacroSlotCount when there is none.
uint8_t macroSlot(uint8_t parameter) {
  uint8_t slot = 0;
  while (slot < kMacroSlotCount && macroForCurrentProgram(slot) != parameter) ++slot;
  return slot;
}

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
  for (uint8_t lfo = 0; lfo < kMacroLfoCount; ++lfo) {
    if (macroForCurrentProgram(kMacroSlotCount + lfo) != parameter || macroState->lfoDepth[lfo] == 0) continue;
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
    case MACRO_FILTER: return mapByteHysteresis(raw, activeMacroValue(parameter, kFilterOpen), 127, 0);
    case MACRO_RESONANCE:
    case MACRO_FILTER_ENVELOPE: return mapByteHysteresis(raw, activeMacroValue(parameter, 0), 127, 0);
    default: return 0;
  }
}

// LEN is a perceptual control, not the raw envelope-step index: 0-126 span roughly
// 0.2 s to 6.5 s at the 5 kHz envelope rate, and 127 is a held note. The engine's slowest non-zero
// phase increment is one, so the upper value is necessarily approximate.
const uint8_t kEnvelopeLengthRateAnchors[] PROGMEM = {33, 22, 15, 10, 7, 5, 3, 2, 1};

// LEN 0-126 run from the shortest to the longest envelope and 127 is HOLD, so
// turning away from HOLD reaches the longest lengths first instead of
// cutting held notes off with the shortest one.
const uint8_t kEnvelopeHoldControl = 127;

// Interpolates a descending nine-anchor flash table at index 0-127.
uint8_t descendingAnchorValue(const uint8_t *anchors, uint8_t index) {
  const uint8_t segment = index >> 4;
  const uint8_t fraction = index & 0x0F;
  const uint8_t start = pgm_read_byte(&anchors[segment]);
  const uint8_t end = pgm_read_byte(&anchors[segment + 1]);
  return start - (((uint16_t)(start - end) * fraction + 8) >> 4);
}

uint8_t envelopeRateForControl(uint8_t control) {
  if (control >= kEnvelopeHoldControl) return 0;
  return descendingAnchorValue(kEnvelopeLengthRateAnchors, control);
}

// State-variable filter coefficients f = 2 sin(pi fc / 20 kHz) * 256 for an
// exponential 3.3 kHz-100 Hz cutoff sweep. Control 0-126 reads them in
// reverse, so higher values are brighter (about 100 Hz-3.3 kHz); 127 is open.
const uint8_t kFilterCoefficientAnchors[] PROGMEM = {255, 169, 110, 72, 46, 30, 19, 12, 8};
// Damping q = 128 / Q for an exponential Q of 1-6 across RES 0-127.
const uint8_t kFilterDampingAnchors[] PROGMEM = {128, 102, 82, 65, 52, 42, 33, 27, 21};

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
  // FEG 0-127 maps quadratically to the engine's 0-252 amount, giving finer
  // control over small sweeps.
  edgar.setBusFilter(cutoff >= kFilterOpen ? 0 :
    descendingAnchorValue(kFilterCoefficientAnchors, kFilterOpen - cutoff),
    descendingAnchorValue(kFilterDampingAnchors, resonance),
    (uint8_t)((uint16_t)(envelopeAmount * envelopeAmount) >> 6));
}

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

// Resolves a melodic preset's envelope and envelope-step length index. Without a LEN
// macro the stored length is used unchanged; LEN 0, or a stored length whose
// speed is zero, holds the constant full-level envelope until note-off.
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

// The VCA follows the stored depth and rate unless a macro or LFO moves them.
void applyCurrentProgramVca() {
  const uint8_t programId = macroProgramId();
  if (isDrumProgram(programId)) { edgar.disablePlayVca(); return; }
  MelodicPreset preset; readMelodicPreset(programId, preset);
  edgar.configurePlayVca(lfoModulatedMacroValue(MACRO_VCA_DEPTH, preset.vcaDepth),
    presetVcaRateHundredths((uint8_t)lfoModulatedMacroValue(MACRO_VCA_RATE, preset.vcaModulation)));
}

void applyCurrentProgramBusEffects() {
  applyCurrentProgramFilter();
  applyCurrentProgramVca();
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
  } else if (parameter >= MACRO_FILTER) {
    // FLT, RES, and FEG are the last macro IDs.
    applyCurrentProgramFilter();
  } else if (parameter == MACRO_VCA_DEPTH || parameter == MACRO_VCA_RATE) {
    applyCurrentProgramVca();
  }
  showParameterEdit(displaySlot, now);
}

// control is 0-3: LFO rate/depth pairs in parameter order.
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

bool parameterPotUnlocked(uint8_t pot, uint16_t raw) {
  if (!parameterPageLocked[pot]) return true;
  if (abs((int16_t)raw - (int16_t)parameterPageRaw[pot]) < kSettingMovementThreshold) return false;
  parameterPageLocked[pot] = false;
  return true;
}

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

// Macro LFOs are intentionally evaluated in the foreground at 250 Hz.  They
// only write resolved engine values, keeping descriptor lookup and routing out
// of the 20 kHz audio ISR.
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
      for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) edgar.setPulseWidth(voice, width);
    } else if (target == MACRO_VIBRATO) {
      const uint8_t modulation = lfoModulatedMacroValue(target, 64);
      for (uint8_t voice = firstMacroVoice(); voice < SYNTH_VOICE_COUNT; ++voice) edgar.setMod(voice, modulation);
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

// Channel bits 0/1/2 map to PB0/PB3/PB2. No ISR writes PORTB, so the
// read-modify-write is safe; Timer1 owns PB1 through OC1A.
void selectMuxChannel(uint8_t channel) {
  PORTB = (PORTB & ~kMuxPortMask) | (channel & 0x05) | ((channel & 0x02) << 2);
  _delay_us(kMuxSettleUs);
}

// One conversion with AVcc reference; the core's init() enabled the ADC at
// clock/128, as analogRead() used.
uint16_t readAdc() {
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC)) {}
  return ADC;
}

// Discards settling reads and returns the median to reject analog spikes.
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

// Spreads the 959-count span between the endpoint margins over 0-1022 (x16/15
// in 16 bits), so the margins do not swallow the values next to each endpoint.
static_assert(kAnalogEndpointMargin == 32, "usableAnalogRaw scales the 959-count usable span");
uint16_t usableAnalogRaw(uint16_t raw) {
  return (uint16_t)((raw - kAnalogEndpointMargin) << 4) / 15U;
}

// Maps a control only after it clears the next value boundary and hysteresis.
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
  // Map the reading less the hysteresis, so a slow turn advances one value at a time.
  const int16_t settled = constrain(map(rising ? raw - kAnalogHysteresis : raw + kAnalogHysteresis,
                                        0, 1023, outMin, outMax), low, high);
  return candidate > current ? max(settled, next) : min(settled, next);
}

uint8_t mapByteHysteresis(uint16_t raw, uint8_t current, int16_t outMin, int16_t outMax) {
  return (uint8_t)mapWithHysteresis(raw, current, outMin, outMax);
}

// raw and count are 16-bit on the ATmega328P.  Keep this product wide: the
// LFO's 94 bins otherwise overflow above roughly two thirds of the pot,
// making its 0.05-0.20 Hz range unreachable.
uint8_t reversedIndexForRaw(uint16_t raw, uint8_t count) {
  return count - 1 - min((uint8_t)(((uint32_t)raw * count) >> 10), (uint8_t)(count - 1));
}

// Maps a reversed control to a stable discrete index.
uint8_t mapIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count) {
  if (count <= 1) return 0;
  if (raw <= kAnalogEndpointMargin) return count - 1;
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;
  raw = usableAnalogRaw(raw);

  const uint8_t candidate = reversedIndexForRaw(raw, count);
  if (current >= count || candidate == current) return candidate;

  // Once past the boundary and hysteresis, map the reading less the
  // hysteresis, so a slow turn advances one index at a time. The boundary is
  // current's raw edge on the approached side. Keep its product 32-bit for the
  // same reason as reversedIndexForRaw(): (count - current) << 10 overflows
  // 16 bits once 64 or more bins remain, which stalled low LFO rates.
  const bool lower = candidate < current;
  const uint16_t boundary = ((uint32_t)(count - current - !lower) << 10) / count;
  if (lower) {
    if (raw <= boundary + kAnalogHysteresis) return current;
    return min(reversedIndexForRaw(raw - kAnalogHysteresis, count), (uint8_t)(current - 1));
  }
  if (raw + kAnalogHysteresis >= boundary) return current;
  return max(reversedIndexForRaw(raw + kAnalogHysteresis, count), (uint8_t)(current + 1));
}

void revoiceHeldButtons();
void stopNormalVoice(uint8_t voice, bool release = false);

void lockParameterPageControls() {
  for (uint8_t pot = 0; pot < kSettingCount; ++pot) {
    parameterPageRaw[pot] = settingValues[pot];
    parameterPageLocked[pot] = true;
  }
}

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

// Scans a subset of multiplexed controls each loop to limit latency.
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

// MODE + a DRONE parameter pot edits that pair's amplitude-LFO rate.
void readDroneControls(const uint16_t values[], unsigned long now) {
  // MODE press and release relock the pots, so neither layer jumps to the
  // position left by the other until that pot moves again.
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

void readPlayStepPot(uint8_t step, unsigned long now) {
  selectMuxChannel(step);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAdcChannel), filteredStepRaw[step], stepFilterReady[step]);
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[step], kStepMovementThreshold);
  if (step == 0) {
    const uint8_t next = mapIndexHysteresis(raw, playControls.octave, kPlayOctaveCount);
    if (next != playControls.octave) { playControls.octave = next; revoiceHeldButtons(); displayDirty = true; }
  } else if (step == 1) {
    // The tempo is shared with SEQ, so it waits for movement after entering PLAY.
    if (playBpmPotLocked) {
      if (abs((int16_t)raw - (int16_t)playBpmPotRaw) < kStepMovementThreshold) return;
      playBpmPotLocked = false;
    }
    const uint8_t next = mapByteHysteresis(raw, seqControls.bpm, kBpmMax, kBpmMin);
    if (next != seqControls.bpm) { seqControls.bpm = next; displayDirty = true; }
  } else if (step == 2) {
    const uint8_t next = mapIndexHysteresis(raw, playControls.arpDivision, kArpDivisionCount);
    if (next != playControls.arpDivision) { playControls.arpDivision = next; displayDirty = true; }
  }
}


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

// One short MODE tap pages through controls; two quick taps change mode.
void scanModeButton(unsigned long now) {
  const uint8_t reading = (PINC >> kModeButtonBit) & 1;
  if (reading != modeButtonReading) { modeButtonReading = reading; modeButtonChangedAt = now; }
  if ((unsigned long)(now - modeButtonChangedAt) >= kButtonDebounceMs && reading != modeButtonStable) {
    modeButtonStable = reading;
    // DRONE's waveform and MODE + pot LFO layers share pots; soft takeover
    // applies whenever MODE switches between them.
    if (instrumentMode == MODE_DRONE) lockParameterPageControls();
    if (reading == LOW) {
      modeButtonPressedAt = now;
      // A chord step may already be held: it can be accepted while MODE is
      // still settling (see handleStepButton()).
      modeButtonChordUsed = modeChordStep != 255;
      // The second tap counts when it is pressed within the window after the
      // first release; requiring its release there was too tight for a real,
      // debounced button.
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
    const uint8_t reading = (PINC >> kStepButtonBit) & 1;
    if (reading != stepButtonReading[button]) { stepButtonReading[button] = reading; stepButtonChangedAt[button] = now; }
    if ((unsigned long)(now - stepButtonChangedAt[button]) >= kButtonDebounceMs && reading != stepButtonStable[button]) {
      stepButtonStable[button] = reading; handleStepButton(button, reading == LOW);
    }
  }
}

// Routes step buttons to gates, drone toggles, or live notes by mode.
void handleStepButton(uint8_t button, bool pressed) {
  if (button == modeChordStep) {
    if (!pressed) modeChordStep = 255;
    return;
  }
  // MODE + step 8 toggles GEN and MODE + step 2 swing in SEQ and PLAY;
  // MODE + step 1 toggles PLAY's background sequence. The chord button's
  // release is swallowed. MODE's raw reading also counts, so a chord whose
  // step lands up to one debounce period before MODE plays no live note.
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
      // The new step lengths start with the next step.
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

// A PLAY note-off releases the voice with a short fade instead of cutting it.
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

// Presets may specify every integer cent from -2400 to +2400. A nearby pitch
// table value keeps the control-rate calculation compact; the residual is
// then applied as a fixed-point detune correction.
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

// Splits budget evenly between voices, capped at full level; a preset level
// below 255 only attenuates that share.
__attribute__((noinline)) uint8_t outputLevel(uint16_t budget, uint8_t voices, uint8_t presetLevel) {
  uint16_t share = budget / voices;
  if (share > 255) share = 255;
  return (share * (presetLevel + 1U)) >> 8;
}

void triggerNormalVoice(uint8_t voice, uint16_t pitch, uint8_t glide) {
  normalTargetPitch[voice] = pitch; normalGlide[voice] = glide;
  if (glide == 0 || normalCurrentPitch[voice] == 0) {
    normalCurrentPitch[voice] = pitch; edgar.setPitchWord(voice, pitch);
  }
  edgar.trigger(voice, instrumentMode == MODE_PLAY && voice >= firstLiveVoice() ?
    liveVoiceLevel : sequenceVoiceLevel);
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
  sequenceVoiceLevel = outputLevel(backgroundOnly ? kBackgroundVoiceLevel : SYNTH_LEVEL_BUDGET,
    voiceCount, preset.level);

  // Unused voices stop first, so louder new levels never overlap them.
  if (!backgroundOnly) for (uint8_t voice = voiceCount; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
  for (uint8_t voice = 0; voice < voiceCount; ++voice) {
    const VoiceRecipe &recipe = preset.voices[voice];
    edgar.setWave(voice, recipe.wave); edgar.setEnvelope(voice, envelope);
    edgar.setPulseWidth(voice, pulseWidth);
    edgar.setLength(voice, envelopeLength);
    edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, recipe.modulation));
    triggerNormalVoice(voice, pitchForNoteWithDetuneCents(note, recipe.detuneCents), glide);
  }
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
  edgar.triggerDrum(min(drum, (uint8_t)(kDrumCount - 1)), tone, decay,
    outputLevel(backgroundOnly ? kBackgroundVoiceLevel : SYNTH_LEVEL_BUDGET, 1, preset.level));
}

// A step's note as GEN currently plays it.
uint8_t sequencerStepNote(uint8_t step) {
  const uint8_t source = genSource[step];
  return stepNotes[source & 7] + (source & 0x80 ? 12 : 0);
}

void triggerSequencerStep(uint8_t step, bool backgroundOnly) {
  if (isDrumProgram(seqControls.preset)) triggerDrum(stepDrums[step], backgroundOnly);
  else triggerSequencerNote(sequencerStepNote(step), backgroundOnly);
}

uint8_t nextGenRandom() {
  genRandom ^= genRandom << 7; genRandom ^= genRandom >> 5; genRandom ^= genRandom << 3;
  return genRandom;
}

// GEN changes at most one step per eight-step cycle, using only gated steps'
// own notes and octaves, so the pitch-class set and rhythm stay as programmed.
// Step 1 never changes and anchors the key; reverts pull the line back home.
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

// A step pair lasts two sixteenths. Swing lengthens the odd-numbered (first)
// step and delays the even-numbered step by the same amount, so pairs and the
// tempo stay exact. 60 BPM x 65% is 32,500, inside 16 bits.
uint16_t stepIntervalMs(uint8_t step) {
  const uint16_t pair = 30000U / seqControls.bpm;
  const uint16_t first = swingEnabled ? pair * kSwingPercent / 100U : pair / 2;
  return (step & 1) ? pair - first : first;
}

// Advances the shared sequencer without accumulating large timing drift.
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
      } else { stopNormalVoice(0, true); edgar.releaseDrum(); }
    }
  } else if (bitRead(seqGateMask, currentStep)) {
    triggerSequencerStep(currentStep, false);
  } else {
    // A rest fades the previous step out instead of cutting it.
    edgar.releaseDrum();
    for (uint8_t voice = 0; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
  }
  currentStep = (currentStep + 1) % kStepCount;
  // SEQ redraws its playhead every step; PLAY's play icon changes every two.
  if (instrumentMode == MODE_SEQ || !(playingStep & 1)) displayDirty = true;
}

// Clocks arpeggios from the sequencer position: each step spans six ticks,
// interpolated from the elapsed part of the current step interval.
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

void applyLiveConfiguration() {
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  applyCurrentProgramBusEffects();
  const uint16_t pulseWidth = pulseWidthForPercent(lfoModulatedMacroValue(MACRO_PULSE_WIDTH, preset.squarePulseWidthPercent));
  uint8_t envelope, envelopeLength;
  presetEnvelope(preset, envelope, envelopeLength);
  playGlide = activeMacroValue(MACRO_GLIDE, preset.glide);
  // Live notes take effect at their next trigger. Every setting that changes
  // the simultaneous voice count also stops and revoices the live notes.
  // Arpeggios play a multi-voice program as one unison stack, like POLY.
  uint8_t liveVoices = 1;
  if (playControls.style == STYLE_CHORD) liveVoices = 3;
  else if (playControls.style == STYLE_POLY || preset.voiceCount > 1) {
    liveVoices = min(preset.voiceCount > 1 ? preset.voiceCount : SYNTH_VOICE_COUNT,
      SYNTH_VOICE_COUNT - playSequencerEnabled);
  }
  liveVoiceLevel = outputLevel(playSequencerEnabled ?
    SYNTH_LEVEL_BUDGET - kBackgroundVoiceLevel : SYNTH_LEVEL_BUDGET, liveVoices, preset.level);
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
  return constrain(24 + playControls.octave * 12 + playControls.root + offset, 0, 127);
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

// Plays one note as a multi-voice program's detuned unison stack across the
// live pool: four oscillators normally, or three while voice 0 sequences.
void triggerLiveStack(const MelodicPreset &preset, int16_t note, bool release) {
  const uint8_t firstVoice = firstLiveVoice();
  const uint8_t stackVoiceCount = min(preset.voiceCount, (uint8_t)(SYNTH_VOICE_COUNT - firstVoice));
  for (uint8_t stackVoice = 0; stackVoice < stackVoiceCount; ++stackVoice) {
    const uint8_t voice = firstVoice + stackVoice;
    const VoiceRecipe recipe = preset.voices[stackVoice];
    edgar.setWave(voice, recipe.wave);
    edgar.setMod(voice, lfoModulatedMacroValue(MACRO_VIBRATO, recipe.modulation));
    triggerNormalVoice(voice, pitchForNoteWithDetuneCents(note, recipe.detuneCents), playGlide);
  }
  for (uint8_t voice = firstVoice + stackVoiceCount; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, release);
}

// Assigns a free live voice or steals the oldest active one.
void triggerPolyButton(uint8_t button) {
  const uint8_t firstVoice = firstLiveVoice();
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  // A multi-voice program is a unison stack in POLY.  It consumes the whole
  // live pool: four oscillators normally, or three while voice 0 sequences.
  if (preset.voiceCount > 1) { triggerLiveStack(preset, liveNoteForButton(button), false); return; }
  // Free voices are taken from the top, so voice 0 is used last and starting
  // the background sequence rarely has to end a live note.
  int8_t voice = -1;
  for (int8_t candidate = SYNTH_VOICE_COUNT - 1; candidate >= (int8_t)firstVoice; --candidate) {
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
    for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
    return;
  }
  const int16_t root = 24 + playControls.octave * 12 + playControls.root;
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

// Sounds an arpeggio note: a single voice, or a multi-voice program's unison stack.
void triggerArpeggioNote(int16_t note) {
  MelodicPreset preset; readMelodicPreset(playProgramId(), preset);
  if (preset.voiceCount > 1) { triggerLiveStack(preset, note, true); return; }
  for (uint8_t voice = 2; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
  triggerNormalVoice(1, pitchForNote(note), playGlide);
}

// Advances the current arpeggio pattern across held buttons, or the latest held chord.
void advanceArpeggio() {
  uint8_t buttons[kStepCount];
  const uint8_t count = collectHeldButtons(buttons);
  if (count == 0) {
    for (uint8_t voice = firstLiveVoice(); voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
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
      stopNormalVoice(voice, true); liveButtonForVoice[voice] = -1; liveVoiceForButton[button] = -1;
    }
  } else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  // Keep the current arp note until the next sequencer clock, where the
  // updated held-button set is applied.
}

// Hands voice 0 between PLAY's background sequence and the live pool without
// restarting ringing live notes. POLY fills voice 0 last, so only a fourth
// held note or a unison stack's first voice fades when the sequence starts;
// stopping it fades the background note. The other live voices are only
// capped to the new level share (a no-op when the share grows). Like a key
// press, the toggle refreshes the live configuration.
void togglePlaySequencer() {
  playSequencerEnabled = !playSequencerEnabled;
  const int8_t button = liveButtonForVoice[0];
  if (button >= 0) { liveVoiceForButton[button] = -1; liveButtonForVoice[0] = -1; }
  stopNormalVoice(0, true); edgar.releaseDrum();
  edgar.setVoiceZeroBackground(playSequencerEnabled);
  applyLiveConfiguration();
  for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) edgar.limitLevel(voice, liveVoiceLevel);
}

// Rebuilds live voice assignments after a play setting changes.
void revoiceHeldButtons() {
  if (instrumentMode != MODE_PLAY) return;
  for (uint8_t voice = firstLiveVoice(); voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice, true);
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

// Latches one LCD nibble (bits 7-4) with RS (bit 2); E is high for >= 1 us.
void lcdNibble(uint8_t value) {
  PORTD = value;
  PORTD = value | kLcdEnable;
  _delay_us(1);
  PORTD = value;
}

// Writes a command (rs = 0) or data byte (rs = kLcdRs), then waits out the
// 37 us execution time scaled to the slowest 190 kHz controller clock.
void lcdWrite(uint8_t value, uint8_t rs) {
  lcdNibble((value & 0xF0) | rs);
  lcdNibble((uint8_t)(value << 4) | rs);
  _delay_us(53);
}

// HD44780 4-bit initialization by instruction; CGRAM loads on demand.
const uint8_t kLcdInitCommands[] PROGMEM = {
  0x28,  // 4-bit, two lines, 5x8 font.
  0x0C,  // Display on, cursor and blink off.
  0x06,  // Increment, no shift.
  0x01,  // Clear.
};

void lcdBegin() {
  DDRD |= 0xFC;
  _delay_ms(50);
  for (uint8_t i = 0; i < 3; ++i) { lcdNibble(0x30); _delay_ms(4.1); }
  lcdNibble(0x20);
  _delay_us(53);
  for (uint8_t i = 0; i < sizeof(kLcdInitCommands); ++i) {
    lcdWrite(pgm_read_byte(&kLcdInitCommands[i]), 0);
    _delay_ms(2);  // Covers clear's 1.52 ms at 270 kHz, scaled to 190 kHz.
  }
  memset(glyphKeys, 0xFF, sizeof(glyphKeys));
}

// Loads a glyph key into a CGRAM slot (about 0.55 ms). Every flush run sets
// its DDRAM address first, so the address may be left in CGRAM.
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

// Maps placeholders to CGRAM codes 8-15. Glyphs already loaded keep their
// slots, so a playhead move reloads at most two. No view needs more than
// eight distinct glyphs (the SEQ contour: seven steps plus the playhead), so
// pass 1 always finds a slot that pass 0 left unused.
void resolveGlyphs() {
  char *const frame = &displayFrame[0][0];
  uint8_t used = 0;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    // Both rows as one array; row 1 starts at 17, and each row's NUL is skipped.
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

// Writes only LCD regions that differ from the rendered frame.
void flushDisplayFrame() {
  for (uint8_t row = 0; row < 2; ++row) {
    uint8_t column = 0;
    while (column < 16) {
      while (column < 16 && renderedFrameReady &&
             displayFrame[row][column] == renderedFrame[row][column]) ++column;
      if (column >= 16) break;
      lcdWrite(0x80 | (row ? 0x40 : 0) | column, 0);  // Set DDRAM address.
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
    // Program, tempo, root, scale, and style values identify themselves, so they stay shown.
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

// Each step is a bar whose height follows its note as GEN plays it (C2-B3 in
// eight heights of three semitones, so B3 fills the character; clamped above),
// or KICK/SNAR/CHH low, middle, and full height in DRM. Disabled
// steps show only a dotted dash at that height; the playhead step is a plain
// checkerboard.
void renderSequencerDashboard(bool inlineEditVisible) {
  for (uint8_t pot = 0; pot < 4; ++pot) renderParameterName(pot);
  const bool drums = isDrumProgram(seqControls.preset);
  for (uint8_t step = 0; step < kStepCount; ++step) {
    const uint8_t height = drums ? stepDrums[step] * 3 + 1
      // x86 >> 8 equals / 3 over the span, including GEN's octave up.
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

void renderPlayDashboard(bool inlineEditVisible) {
  for (uint8_t pot = 0; pot < 4; ++pot) renderParameterName(pot);
  // The play icon blinks with the beat: lit for the first two steps of each
  // four-step quarter note. GEN's asterisk replaces it and blinks alike.
  frameChar(1, 0, !playSequencerEnabled ? kGlyphPlaceholder | kGlyphStop
    : (playingStep & 2) ? ' ' : generativeEnabled ? kGenChar : kGlyphPlaceholder | kGlyphPlay);
  frameChar(1, 1, 'O'); frameChar(1, 2, '1' + playControls.octave);
  frameNumber(1, seqControls.bpm < 100 ? 5 : 4, seqControls.bpm); frameText(1, 7, F("BPM"));
  frameProgmemText(1, 11, kArpDivisionLabels[playControls.arpDivision]);
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
