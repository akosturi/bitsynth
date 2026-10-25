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
const uint8_t kPlayMorphWaveCount = 4;
const uint8_t kPlayMorphOff = 255;
const uint8_t kPlayFilterLevelCount = 8;
const uint8_t kSeqPresetCount = 9;
const uint8_t kLivePresetCount = 8;
const uint8_t kDrumCount = DRUM_SAMPLE_COUNT;
const uint8_t kScaleCount = 4;
const uint8_t kPlayStyleCount = 5;
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
const uint16_t kPanicHoldMs = 1000;
const uint16_t kEditViewMs = 900;
const uint8_t kStepNoteMin = 36;
const uint8_t kStepNoteMax = 59;
const uint16_t kDroneFrequencyMin = 80;
const uint16_t kDroneFrequencyMax = 600;
const uint16_t kLfoFrequencyMinTenths = 1;
const uint16_t kLfoFrequencyMaxTenths = 50;

enum InstrumentMode : uint8_t { MODE_SEQ = 0, MODE_PLAY = 1, MODE_DRONE = 2 };
enum ScaleMode : uint8_t { SCALE_MAJOR = 0, SCALE_MINOR = 1, SCALE_PENTATONIC = 2, SCALE_BLUES = 3 };
enum PlayStyle : uint8_t { STYLE_POLY = 0, STYLE_CHORD = 1, STYLE_ARP_UP = 2, STYLE_ARP_DOWN = 3, STYLE_ARP_UPDOWN = 4 };
enum SeqPresetId : uint8_t { PRESET_SUB = 0, PRESET_BASS, PRESET_PLUCK, PRESET_BELL, PRESET_LEAD, PRESET_CHORD, PRESET_CHIP, PRESET_PERC, PRESET_DRUM };
enum DrumId : uint8_t { DRUM_KICK = 0, DRUM_SNARE, DRUM_CLOSED_HAT };

struct VoiceRecipe {
  uint8_t wave;
  int8_t semitone;
  uint8_t envelope;
  uint8_t modulation;
};

struct SeqPreset {
  char label[5];
  uint8_t voiceCount;
  VoiceRecipe voices[4];
};

struct LivePreset {
  char label[5];
  uint8_t wave;
  uint8_t envelope;
  uint8_t length;
  uint8_t modulation;
  uint8_t sampleHold;
};

struct SeqControls { uint8_t preset; uint8_t bpm; uint8_t color; uint8_t shape; };
struct PlayControls { uint8_t preset; uint8_t root; uint8_t scale; uint8_t style; uint8_t octave; };

struct AnalogMotionState {
  uint16_t accepted;
  uint8_t confirmations;
  int8_t direction;
  bool ready;
};

const SeqPreset kSeqPresets[kSeqPresetCount] PROGMEM = {
  {"SUB",  2, {{SINE, 0, ENVELOPE0, 64}, {SQUARE, -12, ENVELOPE0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}},
  {"BAS", 2, {{SAW, 0, ENVELOPE1, 64}, {SQUARE, -12, ENVELOPE1, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}},
  {"PLK", 2, {{TRIANGLE, 0, ENVELOPE1, 64}, {SQUARE, 12, ENVELOPE1, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}},
  {"BEL", 3, {{SINE, 0, ENVELOPE2, 64}, {SINE, 12, ENVELOPE2, 64}, {TRIANGLE, 19, ENVELOPE2, 64}, {SINE, 0, 0, 64}}},
  {"LED", 2, {{SAW, 0, ENVELOPE0, 64}, {RAMP, 0, ENVELOPE0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}},
  {"CRD", 4, {{TRIANGLE, 0, ENVELOPE0, 64}, {TRIANGLE, 4, ENVELOPE0, 64}, {TRIANGLE, 7, ENVELOPE0, 64}, {SINE, 12, ENVELOPE0, 64}}},
  {"CIP", 2, {{SQUARE, 0, ENVELOPE3, 64}, {SQUARE, 12, ENVELOPE3, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}},
  {"PRC", 2, {{NOISE, 12, ENVELOPE1, 64}, {SINE, -12, ENVELOPE1, 42}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}},
  {"DRM", 1, {{SINE, 0, ENVELOPE1, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}, {SINE, 0, 0, 64}}}
};

const LivePreset kLivePresets[kLivePresetCount] PROGMEM = {
  {"PURE", SINE, ENVELOPE3, 116, 64, 1}, {"SOFT", SINE, ENVELOPE0, 102, 64, 1},
  {"KEYS", TRIANGLE, ENVELOPE2, 82, 64, 1}, {"PLUK", SQUARE, ENVELOPE1, 46, 64, 1},
  {"BASS", SAW, ENVELOPE0, 92, 58, 1}, {"LEAD", SAW, ENVELOPE3, 116, 68, 1},
  {"ORGN", SQUARE, ENVELOPE3, 116, 64, 1}, {"LOFI", RAMP, ENVELOPE2, 88, 60, 3}
};

const char kDrumLabels[kDrumCount][5] PROGMEM = {"KICK", "SNAR", "CHH"};

const int8_t kScaleOffsets[kScaleCount][kStepCount] PROGMEM = {
  {0, 2, 4, 5, 7, 9, 11, 12}, {0, 2, 3, 5, 7, 8, 10, 12},
  {0, 2, 4, 7, 9, 12, 14, 16}, {0, 3, 5, 6, 7, 10, 12, 15}
};
const uint8_t kScaleLengths[kScaleCount] PROGMEM = {7, 7, 5, 6};
const char kDroneWaveLabels[kDroneWaveCount][4] PROGMEM = {"SIN", "TRI", "SQR", "SAW"};
const char kRootLabels[12][3] PROGMEM = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char kScaleLabels[kScaleCount][4] PROGMEM = {"MAJ", "MIN", "PEN", "BLU"};
const char kStyleLabels[kPlayStyleCount][5] PROGMEM = {"POLY", "CHRD", "UP", "DOWN", "UPDN"};
const char kPlayFilterLabels[kPlayFilterLevelCount][5] PROGMEM = {
  "OFF", "6K6", "4K4", "2K2", "916", "425", "205", "101"
};

synth edgar;
InstrumentMode instrumentMode = MODE_SEQ;
SeqControls seqControls = {PRESET_SUB, 126, 64, 64};
PlayControls playControls = {0, 0, SCALE_MAJOR, STYLE_POLY, 0};
uint8_t stepNotes[kStepCount] = {36, 38, 40, 41, 43, 45, 47, 48};
uint8_t stepDrums[kStepCount] = {DRUM_KICK, DRUM_CLOSED_HAT, DRUM_SNARE, DRUM_CLOSED_HAT,
                                 DRUM_KICK, DRUM_CLOSED_HAT, DRUM_SNARE, DRUM_CLOSED_HAT};
uint8_t seqGateMask = 0xFF;
uint8_t droneGateMask = 0;
uint8_t dronePairWave[kSettingCount] = {SINE, TRIANGLE, SQUARE, SAW};
uint16_t droneLfoRateTenths[kSettingCount] = {0, 0, 0, 0};
uint16_t droneLfoShiftRaw[kSettingCount] = {0, 0, 0, 0};
bool droneWaveLocked[kSettingCount] = {false, false, false, false};
uint16_t dronePitchWord[kStepCount] = {0};
uint8_t playMorphWave = kPlayMorphOff;
uint16_t playMorphLfoRateTenths = 0;
uint16_t playMorphShiftRaw[2] = {0, 0};
bool playMorphControlLocked[2] = {false, false};
uint8_t playFilterLevel = 0;
uint16_t playFilterShiftRaw = 0;
bool playFilterControlLocked = false;
uint16_t playFilterLfoRateTenths = 0;
uint16_t playFilterLfoShiftRaw = 0;
bool playFilterLfoControlLocked = false;

uint8_t currentStep = 0;
uint8_t playingStep = 255;
uint8_t renderedStep = 255;
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
int8_t liveButtonForVoice[3] = {-1, -1, -1};
uint16_t liveVoiceAge[3] = {0, 0, 0};
uint8_t arpPosition = 0;
int8_t arpDirection = 1;

uint8_t stepButtonStable[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
uint8_t stepButtonReading[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long stepButtonChangedAt[kStepCount] = {0};
uint8_t modeButtonStable = HIGH;
uint8_t modeButtonReading = HIGH;
unsigned long modeButtonChangedAt = 0;
unsigned long modeButtonPressedAt = 0;
bool modeButtonLongHandled = false;
bool modeButtonChordUsed = false;
bool modeChordStepHeld = false;
bool playSequencerEnabled = true;

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
bool renderedEditVisible = false;
bool renderedInlineEditVisible = false;
char editTitle[10] = "";
char editValue[10] = "";
unsigned long editUntilMs = 0;
uint8_t editedStep = 255;
uint8_t editedDrone = 255;
uint16_t editedDroneHz = 0;
uint8_t editedDroneLfo = 255;
uint16_t editedDroneLfoRateTenths = 0;
uint8_t editedPlayShift = 0;
unsigned long inlineEditUntilMs = 0;
char displayFrame[2][17];
char renderedFrame[2][16];
bool renderedFrameReady = false;

void copyProgmemLabel(char *destination, const char *source, uint8_t size) {
  strncpy_P(destination, source, size - 1);
  destination[size - 1] = '\0';
}

void readSeqPreset(uint8_t index, SeqPreset &preset) { memcpy_P(&preset, &kSeqPresets[index], sizeof(SeqPreset)); }
void readLivePreset(uint8_t index, LivePreset &preset) { memcpy_P(&preset, &kLivePresets[index], sizeof(LivePreset)); }

uint16_t pitchWordForFrequency(uint16_t frequency) {
  return ((uint32_t)frequency * 65535UL + FS / 2) / FS;
}

uint16_t lfoRateForRaw(uint16_t raw) {
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;
  const uint16_t travel = 1023 - kAnalogEndpointMargin;
  const uint16_t position = 1023 - raw - kAnalogEndpointMargin;
  return kLfoFrequencyMinTenths +
    (position * (kLfoFrequencyMaxTenths - kLfoFrequencyMinTenths)) / travel;
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

  const uint16_t lowPitch = pitchWordForFrequency(kDroneFrequencyMin);
  const uint16_t highPitch = pitchWordForFrequency(kDroneFrequencyMax);
  for (uint8_t i = 0; i < kStepCount; ++i) {
    dronePitchWord[i] = lowPitch + ((highPitch - lowPitch) * i) / (kStepCount - 1);
  }

  lcd.begin(16, 2);
  lcd.clear(); lcd.print(F("BitSynth 3-mode")); delay(500); lcd.clear();
  nextStepIntervalMs = 15000U / seqControls.bpm;
  lastStepMs = millis();
}

void loop() {
  unsigned long now = millis();
  runSequencer(now);
  updateGlide(now);
  scanModeButton(now);
  scanStepButtons(now);
  readControls(now);
  now = millis();
  runSequencer(now);
  updateGlide(now);
  renderDisplay(now);
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(kMuxBit0Pin, bitRead(channel, 0));
  digitalWrite(kMuxBit1Pin, bitRead(channel, 1));
  digitalWrite(kMuxBit2Pin, bitRead(channel, 2));
  delayMicroseconds(kMuxSettleUs);
}

uint16_t readMedianAnalog(uint8_t pin) {
  uint16_t samples[kAnalogSamples];
  for (uint8_t i = 0; i < kAnalogDiscardSamples; ++i) analogRead(pin);
  for (uint8_t i = 0; i < kAnalogSamples; ++i) {
    const uint16_t value = analogRead(pin);
    uint8_t at = i;
    while (at > 0 && samples[at - 1] > value) { samples[at] = samples[at - 1]; --at; }
    samples[at] = value;
  }
  return samples[kAnalogSamples / 2];
}

uint16_t filterAnalog(uint16_t raw, uint16_t &filtered, bool &ready) {
  const uint16_t fixed = raw << kAnalogFilterFractionBits;
  if (!ready) { filtered = fixed; ready = true; return raw; }
  const int16_t delta = (int16_t)fixed - (int16_t)filtered;
  filtered += (delta * kAnalogFilterAlphaNumerator) / kAnalogFilterAlphaDenominator;
  return (filtered + (1 << (kAnalogFilterFractionBits - 1))) >> kAnalogFilterFractionBits;
}

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

uint8_t mapIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count) {
  if (count <= 1) return 0;
  if (raw <= kAnalogEndpointMargin) return count - 1;
  if (raw >= 1023 - kAnalogEndpointMargin) return 0;

  const uint8_t forwardBin = min((uint8_t)((raw * count) >> 10), (uint8_t)(count - 1));
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

uint8_t playMorphWaveForRaw(uint16_t raw) {
  const uint16_t offBoundary = 1023 - kAnalogEndpointMargin;
  if (playMorphWave == kPlayMorphOff) {
    if (raw + kAnalogHysteresis >= offBoundary) return kPlayMorphOff;
  } else if (raw >= offBoundary) {
    return kPlayMorphOff;
  }

  const uint16_t scaled = ((uint32_t)min(raw, offBoundary) * 1023UL) / offBoundary;
  const uint8_t current = playMorphWave == kPlayMorphOff ? 0 : playMorphWave;
  return mapIndexHysteresis(scaled, current, kPlayMorphWaveCount);
}

void setEditText(const __FlashStringHelper *title, const __FlashStringHelper *value, unsigned long now) {
  copyProgmemLabel(editTitle, (const char *)title, sizeof(editTitle));
  copyProgmemLabel(editValue, (const char *)value, sizeof(editValue));
  editUntilMs = now + kEditViewMs; displayDirty = true;
}

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
    if (instrumentMode == MODE_SEQ) readSequencerControls(settingValues, now);
    else if (instrumentMode == MODE_PLAY) readPlayControls(settingValues, now);
    else readDroneControls(settingValues, now);
  }

  if (instrumentMode == MODE_PLAY) {
    readPlayOctavePot(now);
    return;
  }

  for (uint8_t i = 0; i < kStepPotsPerLoop; ++i) {
    const uint8_t step = stepScanChannel;
    if (instrumentMode == MODE_SEQ) readSequencerStepPot(step, now);
    else readDronePitchPot(step, now);
    stepScanChannel = (stepScanChannel + 1) % kStepCount;
  }
}

void readSequencerControls(const uint16_t values[], unsigned long now) {
  (void)now;
  uint8_t next = mapIndexHysteresis(values[0], seqControls.preset, kSeqPresetCount);
  if (next != seqControls.preset) {
    if (seqControls.preset == PRESET_DRUM || next == PRESET_DRUM) {
      edgar.stopDrum();
      stopNormalVoices();
    }
    seqControls.preset = next;
    displayDirty = true;
  }
  next = mapByteHysteresis(values[1], seqControls.bpm, 60, 220);
  if (next != seqControls.bpm) { seqControls.bpm = next; displayDirty = true; }
  next = mapByteHysteresis(values[2], seqControls.color, 127, 0);
  if (next != seqControls.color) { seqControls.color = next; displayDirty = true; }
  next = mapByteHysteresis(values[3], seqControls.shape, 127, 0);
  if (next != seqControls.shape) { seqControls.shape = next; displayDirty = true; }
}

void revoiceHeldButtons();

void readPlayControls(const uint16_t values[], unsigned long now) {
  if (modeButtonStable == LOW) {
    if (values[1] != playMorphShiftRaw[1]) {
      playMorphShiftRaw[1] = values[1];
      playMorphControlLocked[1] = true;
      modeButtonChordUsed = true;
      const uint8_t nextWave = playMorphWaveForRaw(values[1]);
      if (nextWave != playMorphWave) {
        playMorphWave = nextWave;
        applyLiveConfiguration();
        editedPlayShift = 1;
        editedStep = 255;
        editedDrone = 255;
        editedDroneLfo = 255;
        inlineEditUntilMs = now + kEditViewMs;
        displayDirty = true;
      }
    }

    if (values[0] != playMorphShiftRaw[0]) {
      playMorphShiftRaw[0] = values[0];
      playMorphControlLocked[0] = true;
      modeButtonChordUsed = true;
      if (playMorphWave == kPlayMorphOff) {
        editedPlayShift = 1;
        inlineEditUntilMs = now + kEditViewMs;
        displayDirty = true;
      } else {
        const uint16_t nextRate = lfoRateForRaw(values[0]);
        if (nextRate != playMorphLfoRateTenths) {
          playMorphLfoRateTenths = nextRate;
          applyLiveConfiguration();
          editedPlayShift = 2;
          editedStep = 255;
          editedDrone = 255;
          editedDroneLfo = 255;
          inlineEditUntilMs = now + kEditViewMs;
          displayDirty = true;
        }
      }
    }

    if (values[2] != playFilterShiftRaw) {
      playFilterShiftRaw = values[2];
      playFilterControlLocked = true;
      modeButtonChordUsed = true;
      const uint8_t nextLevel = mapIndexHysteresis(
        values[2], playFilterLevel, kPlayFilterLevelCount);
      if (nextLevel != playFilterLevel) {
        playFilterLevel = nextLevel;
        edgar.configurePlayFilter(playFilterLevel, playFilterLfoRateTenths);
        editedPlayShift = 3;
        editedStep = 255;
        editedDrone = 255;
        editedDroneLfo = 255;
        inlineEditUntilMs = now + kEditViewMs;
        displayDirty = true;
      }
    }

    if (values[3] != playFilterLfoShiftRaw) {
      playFilterLfoShiftRaw = values[3];
      playFilterLfoControlLocked = true;
      modeButtonChordUsed = true;
      if (playFilterLevel == 0) {
        editedPlayShift = 3;
        inlineEditUntilMs = now + kEditViewMs;
        displayDirty = true;
      } else {
        const uint16_t nextRate = lfoRateForRaw(values[3]);
        if (nextRate != playFilterLfoRateTenths) {
          playFilterLfoRateTenths = nextRate;
          edgar.configurePlayFilter(playFilterLevel, playFilterLfoRateTenths);
          editedPlayShift = 4;
          editedStep = 255;
          editedDrone = 255;
          editedDroneLfo = 255;
          inlineEditUntilMs = now + kEditViewMs;
          displayDirty = true;
        }
      }
    }
    return;
  }

  if (playMorphControlLocked[0]) {
    if (abs((int16_t)values[0] - (int16_t)playMorphShiftRaw[0]) >= kSettingMovementThreshold) {
      playMorphControlLocked[0] = false;
    }
  }
  if (!playMorphControlLocked[0]) {
    const uint8_t next = mapIndexHysteresis(values[0], playControls.preset, kLivePresetCount);
    if (next != playControls.preset) {
      playControls.preset = next; applyLiveConfiguration(); revoiceHeldButtons();
      displayDirty = true;
    }
  }

  if (playMorphControlLocked[1]) {
    if (abs((int16_t)values[1] - (int16_t)playMorphShiftRaw[1]) >= kSettingMovementThreshold) {
      playMorphControlLocked[1] = false;
    }
  }
  if (!playMorphControlLocked[1]) {
    const uint8_t next = mapIndexHysteresis(values[1], playControls.root, 12);
    if (next != playControls.root) {
      playControls.root = next; revoiceHeldButtons(); displayDirty = true;
    }
  }

  if (playFilterControlLocked) {
    if (abs((int16_t)values[2] - (int16_t)playFilterShiftRaw) >= kSettingMovementThreshold) {
      playFilterControlLocked = false;
    }
  }
  uint8_t next;
  if (!playFilterControlLocked) {
    next = mapIndexHysteresis(values[2], playControls.scale, kScaleCount);
    if (next != playControls.scale) {
      playControls.scale = next; revoiceHeldButtons(); displayDirty = true;
    }
  }
  if (playFilterLfoControlLocked) {
    if (abs((int16_t)values[3] - (int16_t)playFilterLfoShiftRaw) >= kSettingMovementThreshold) {
      playFilterLfoControlLocked = false;
    }
  }
  if (!playFilterLfoControlLocked) {
    next = mapIndexHysteresis(values[3], playControls.style, kPlayStyleCount);
    if (next != playControls.style) {
      playControls.style = next; arpPosition = 0; arpDirection = 1; revoiceHeldButtons();
      displayDirty = true;
    }
  }
}

void readDroneControls(const uint16_t values[], unsigned long now) {
  if (modeButtonStable == LOW) {
    for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
      if (values[pair] == droneLfoShiftRaw[pair]) continue;
      droneLfoShiftRaw[pair] = values[pair];
      droneWaveLocked[pair] = true;
      modeButtonChordUsed = true;
      const uint16_t rate = lfoRateForRaw(values[pair]);
      if (rate == droneLfoRateTenths[pair]) continue;
      droneLfoRateTenths[pair] = rate;
      edgar.setDroneLfoRate(pair, rate);
      editedDroneLfo = pair;
      editedDroneLfoRateTenths = rate;
      editedDrone = 255;
      editedStep = 255;
      inlineEditUntilMs = now + kEditViewMs;
      displayDirty = true;
    }
    return;
  }

  for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
    if (droneWaveLocked[pair]) {
      if (abs((int16_t)values[pair] - (int16_t)droneLfoShiftRaw[pair]) < kSettingMovementThreshold) continue;
      droneWaveLocked[pair] = false;
    }
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
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[step], kStepMovementThreshold);
  const bool drumMode = seqControls.preset == PRESET_DRUM;
  const uint8_t current = drumMode ? stepDrums[step] : stepNotes[step];
  const uint8_t next = drumMode
    ? mapIndexHysteresis(raw, current, kDrumCount)
    : mapByteHysteresis(raw, current, kStepNoteMax, kStepNoteMin);
  if (next != current) {
    if (drumMode) stepDrums[step] = next;
    else stepNotes[step] = next;
    editedStep = step;
    editedDrone = 255;
    inlineEditUntilMs = now + kEditViewMs;
    displayDirty = true;
  }
}

void readPlayOctavePot(unsigned long now) {
  (void)now;
  selectMuxChannel(0);
  const uint16_t filtered = filterAnalog(readMedianAnalog(kStepAnalogPin), filteredStepRaw[0], stepFilterReady[0]);
  const uint16_t raw = acceptAnalogMovement(filtered, stepMotion[0], kStepMovementThreshold);
  const uint8_t next = mapIndexHysteresis(raw, playControls.octave, 4);
  if (next != playControls.octave) {
    playControls.octave = next; revoiceHeldButtons(); displayDirty = true;
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

void scanModeButton(unsigned long now) {
  selectMuxChannel(0);
  const uint8_t reading = digitalRead(kModeButtonPin);
  if (reading != modeButtonReading) { modeButtonReading = reading; modeButtonChangedAt = now; }
  if ((unsigned long)(now - modeButtonChangedAt) >= kButtonDebounceMs && reading != modeButtonStable) {
    modeButtonStable = reading;
    if (reading == LOW) {
      modeButtonPressedAt = now;
      modeButtonLongHandled = false;
      modeButtonChordUsed = false;
      if (instrumentMode == MODE_PLAY) {
        playMorphShiftRaw[0] = settingValues[0];
        playMorphShiftRaw[1] = settingValues[1];
        playFilterShiftRaw = settingValues[2];
        playFilterLfoShiftRaw = settingValues[3];
      } else if (instrumentMode == MODE_DRONE) {
        for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
          droneLfoShiftRaw[pair] = settingValues[pair];
        }
      }
    } else if (!modeButtonLongHandled && !modeButtonChordUsed) {
      changeMode((InstrumentMode)((instrumentMode + 1) % 3), now);
    }
  }
  if (modeButtonStable == LOW && !modeButtonLongHandled && !modeButtonChordUsed &&
      (unsigned long)(now - modeButtonPressedAt) >= kPanicHoldMs) {
    panicAll(); modeButtonLongHandled = true; setEditText(F("PANIC"), F("ALL OFF"), now);
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

void handleStepButton(uint8_t button, bool pressed) {
  if (button == 0 && modeChordStepHeld) {
    if (!pressed) modeChordStepHeld = false;
    return;
  }
  if (instrumentMode == MODE_PLAY && button == 0 && pressed && modeButtonStable == LOW) {
    playSequencerEnabled = !playSequencerEnabled;
    modeButtonChordUsed = true;
    modeChordStepHeld = true;
    if (!playSequencerEnabled) { stopNormalVoice(0); edgar.stopDrum(); }
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

void changeMode(InstrumentMode nextMode, unsigned long now) {
  if (nextMode == instrumentMode) return;
  const InstrumentMode previous = instrumentMode;
  if (previous == MODE_DRONE) { edgar.stopAllDrones(); edgar.setDroneMode(false); }
  if (previous == MODE_PLAY) {
    clearHeldNotes();
    edgar.disablePlayMorph();
    edgar.disablePlayFilter();
  }
  edgar.stopDrum();
  stopNormalVoices();
  instrumentMode = nextMode;

  if (nextMode == MODE_DRONE) {
    edgar.setSampleHold(1);
    for (uint8_t pair = 0; pair < kSettingCount; ++pair) {
      edgar.setDroneWave(pair * 2, dronePairWave[pair]);
      edgar.setDroneWave(pair * 2 + 1, dronePairWave[pair]);
      edgar.setDroneLfoRate(pair, droneLfoRateTenths[pair]);
    }
    for (uint8_t voice = 0; voice < kStepCount; ++voice) {
      droneRawReady[voice] = false;
      edgar.setDronePitchWord(voice, dronePitchWord[voice]);
      edgar.setDroneGate(voice, bitRead(droneGateMask, voice));
    }
    edgar.setDroneMode(true); setEditText(F("MODE"), F("DRONE"), now);
  } else if (nextMode == MODE_PLAY) {
    applyLiveConfiguration(); setEditText(F("MODE"), F("PLAY"), now);
  } else {
    if (previous == MODE_DRONE) {
      currentStep = 0;
      playingStep = 0;
      if (bitRead(seqGateMask, 0)) triggerSequencerStep(0, false);
      currentStep = 1;
      lastStepMs = now;
      nextStepIntervalMs = bpmStepMs();
    }
    edgar.setSampleHold(seqControls.preset == PRESET_CHIP ? map(seqControls.color, 0, 127, 1, 8) : 1);
    setEditText(F("MODE"), F("SEQ"), now);
  }
  displayDirty = true;
}

void panicAll() {
  edgar.stopDrum(); stopNormalVoices(); heldButtonMask = 0; resetLiveAssignments();
  if (instrumentMode == MODE_DRONE) { droneGateMask = 0; edgar.stopAllDrones(); }
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

uint16_t detunePitch(uint16_t pitch, int16_t permille) {
  int32_t result = pitch + ((int32_t)pitch * permille) / 1000;
  return constrain(result, 1, 65535);
}

void triggerNormalVoice(uint8_t voice, uint16_t pitch, uint8_t glide) {
  normalTargetPitch[voice] = pitch; normalGlide[voice] = glide;
  if (glide == 0 || normalCurrentPitch[voice] == 0) {
    normalCurrentPitch[voice] = pitch; edgar.setPitchWord(voice, pitch);
  }
  edgar.trigger(voice);
}

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

uint8_t seqShapeLength(uint8_t preset, uint8_t shape) {
  switch (preset) {
    case PRESET_BASS: return map(shape, 0, 127, 12, 92);
    case PRESET_PLUCK: return map(shape, 0, 127, 4, 76);
    case PRESET_BELL: return map(shape, 0, 127, 28, 112);
    case PRESET_CHORD: return map(shape, 0, 127, 20, 108);
    case PRESET_CHIP: return map(shape, 0, 127, 8, 100);
    case PRESET_PERC: return map(shape, 0, 127, 2, 62);
    case PRESET_SUB: return map(shape, 0, 127, 24, 110);
    default: return 102;
  }
}

int8_t chordIntervalForVoice(uint8_t voice, uint8_t color) {
  static const int8_t intervals[4][4] PROGMEM = {{0, 3, 7, 12}, {0, 4, 7, 12}, {0, 2, 7, 12}, {0, 5, 7, 12}};
  return (int8_t)pgm_read_byte(&intervals[min((uint8_t)(color >> 5), (uint8_t)3)][voice]);
}

uint8_t effectiveSampleHold() {
  uint8_t hold = seqControls.preset == PRESET_CHIP ? map(seqControls.color, 0, 127, 1, 8) : 1;
  if (instrumentMode == MODE_PLAY) { LivePreset live; readLivePreset(playControls.preset, live); hold = max(hold, live.sampleHold); }
  return hold;
}

void triggerSequencerNote(uint8_t note, bool backgroundOnly) {
  edgar.stopDrum();
  SeqPreset preset; readSeqPreset(seqControls.preset, preset); edgar.setSampleHold(effectiveSampleHold());
  const uint8_t voiceCount = backgroundOnly ? 1 : preset.voiceCount;
  const uint8_t length = seqShapeLength(seqControls.preset, seqControls.shape);

  for (uint8_t voice = 0; voice < voiceCount; ++voice) {
    VoiceRecipe recipe = preset.voices[voice];
    int8_t semitone = recipe.semitone;
    int16_t fine = 0;
    uint8_t modulation = recipe.modulation;
    uint8_t glide = 0;
    if (seqControls.preset == PRESET_SUB && voice == 1) fine = map(seqControls.color, 0, 127, 0, 8);
    else if (seqControls.preset == PRESET_BASS) modulation = map(seqControls.color, 0, 127, 64, 36);
    else if (seqControls.preset == PRESET_PLUCK && voice == 1) fine = map(seqControls.color, 0, 127, 0, 12);
    else if (seqControls.preset == PRESET_BELL && voice == 2) fine = map(seqControls.color, 0, 127, -15, 40);
    else if (seqControls.preset == PRESET_LEAD) {
      const int16_t spread = map(seqControls.color, 0, 127, 0, 18);
      fine = voice == 0 ? -(spread / 2) : spread; glide = seqControls.shape;
    } else if (seqControls.preset == PRESET_CHORD) semitone = chordIntervalForVoice(voice, seqControls.color);
    else if (seqControls.preset == PRESET_PERC) {
      if (voice == 0) semitone = map(seqControls.color, 0, 127, -12, 24);
      else modulation = map(seqControls.color, 0, 127, 30, 58);
    }
    edgar.setWave(voice, recipe.wave); edgar.setEnvelope(voice, recipe.envelope);
    edgar.setLength(voice, length); edgar.setMod(voice, modulation);
    triggerNormalVoice(voice, detunePitch(pitchForNote((int16_t)note + semitone), fine), glide);
  }
  if (!backgroundOnly) for (uint8_t voice = voiceCount; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
}

void triggerDrum(uint8_t drum, bool backgroundOnly) {
  if (backgroundOnly) stopNormalVoice(0);
  else stopNormalVoices();
  edgar.setSampleHold(effectiveSampleHold());
  normalCurrentPitch[0] = 0;
  normalTargetPitch[0] = 0;
  normalGlide[0] = 0;
  edgar.triggerDrum(min(drum, (uint8_t)(kDrumCount - 1)), seqControls.color, seqControls.shape);
}

void triggerSequencerStep(uint8_t step, bool backgroundOnly) {
  if (seqControls.preset == PRESET_DRUM) triggerDrum(stepDrums[step], backgroundOnly);
  else triggerSequencerNote(stepNotes[step], backgroundOnly);
}

uint16_t bpmStepMs() { return 15000U / max((uint8_t)1, seqControls.bpm); }

void runSequencer(unsigned long now) {
  if (instrumentMode == MODE_DRONE || (unsigned long)(now - lastStepMs) < nextStepIntervalMs) return;
  lastStepMs += nextStepIntervalMs;
  nextStepIntervalMs = bpmStepMs();
  if ((unsigned long)(now - lastStepMs) >= nextStepIntervalMs) lastStepMs = now;
  playingStep = currentStep;
  if (instrumentMode == MODE_PLAY) {
    if (playSequencerEnabled && bitRead(seqGateMask, currentStep)) triggerSequencerStep(currentStep, true);
    else { stopNormalVoice(0); edgar.stopDrum(); }
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
  LivePreset preset; readLivePreset(playControls.preset, preset);
  const bool secondaryEnabled = playMorphWave != kPlayMorphOff;
  const uint8_t secondaryWave = secondaryEnabled ? playMorphWave : SINE;
  edgar.configurePlayMorph(
    preset.wave, secondaryWave, playMorphLfoRateTenths, secondaryEnabled);
  edgar.configurePlayFilter(playFilterLevel, playFilterLfoRateTenths);
  for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) {
    edgar.setEnvelope(voice, preset.envelope);
    edgar.setLength(voice, preset.length); edgar.setMod(voice, preset.modulation);
  }
  edgar.setSampleHold(effectiveSampleHold());
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
  for (uint8_t slot = 0; slot < 3; ++slot) { liveButtonForVoice[slot] = -1; liveVoiceAge[slot] = 0; }
}

void clearHeldNotes() {
  heldButtonMask = 0;
  for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
  resetLiveAssignments();
}

void triggerPolyButton(uint8_t button) {
  int8_t slot = -1;
  for (uint8_t i = 0; i < 3; ++i) if (liveButtonForVoice[i] < 0) { slot = i; break; }
  if (slot < 0) {
    slot = 0;
    for (uint8_t i = 1; i < 3; ++i) if (liveVoiceAge[i] < liveVoiceAge[slot]) slot = i;
    const int8_t stolenButton = liveButtonForVoice[slot];
    if (stolenButton >= 0) liveVoiceForButton[stolenButton] = -1;
  }
  const uint8_t voice = slot + 1;
  liveButtonForVoice[slot] = button; liveVoiceForButton[button] = voice; liveVoiceAge[slot] = buttonPressOrder[button];
  triggerNormalVoice(voice, pitchForNote(liveNoteForButton(button)), 0);
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
    triggerNormalVoice(chordVoice + 1, pitchForNote(root + offset), 0);
  }
}

uint8_t collectHeldButtons(uint8_t buttons[kStepCount]) {
  uint8_t count = 0;
  for (uint8_t button = 0; button < kStepCount; ++button) if (bitRead(heldButtonMask, button)) buttons[count++] = button;
  return count;
}

void advanceArpeggio() {
  uint8_t buttons[kStepCount];
  const uint8_t count = collectHeldButtons(buttons);
  stopNormalVoice(2); stopNormalVoice(3);
  if (count == 0) { stopNormalVoice(1); arpPosition = 0; arpDirection = 1; return; }

  if (playControls.style == STYLE_ARP_DOWN) {
    arpPosition %= count;
    triggerNormalVoice(1, pitchForNote(liveNoteForButton(buttons[count - 1 - arpPosition])), 0);
    arpPosition = (arpPosition + 1) % count;
    return;
  }
  if (playControls.style == STYLE_ARP_UPDOWN && count > 1) {
    if (arpPosition >= count) arpPosition = count - 1;
    triggerNormalVoice(1, pitchForNote(liveNoteForButton(buttons[arpPosition])), 0);
    if (arpDirection > 0 && arpPosition >= count - 1) arpDirection = -1;
    else if (arpDirection < 0 && arpPosition == 0) arpDirection = 1;
    arpPosition += arpDirection;
    return;
  }
  arpPosition %= count;
  triggerNormalVoice(1, pitchForNote(liveNoteForButton(buttons[arpPosition])), 0);
  arpPosition = (arpPosition + 1) % count;
}

void handleLivePress(uint8_t button) {
  applyLiveConfiguration();
  if (playControls.style == STYLE_POLY) triggerPolyButton(button);
  else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  else if ((heldButtonMask & ~(1 << button)) == 0) { arpPosition = 0; arpDirection = 1; advanceArpeggio(); }
}

void handleLiveRelease(uint8_t button) {
  if (playControls.style == STYLE_POLY) {
    const int8_t voice = liveVoiceForButton[button];
    if (voice >= 1) {
      stopNormalVoice(voice); liveButtonForVoice[voice - 1] = -1; liveVoiceForButton[button] = -1;
    }
  } else if (playControls.style == STYLE_CHORD) triggerHeldChord();
  else advanceArpeggio();
}

void revoiceHeldButtons() {
  if (instrumentMode != MODE_PLAY) return;
  for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) stopNormalVoice(voice);
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

void formatNoteName(uint8_t note, char output[5]) {
  copyProgmemLabel(output, kRootLabels[note % 12], 3);
  const uint8_t length = strlen(output);
  output[length] = '0' + (note / 12 - 1); output[length + 1] = '\0';
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

void frameTenths(uint8_t row, uint8_t column, uint16_t value) {
  char text[6];
  utoa(value / 10, text, 10);
  const uint8_t length = strlen(text);
  text[length] = '.';
  text[length + 1] = '0' + value % 10;
  text[length + 2] = '\0';
  frameText(row, column, text);
}

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

void renderDisplay(unsigned long now) {
  const bool editVisible = (long)(editUntilMs - now) > 0;
  const bool inlineEditVisible = !editVisible && (long)(inlineEditUntilMs - now) > 0 &&
    ((instrumentMode == MODE_SEQ && editedStep < kStepCount) ||
      (instrumentMode == MODE_PLAY && editedPlayShift != 0) ||
     (instrumentMode == MODE_DRONE &&
      (editedDrone < kStepCount || editedDroneLfo < kSettingCount)));
  const bool displayedStepChanged = instrumentMode == MODE_SEQ && renderedStep != playingStep;
  if (!displayDirty && !displayedStepChanged && editVisible == renderedEditVisible &&
      inlineEditVisible == renderedInlineEditVisible) return;

  resetDisplayFrame();
  if (editVisible) {
    frameText(0, 0, editTitle); frameText(1, 0, editValue);
  } else if (instrumentMode == MODE_SEQ) renderSequencerDashboard(inlineEditVisible);
  else if (instrumentMode == MODE_PLAY) renderPlayDashboard(inlineEditVisible);
  else renderDroneDashboard(inlineEditVisible);
  flushDisplayFrame();

  renderedStep = playingStep;
  renderedEditVisible = editVisible;
  renderedInlineEditVisible = inlineEditVisible;
  displayDirty = false;
}

void renderSequencerDashboard(bool inlineEditVisible) {
  frameProgmemText(0, 0, kSeqPresets[seqControls.preset].label);
  frameChar(0, 4, 'B'); frameNumber(0, 5, seqControls.bpm);
  frameChar(0, 9, seqControls.preset == PRESET_DRUM ? 'T' : 'C');
  frameNumber(0, 10, map(seqControls.color, 0, 127, 0, 99));
  frameChar(0, 13, seqControls.preset == PRESET_DRUM ? 'D' : 'S');
  frameNumber(0, 14, map(seqControls.shape, 0, 127, 0, 99));
  for (uint8_t step = 0; step < kStepCount; ++step) {
    const bool enabled = bitRead(seqGateMask, step);
    if (step == playingStep) frameChar(1, step, enabled ? 'O' : 'o');
    else frameChar(1, step, enabled ? 'x' : '-');
  }
  if (inlineEditVisible) {
    frameChar(1, 9, 'S'); frameChar(1, 10, '1' + editedStep); frameChar(1, 11, ':');
    if (seqControls.preset == PRESET_DRUM) {
      frameProgmemText(1, 12, kDrumLabels[stepDrums[editedStep]]);
    } else {
      char note[5]; formatNoteName(stepNotes[editedStep], note); frameText(1, 12, note);
    }
  }
}

void renderPlayDashboard(bool inlineEditVisible) {
  frameText(0, 0, F("PLY "));
  frameProgmemText(0, 4, kLivePresets[playControls.preset].label);
  frameProgmemText(0, 9, kRootLabels[playControls.root]);
  frameProgmemText(0, 12, kScaleLabels[playControls.scale]);
  frameProgmemText(1, 0, kStyleLabels[playControls.style]);
  frameChar(1, 5, 'O'); frameChar(1, 6, '2' + playControls.octave);
  if (!inlineEditVisible) {
    frameText(1, 9, playSequencerEnabled ? F("SEQ+") : F("SEQ-"));
  } else if (editedPlayShift == 1) {
    frameText(1, 9, F("B:"));
    if (playMorphWave == kPlayMorphOff) frameText(1, 11, F("OFF"));
    else {
      frameProgmemText(1, 11, kDroneWaveLabels[playMorphWave]);
    }
  } else if (editedPlayShift == 2) {
    frameText(1, 9, F("L:"));
    if (playMorphLfoRateTenths == 0) frameText(1, 11, F("OFF"));
    else frameTenths(1, 11, playMorphLfoRateTenths);
  } else if (editedPlayShift == 3) {
    frameText(1, 9, F("F:"));
    frameProgmemText(1, 11, kPlayFilterLabels[playFilterLevel]);
  } else {
    frameText(1, 9, F("FL:"));
    if (playFilterLfoRateTenths == 0) frameText(1, 12, F("OFF"));
    else frameTenths(1, 12, playFilterLfoRateTenths);
  }
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
      if (editedDroneLfoRateTenths == 0) frameText(1, 12, F("OFF"));
      else frameTenths(1, 12, editedDroneLfoRateTenths);
    } else {
      frameChar(1, 9, 'D'); frameChar(1, 10, '1' + editedDrone); frameChar(1, 11, ':');
      frameNumber(1, 12, editedDroneHz);
    }
  }
}
