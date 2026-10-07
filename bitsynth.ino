#include <LiquidCrystal.h>
#include "synth.h"

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

const uint8_t kMuxBit0Pin = 8;
const uint8_t kMuxBit1Pin = 11;
const uint8_t kMuxBit2Pin = 10;

const uint8_t kSettingsAnalogPin = A0;
const uint8_t kStepButtonPin = A1;
const uint8_t kPitchAnalogPin = A2;
const uint8_t kExtraButtonPin = A3;

const uint8_t kMainVoice = 0;
const uint8_t kOsc2Voice = 1;
const uint8_t kChordVoiceA = 2;
const uint8_t kChordVoiceB = 3;
const uint8_t kSynthVoiceCount = 4;
const uint8_t kStepCount = 16;
const uint8_t kVisibleStepCount = 8;
const uint8_t kSettingsCount = 4;
const uint8_t kPatternCount = 4;
const uint8_t kWaveCount = 6;
const uint8_t kPlayModeCount = 3;
const uint8_t kEnvelopeCount = 4;
const uint8_t kOsc2EnvelopeCount = kEnvelopeCount + 1;
const uint8_t kScaleCount = 5;
const uint8_t kLaneCount = 5;
const uint8_t kPageCount = 7;
const uint8_t kAnalogSamples = 5;
const uint8_t kAnalogFilterFractionBits = 4;
const uint8_t kAnalogFilterAlphaNumerator = 1;
const uint8_t kAnalogFilterAlphaDenominator = 16;
const uint8_t kMuxSettleUs = 50;

const uint8_t kButtonDebounceMs = 25;
const uint16_t kDoublePressMs = 320;
const uint16_t kShiftHoldMs = 260;
const uint16_t kUtilityLongPressMs = 1000;
const uint16_t kEditViewMs = 1200;
const uint16_t kPickupHysteresis = 18;
const uint8_t kAnalogValueHysteresis = 12;
const uint8_t kGateHysteresis = 18;

const uint8_t kDefaultBpm = 126;
const uint8_t kDefaultProbability = 100;
const uint8_t kDefaultRatchet = 1;
const int8_t kDefaultVoicingLock = 0;
const uint8_t kDefaultLength = 64;
const uint8_t kDefaultModulation = 64;

const uint8_t kWaveIds[kWaveCount] = {SINE, TRIANGLE, SQUARE, SAW, RAMP, NOISE};

enum ControlPage : uint8_t {
  PAGE_OSC = 0,
  PAGE_OSC2 = 1,
  PAGE_ENV = 2,
  PAGE_SEQ = 3,
  PAGE_PAT = 4,
  PAGE_FX = 5,
  PAGE_UTL = 6
};

enum StepLane : uint8_t {
  LANE_NOTE = 0,
  LANE_GATE = 1,
  LANE_PROB = 2,
  LANE_RATCHET = 3,
  LANE_LOCK = 4
};

enum PlayMode : uint8_t {
  MODE_SCALE = 0,
  MODE_CHORD = 1,
  MODE_ARP = 2
};

enum ScaleMode : uint8_t {
  SCALE_CHROMATIC = 0,
  SCALE_MAJOR = 1,
  SCALE_MINOR = 2,
  SCALE_PENTATONIC = 3,
  SCALE_BLUES = 4
};

struct Pattern {
  uint8_t note[kStepCount];
  uint8_t probability[kStepCount];
  uint8_t ratchet[kStepCount];
  int8_t voicingLock[kStepCount];
  uint16_t gates;
};

struct OscControls {
  uint8_t waveform;
  uint8_t playMode;
  int8_t voicing;
  int8_t transpose;
};

struct Osc2Controls {
  uint8_t waveform;
  uint8_t envelope;
  uint8_t detune;
  int8_t transpose;
};

struct EnvControls {
  uint8_t envelope;
  uint8_t length;
  uint8_t modulation;
  uint8_t glide;
};

struct SeqControls {
  uint8_t bpm;
  uint8_t swing;
  uint8_t scale;
};

struct PatControls {
  uint8_t targetSlot;
  uint8_t randomAmount;
};

struct FxControls {
  uint8_t sampleHoldFrames;
};

synth edgar;

Pattern patterns[kPatternCount];
Pattern clipboardPattern;
bool clipboardHasPattern = false;

OscControls oscControls = {0, MODE_SCALE, 0, 0};
OscControls lastDisplayedOscControls = {255, 255, 127, 127};
Osc2Controls osc2Controls = {0, 0, 8, 0};
Osc2Controls lastDisplayedOsc2Controls = {255, 255, 255, 127};
EnvControls envControls = {ENVELOPE0, kDefaultLength, kDefaultModulation, 0};
EnvControls lastDisplayedEnvControls = {255, 255, 255, 255};
SeqControls seqControls = {kDefaultBpm, 0, SCALE_CHROMATIC};
SeqControls lastDisplayedSeqControls = {255, 255, 255};
PatControls patControls = {0, 50};
PatControls lastDisplayedPatControls = {255, 255};
FxControls fxControls = {1};
FxControls lastDisplayedFxControls = {255};

uint8_t activePattern = 0;
uint8_t lastDisplayedPattern = 255;
uint8_t activeLane = LANE_NOTE;
uint8_t lastDisplayedLane = 255;
uint8_t visibleRange = 0;
uint8_t lastDisplayedRange = 255;
uint8_t controlPage = PAGE_OSC;
uint8_t lastDisplayedPage = 255;
uint8_t utilityAction = 0;

uint8_t currentStep = 0;
uint8_t playingStep = 255;
uint8_t renderedStep = 255;
uint16_t nextStepIntervalMs = 119;
unsigned long lastStepMs = 0;
uint8_t ratchetStep = 255;
uint8_t remainingRatchets = 0;
uint16_t ratchetIntervalMs = 1;
unsigned long lastRatchetMs = 0;
uint8_t arpIndex = 0;

uint8_t configuredWaveform = 255;
uint8_t configuredOsc2Waveform = 255;
uint8_t configuredEnvelope = 255;
uint8_t configuredOsc2Envelope = 255;
uint8_t configuredLength = 255;
uint8_t configuredModulation = 255;
uint8_t configuredSampleHoldFrames = 255;

uint16_t currentPitchWord[kSynthVoiceCount] = {0, 0, 0, 0};
uint16_t targetPitchWord[kSynthVoiceCount] = {0, 0, 0, 0};

uint8_t stepButtonStableState[kVisibleStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
uint8_t stepButtonLastReading[kVisibleStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long stepButtonLastChangeMs[kVisibleStepCount] = {0};

uint8_t extraButtonStableState = HIGH;
uint8_t extraButtonLastReading = HIGH;
unsigned long extraButtonLastChangeMs = 0;
unsigned long extraButtonPressedAtMs = 0;
unsigned long pendingClickMs = 0;
bool pendingSingleClick = false;
bool extraButtonHeld = false;
bool extraButtonLongHandled = false;
bool shiftActionUsed = false;
bool lastDisplayedShift = false;

uint16_t lastLivePotRaw[kSettingsCount] = {0, 0, 0, 0};
uint16_t filteredSettingRaw[kSettingsCount] = {0, 0, 0, 0};
uint16_t filteredStepRaw[kVisibleStepCount] = {0, 0, 0, 0, 0, 0, 0, 0};
bool livePotCaptured[kSettingsCount] = {true, true, true, true};
bool settingFilterReady[kSettingsCount] = {false, false, false, false};
bool stepFilterReady[kVisibleStepCount] = {false, false, false, false, false, false, false, false};
char pickupHint[kSettingsCount] = {' ', ' ', ' ', ' '};

char lastEditLabel[4] = {'W', 'A', 'V', '\0'};
char lastEditValue[4] = {'S', 'I', 'N', '\0'};
unsigned long editViewUntilMs = 0;
bool displayDirty = true;

void setup() {
  initPatterns();
  clipboardPattern = patterns[0];

  edgar.begin(CHA);
  for (uint8_t voice = 0; voice < kSynthVoiceCount; ++voice) {
    edgar.setupVoice(voice, SINE, 0, ENVELOPE0, envControls.length, envControls.modulation);
    edgar.stopVoice(voice);
  }

  pinMode(kMuxBit0Pin, OUTPUT);
  pinMode(kMuxBit1Pin, OUTPUT);
  pinMode(kMuxBit2Pin, OUTPUT);
  pinMode(kSettingsAnalogPin, INPUT);
  pinMode(kStepButtonPin, INPUT_PULLUP);
  pinMode(kPitchAnalogPin, INPUT);
  pinMode(kExtraButtonPin, INPUT_PULLUP);

  randomSeed(analogRead(kPitchAnalogPin));

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print(F("BitSynth v2"));
  delay(600);
  lcd.clear();
}

void loop() {
  const unsigned long now = millis();

  readControls(now);
  scanExtraButton(now);
  scanStepButtons(now);
  applySynthControls();
  updateGlide();
  runSequencer(now);
  renderDisplay(now);
}

void initPatterns() {
  for (uint8_t slot = 0; slot < kPatternCount; ++slot) {
    patterns[slot].gates = 0xFFFF;
    for (uint8_t step = 0; step < kStepCount; ++step) {
      patterns[slot].note[step] = 48 + step;
      patterns[slot].probability[step] = kDefaultProbability;
      patterns[slot].ratchet[step] = kDefaultRatchet;
      patterns[slot].voicingLock[step] = kDefaultVoicingLock;
    }
  }
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(kMuxBit0Pin, bitRead(channel, 0));
  digitalWrite(kMuxBit1Pin, bitRead(channel, 1));
  digitalWrite(kMuxBit2Pin, bitRead(channel, 2));
  delayMicroseconds(kMuxSettleUs);
}

uint16_t readMedianAnalog(uint8_t pin) {
  uint16_t samples[kAnalogSamples];

  analogRead(pin);

  for (uint8_t i = 0; i < kAnalogSamples; ++i) {
    const uint16_t value = analogRead(pin);
    uint8_t insertAt = i;

    while (insertAt > 0 && samples[insertAt - 1] > value) {
      samples[insertAt] = samples[insertAt - 1];
      --insertAt;
    }

    samples[insertAt] = value;
  }

  return samples[kAnalogSamples / 2];
}

uint16_t filterAnalog(uint16_t raw, uint16_t &filteredRaw, bool &ready) {
  const uint16_t rawFixed = raw << kAnalogFilterFractionBits;

  if (!ready) {
    filteredRaw = rawFixed;
    ready = true;
    return raw;
  }

  const int32_t delta = (int32_t)rawFixed - filteredRaw;
  filteredRaw += (delta * kAnalogFilterAlphaNumerator) / kAnalogFilterAlphaDenominator;
  return (filteredRaw + (1 << (kAnalogFilterFractionBits - 1))) >> kAnalogFilterFractionBits;
}

uint8_t mapToByte(uint16_t value, int16_t outMin, int16_t outMax) {
  return (uint8_t)constrain(map(value, 0, 1023, outMin, outMax), min(outMin, outMax), max(outMin, outMax));
}

int8_t mapToSigned(uint16_t value, int8_t outMin, int8_t outMax) {
  return (int8_t)constrain(map(value, 0, 1023, outMin, outMax), min(outMin, outMax), max(outMin, outMax));
}

uint8_t mapToIndex(uint16_t value, uint8_t count) {
  uint8_t index = mapToByte(value, count - 1, 0);
  return index >= count ? count - 1 : index;
}

uint16_t rawForMappedValue(int16_t value, int16_t outMin, int16_t outMax) {
  if (outMin == outMax) {
    return 0;
  }
  return (uint16_t)constrain(map(value, outMin, outMax, 0, 1023), 0, 1023);
}

bool rawPastHysteresisBoundary(uint16_t raw, uint16_t currentRaw, uint16_t nextRaw) {
  const uint16_t boundary = (currentRaw + nextRaw) / 2;

  if (nextRaw > currentRaw) {
    return raw > boundary + kAnalogValueHysteresis;
  }

  return raw + kAnalogValueHysteresis < boundary;
}

int16_t mapWithHysteresis(uint16_t raw, int16_t current, int16_t outMin, int16_t outMax) {
  const int16_t lowest = min(outMin, outMax);
  const int16_t highest = max(outMin, outMax);
  const int16_t candidate = constrain(map(raw, 0, 1023, outMin, outMax), lowest, highest);

  if (candidate == current) {
    return current;
  }

  const int16_t next = current + (candidate > current ? 1 : -1);
  if (next < lowest || next > highest) {
    return candidate;
  }

  if (rawPastHysteresisBoundary(raw, rawForMappedValue(current, outMin, outMax), rawForMappedValue(next, outMin, outMax))) {
    return candidate;
  }

  return current;
}

uint8_t mapToByteHysteresis(uint16_t raw, uint8_t current, int16_t outMin, int16_t outMax) {
  return (uint8_t)mapWithHysteresis(raw, current, outMin, outMax);
}

int8_t mapToSignedHysteresis(uint16_t raw, int8_t current, int8_t outMin, int8_t outMax) {
  return (int8_t)mapWithHysteresis(raw, current, outMin, outMax);
}

uint8_t mapToIndexHysteresis(uint16_t raw, uint8_t current, uint8_t count) {
  uint8_t index = mapToByteHysteresis(raw, current, count - 1, 0);
  return index >= count ? count - 1 : index;
}

bool mapToGateHysteresis(uint16_t raw, bool current) {
  if (current) {
    return raw < 512 + kGateHysteresis;
  }

  return raw < 512 - kGateHysteresis;
}

uint16_t rawForIndex(uint8_t index, uint8_t count) {
  if (count <= 1) {
    return 0;
  }
  return (uint16_t)map(index, 0, count - 1, 1023, 0);
}

uint16_t rawForReverseByte(uint8_t value, uint8_t maxValue) {
  if (maxValue == 0) {
    return 0;
  }
  return (uint16_t)map(value, 0, maxValue, 1023, 0);
}

uint16_t rawForRange(uint16_t value, uint16_t inMin, uint16_t inMax) {
  if (inMax <= inMin) {
    return 0;
  }
  return (uint16_t)map(value, inMin, inMax, 0, 1023);
}

uint16_t rawForReverseRange(uint16_t value, uint16_t inMin, uint16_t inMax) {
  if (inMax <= inMin) {
    return 0;
  }
  return (uint16_t)map(value, inMin, inMax, 1023, 0);
}

bool crossedTarget(uint16_t previous, uint16_t current, uint16_t target) {
  return (previous <= target && current >= target) || (previous >= target && current <= target);
}

bool captureLivePot(uint8_t pot, uint16_t raw, uint16_t targetRaw) {
  if (livePotCaptured[pot]) {
    pickupHint[pot] = ' ';
    return true;
  }

  const int16_t distance = (int16_t)raw - (int16_t)targetRaw;
  if (abs(distance) <= kPickupHysteresis || crossedTarget(lastLivePotRaw[pot], raw, targetRaw)) {
    livePotCaptured[pot] = true;
    pickupHint[pot] = ' ';
    displayDirty = true;
    return true;
  }

  pickupHint[pot] = raw < targetRaw ? '>' : '<';
  displayDirty = true;
  return false;
}

void resetLivePotPickup() {
  for (uint8_t i = 0; i < kSettingsCount; ++i) {
    livePotCaptured[i] = false;
    pickupHint[i] = ' ';
  }
}

void showEditView(unsigned long now) {
  editViewUntilMs = now + kEditViewMs;
  displayDirty = true;
}

void setLastEditText(const char label[4], const char value[4], unsigned long now) {
  for (uint8_t i = 0; i < 4; ++i) {
    lastEditLabel[i] = label[i];
    lastEditValue[i] = value[i];
  }
  showEditView(now);
}

void setLastEditNumber(const char label[4], int16_t value, unsigned long now) {
  for (uint8_t i = 0; i < 4; ++i) {
    lastEditLabel[i] = label[i];
  }

  if (value < 0) {
    lastEditValue[0] = '-';
    value = -value;
  } else if (value > 99) {
    lastEditValue[0] = (char)('0' + ((value / 100) % 10));
  } else {
    lastEditValue[0] = value > 9 ? (char)('0' + ((value / 10) % 10)) : '0';
  }

  if (value > 99) {
    lastEditValue[1] = (char)('0' + ((value / 10) % 10));
    lastEditValue[2] = (char)('0' + (value % 10));
  } else if (lastEditValue[0] == '-') {
    lastEditValue[1] = value > 9 ? (char)('0' + ((value / 10) % 10)) : '0';
    lastEditValue[2] = (char)('0' + (value % 10));
  } else {
    lastEditValue[1] = (char)('0' + (value % 10));
    lastEditValue[2] = ' ';
  }

  lastEditValue[3] = '\0';
  showEditView(now);
}

void readControls(unsigned long now) {
  uint16_t settingValues[kSettingsCount];

  for (uint8_t channel = 0; channel < kSettingsCount; ++channel) {
    selectMuxChannel(channel);
    const uint16_t raw = readMedianAnalog(kSettingsAnalogPin);
    settingValues[channel] = filterAnalog(raw, filteredSettingRaw[channel], settingFilterReady[channel]);
  }

  readCurrentPage(settingValues, now);
  readStepPots(now);

  for (uint8_t channel = 0; channel < kSettingsCount; ++channel) {
    lastLivePotRaw[channel] = settingValues[channel];
  }
}

void readCurrentPage(const uint16_t values[], unsigned long now) {
  switch (controlPage) {
    case PAGE_OSC2:
      readOsc2Page(values, now);
      break;
    case PAGE_ENV:
      readEnvPage(values, now);
      break;
    case PAGE_SEQ:
      readSeqPage(values, now);
      break;
    case PAGE_PAT:
      readPatPage(values, now);
      break;
    case PAGE_FX:
      readFxPage(values, now);
      break;
    case PAGE_UTL:
      readUtilityPage(values, now);
      break;
    case PAGE_OSC:
    default:
      readOscPage(values, now);
      break;
  }
}

void readOscPage(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], rawForIndex(oscControls.waveform, kWaveCount))) {
    const uint8_t next = mapToIndexHysteresis(values[0], oscControls.waveform, kWaveCount);
    if (next != oscControls.waveform) {
      oscControls.waveform = next;
      setLastEditText("WAV", waveLabel(oscControls.waveform), now);
    }
  }

  if (captureLivePot(1, values[1], rawForIndex(oscControls.playMode, kPlayModeCount))) {
    const uint8_t next = mapToIndexHysteresis(values[1], oscControls.playMode, kPlayModeCount);
    if (next != oscControls.playMode) {
      oscControls.playMode = next;
      panicVoices();
      setLastEditText("MOD", playModeLabel(oscControls.playMode), now);
    }
  }

  if (captureLivePot(2, values[2], rawForReverseRange((uint8_t)(oscControls.voicing + 12), 0, 24))) {
    const int8_t next = mapToSignedHysteresis(values[2], oscControls.voicing, 12, -12);
    if (next != oscControls.voicing) {
      oscControls.voicing = next;
      setLastEditNumber("VOI", oscControls.voicing, now);
    }
  }

  if (captureLivePot(3, values[3], rawForReverseRange((uint8_t)(oscControls.transpose + 24), 0, 48))) {
    const int8_t next = mapToSignedHysteresis(values[3], oscControls.transpose, 24, -24);
    if (next != oscControls.transpose) {
      oscControls.transpose = next;
      setLastEditNumber("TRN", oscControls.transpose, now);
    }
  }
}

void readOsc2Page(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], rawForIndex(osc2Controls.waveform, kWaveCount))) {
    const uint8_t next = mapToIndexHysteresis(values[0], osc2Controls.waveform, kWaveCount);
    if (next != osc2Controls.waveform) {
      osc2Controls.waveform = next;
      setLastEditText("2WV", waveLabel(osc2Controls.waveform), now);
    }
  }

  if (captureLivePot(1, values[1], rawForIndex(osc2Controls.envelope, kOsc2EnvelopeCount))) {
    const uint8_t next = mapToIndexHysteresis(values[1], osc2Controls.envelope, kOsc2EnvelopeCount);
    if (next != osc2Controls.envelope) {
      osc2Controls.envelope = next;
      setLastEditText("2EN", osc2EnvelopeLabel(osc2Controls.envelope), now);
    }
  }

  if (captureLivePot(2, values[2], rawForReverseByte(osc2Controls.detune, 24))) {
    const uint8_t next = mapToByteHysteresis(values[2], osc2Controls.detune, 24, 0);
    if (next != osc2Controls.detune) {
      osc2Controls.detune = next;
      setLastEditNumber("2DT", osc2Controls.detune, now);
    }
  }

  if (captureLivePot(3, values[3], rawForReverseRange((uint8_t)(osc2Controls.transpose + 24), 0, 48))) {
    const int8_t next = mapToSignedHysteresis(values[3], osc2Controls.transpose, 24, -24);
    if (next != osc2Controls.transpose) {
      osc2Controls.transpose = next;
      setLastEditNumber("2TR", osc2Controls.transpose, now);
    }
  }
}

void readEnvPage(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], rawForIndex(envControls.envelope, kEnvelopeCount))) {
    const uint8_t next = mapToIndexHysteresis(values[0], envControls.envelope, kEnvelopeCount);
    if (next != envControls.envelope) {
      envControls.envelope = next;
      setLastEditNumber("ENV", envControls.envelope, now);
    }
  }

  if (captureLivePot(1, values[1], rawForReverseByte(envControls.length, 127))) {
    const uint8_t next = mapToByteHysteresis(values[1], envControls.length, 127, 0);
    if (next != envControls.length) {
      envControls.length = next;
      setLastEditNumber("LEN", envControls.length, now);
    }
  }

  if (captureLivePot(2, values[2], rawForReverseByte(envControls.modulation, 127))) {
    const uint8_t next = mapToByteHysteresis(values[2], envControls.modulation, 127, 0);
    if (next != envControls.modulation) {
      envControls.modulation = next;
      setLastEditNumber("MOD", envControls.modulation, now);
    }
  }

  if (captureLivePot(3, values[3], rawForReverseByte(envControls.glide, 127))) {
    const uint8_t next = mapToByteHysteresis(values[3], envControls.glide, 127, 0);
    if (next != envControls.glide) {
      envControls.glide = next;
      setLastEditNumber("GLD", envControls.glide, now);
    }
  }
}

void readSeqPage(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], rawForRange(seqControls.bpm, 40, 240))) {
    const uint8_t next = mapToByteHysteresis(values[0], seqControls.bpm, 40, 240);
    if (next != seqControls.bpm) {
      seqControls.bpm = next;
      setLastEditNumber("BPM", seqControls.bpm, now);
    }
  }

  if (captureLivePot(1, values[1], rawForReverseByte(seqControls.swing, 75))) {
    const uint8_t next = mapToByteHysteresis(values[1], seqControls.swing, 75, 0);
    if (next != seqControls.swing) {
      seqControls.swing = next;
      setLastEditNumber("SWG", seqControls.swing, now);
    }
  }

  if (captureLivePot(2, values[2], rawForIndex(seqControls.scale, kScaleCount))) {
    const uint8_t next = mapToIndexHysteresis(values[2], seqControls.scale, kScaleCount);
    if (next != seqControls.scale) {
      seqControls.scale = next;
      setLastEditText("SCL", scaleLabel(seqControls.scale), now);
    }
  }

  if (captureLivePot(3, values[3], rawForIndex(activeLane, kLaneCount))) {
    const uint8_t next = mapToIndexHysteresis(values[3], activeLane, kLaneCount);
    if (next != activeLane) {
      activeLane = next;
      setLastEditText("LAN", laneLabel(activeLane), now);
    }
  }
}

void readPatPage(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], rawForIndex(activePattern, kPatternCount))) {
    const uint8_t next = mapToIndexHysteresis(values[0], activePattern, kPatternCount);
    if (next != activePattern) {
      activePattern = next;
      patControls.targetSlot = next;
      resetLivePotPickup();
      setLastEditNumber("PAT", activePattern + 1, now);
    }
  }

  if (captureLivePot(1, values[1], rawForIndex(patControls.targetSlot, kPatternCount))) {
    const uint8_t next = mapToIndexHysteresis(values[1], patControls.targetSlot, kPatternCount);
    if (next != patControls.targetSlot) {
      patControls.targetSlot = next;
      setLastEditNumber("DST", patControls.targetSlot + 1, now);
    }
  }

  if (captureLivePot(2, values[2], rawForReverseByte(patControls.randomAmount, 100))) {
    const uint8_t next = mapToByteHysteresis(values[2], patControls.randomAmount, 100, 0);
    if (next != patControls.randomAmount) {
      patControls.randomAmount = next;
      setLastEditNumber("RND", patControls.randomAmount, now);
    }
  }

  if (captureLivePot(3, values[3], rawForIndex(visibleRange, 2))) {
    const uint8_t next = mapToIndexHysteresis(values[3], visibleRange, 2);
    if (next != visibleRange) {
      visibleRange = next;
      setLastEditText("RNG", visibleRange ? "916" : "1-8", now);
    }
  }
}

void readFxPage(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], rawForReverseRange(fxControls.sampleHoldFrames, 1, 16))) {
    const uint8_t next = mapToByteHysteresis(values[0], fxControls.sampleHoldFrames, 16, 1);
    if (next != fxControls.sampleHoldFrames) {
      fxControls.sampleHoldFrames = next;
      setLastEditNumber("HLD", fxControls.sampleHoldFrames, now);
    }
  }

  for (uint8_t pot = 1; pot < kSettingsCount; ++pot) {
    captureLivePot(pot, values[pot], 512);
  }
}

void readUtilityPage(const uint16_t values[], unsigned long now) {
  if (captureLivePot(0, values[0], 1023)) {
    const uint8_t next = mapToIndexHysteresis(values[0], utilityAction, 4);
    if (next != utilityAction) {
      utilityAction = next;
      if (utilityAction == 0) {
        setLastEditText("UTL", "PNC", now);
      } else if (utilityAction == 1) {
        setLastEditText("UTL", "INI", now);
      } else if (utilityAction == 2) {
        setLastEditText("UTL", "GAT", now);
      } else {
        setLastEditText("UTL", "VER", now);
      }
    }
  }

  for (uint8_t pot = 1; pot < kSettingsCount; ++pot) {
    captureLivePot(pot, values[pot], 512);
  }
}

void readStepPots(unsigned long now) {
  const uint8_t baseStep = visibleRange ? 8 : 0;

  for (uint8_t slot = 0; slot < kVisibleStepCount; ++slot) {
    const uint8_t step = baseStep + slot;
    selectMuxChannel(slot);
    const uint16_t raw = filterAnalog(readMedianAnalog(kPitchAnalogPin), filteredStepRaw[slot], stepFilterReady[slot]);

    if (activeLane == LANE_NOTE) {
      const uint8_t next = mapToByteHysteresis(raw, patterns[activePattern].note[step], 84, 12);
      if (next != patterns[activePattern].note[step]) {
        patterns[activePattern].note[step] = next;
        setLastEditNumber("NOT", quantizeNoteWithTranspose(next), now);
      }
    } else if (activeLane == LANE_GATE) {
      const bool enabled = mapToGateHysteresis(raw, stepGate(activePattern, step));
      if (enabled != stepGate(activePattern, step)) {
        setStepGate(activePattern, step, enabled);
        setLastEditText("GAT", enabled ? "ON " : "OFF", now);
      }
    } else if (activeLane == LANE_PROB) {
      const uint8_t next = mapToByteHysteresis(raw, patterns[activePattern].probability[step], 100, 0);
      if (next != patterns[activePattern].probability[step]) {
        patterns[activePattern].probability[step] = next;
        setLastEditNumber("PRB", patterns[activePattern].probability[step], now);
      }
    } else if (activeLane == LANE_RATCHET) {
      const uint8_t next = mapToByteHysteresis(raw, patterns[activePattern].ratchet[step], 4, 1);
      if (next != patterns[activePattern].ratchet[step]) {
        patterns[activePattern].ratchet[step] = next;
        setLastEditNumber("RAT", patterns[activePattern].ratchet[step], now);
      }
    } else {
      const int8_t next = mapToSignedHysteresis(raw, patterns[activePattern].voicingLock[step], 12, -12);
      if (next != patterns[activePattern].voicingLock[step]) {
        patterns[activePattern].voicingLock[step] = next;
        setLastEditNumber("LCK", patterns[activePattern].voicingLock[step], now);
      }
    }
  }
}

void scanExtraButton(unsigned long now) {
  selectMuxChannel(0);
  const uint8_t reading = digitalRead(kExtraButtonPin);

  if (reading != extraButtonLastReading) {
    extraButtonLastChangeMs = now;
    extraButtonLastReading = reading;
  }

  if ((now - extraButtonLastChangeMs) >= kButtonDebounceMs && reading != extraButtonStableState) {
    extraButtonStableState = reading;

    if (reading == LOW) {
      extraButtonHeld = true;
      extraButtonPressedAtMs = now;
      extraButtonLongHandled = false;
      shiftActionUsed = false;
      displayDirty = true;
    } else {
      const unsigned long heldFor = now - extraButtonPressedAtMs;
      extraButtonHeld = false;

      if (!extraButtonLongHandled && !shiftActionUsed && heldFor < kShiftHoldMs) {
        handleExtraButtonClick(now);
      }

      displayDirty = true;
    }
  }

  if (extraButtonHeld && !extraButtonLongHandled &&
      (now - extraButtonPressedAtMs) >= kUtilityLongPressMs && !shiftActionUsed) {
    panicVoices();
    changePage(PAGE_UTL, now);
    setLastEditText("UTL", "PNC", now);
    extraButtonLongHandled = true;
  }

  if (pendingSingleClick && (now - pendingClickMs) > kDoublePressMs) {
    changePage((uint8_t)((controlPage + 1) % kPageCount), now);
    pendingSingleClick = false;
  }
}

bool shiftActive(unsigned long now) {
  return extraButtonHeld && (now - extraButtonPressedAtMs) >= kShiftHoldMs;
}

void handleExtraButtonClick(unsigned long now) {
  if (pendingSingleClick && (now - pendingClickMs) <= kDoublePressMs) {
    changePage(controlPage == 0 ? kPageCount - 1 : controlPage - 1, now);
    pendingSingleClick = false;
  } else {
    pendingSingleClick = true;
    pendingClickMs = now;
  }
}

void changePage(uint8_t nextPage, unsigned long now) {
  if (nextPage >= kPageCount || nextPage == controlPage) {
    return;
  }

  controlPage = nextPage;
  resetLivePotPickup();
  showEditView(now);
}

void scanStepButtons(unsigned long now) {
  const bool shifted = shiftActive(now);

  for (uint8_t slot = 0; slot < kVisibleStepCount; ++slot) {
    selectMuxChannel(slot);
    const uint8_t reading = digitalRead(kStepButtonPin);

    if (reading != stepButtonLastReading[slot]) {
      stepButtonLastChangeMs[slot] = now;
      stepButtonLastReading[slot] = reading;
    }

    if ((now - stepButtonLastChangeMs[slot]) >= kButtonDebounceMs &&
        reading != stepButtonStableState[slot]) {
      stepButtonStableState[slot] = reading;

      if (reading == LOW) {
        if (shifted) {
          handleShiftStepButton(slot, now);
        } else {
          handleStepButton(slot, now);
        }
      }
    }
  }
}

void handleStepButton(uint8_t slot, unsigned long now) {
  if (controlPage == PAGE_UTL) {
    handleUtilityButton(slot, now);
    return;
  }

  const uint8_t step = (visibleRange ? 8 : 0) + slot;
  const bool enabled = !stepGate(activePattern, step);
  setStepGate(activePattern, step, enabled);
  setLastEditText("GAT", enabled ? "ON " : "OFF", now);
}

void handleUtilityButton(uint8_t slot, unsigned long now) {
  if (slot == 0) {
    panicVoices();
    setLastEditText("PNC", "OK ", now);
  } else if (slot == 1) {
    initPattern(activePattern);
    setLastEditText("INI", "OK ", now);
  } else if (slot == 2) {
    patterns[activePattern].gates = 0xFFFF;
    setLastEditText("GAT", "ALL", now);
  } else if (slot == 3) {
    controlPage = PAGE_OSC;
    resetLivePotPickup();
    setLastEditText("UTL", "EXT", now);
  }
}

void handleShiftStepButton(uint8_t slot, unsigned long now) {
  shiftActionUsed = true;

  switch (slot) {
    case 0:
      activeLane = LANE_NOTE;
      setLastEditText("LAN", "N  ", now);
      break;
    case 1:
      activeLane = LANE_GATE;
      setLastEditText("LAN", "G  ", now);
      break;
    case 2:
      activeLane = LANE_PROB;
      setLastEditText("LAN", "P  ", now);
      break;
    case 3:
      visibleRange = visibleRange ? 0 : 1;
      setLastEditText("RNG", visibleRange ? "916" : "1-8", now);
      break;
    case 4:
      clipboardPattern = patterns[activePattern];
      clipboardHasPattern = true;
      setLastEditText("CPY", "OK ", now);
      break;
    case 5:
      if (clipboardHasPattern) {
        patterns[activePattern] = clipboardPattern;
        resetLivePotPickup();
        setLastEditText("PST", "OK ", now);
      } else {
        setLastEditText("PST", "NO ", now);
      }
      break;
    case 6:
      clearCurrentLane();
      setLastEditText("CLR", "OK ", now);
      break;
    case 7:
      randomizeCurrentLane();
      setLastEditText("RND", "OK ", now);
      break;
  }
}

bool stepGate(uint8_t patternSlot, uint8_t step) {
  return (patterns[patternSlot].gates & (1U << step)) != 0;
}

void setStepGate(uint8_t patternSlot, uint8_t step, bool enabled) {
  if (enabled) {
    patterns[patternSlot].gates |= (1U << step);
  } else {
    patterns[patternSlot].gates &= ~(1U << step);
  }
  displayDirty = true;
}

void initPattern(uint8_t slot) {
  patterns[slot].gates = 0xFFFF;
  for (uint8_t step = 0; step < kStepCount; ++step) {
    patterns[slot].note[step] = 48 + step;
    patterns[slot].probability[step] = kDefaultProbability;
    patterns[slot].ratchet[step] = kDefaultRatchet;
    patterns[slot].voicingLock[step] = kDefaultVoicingLock;
  }
  displayDirty = true;
}

void clearCurrentLane() {
  const uint8_t baseStep = visibleRange ? 8 : 0;

  for (uint8_t slot = 0; slot < kVisibleStepCount; ++slot) {
    const uint8_t step = baseStep + slot;
    if (activeLane == LANE_NOTE) {
      patterns[activePattern].note[step] = 48;
    } else if (activeLane == LANE_GATE) {
      setStepGate(activePattern, step, false);
    } else if (activeLane == LANE_PROB) {
      patterns[activePattern].probability[step] = kDefaultProbability;
    } else if (activeLane == LANE_RATCHET) {
      patterns[activePattern].ratchet[step] = kDefaultRatchet;
    } else {
      patterns[activePattern].voicingLock[step] = kDefaultVoicingLock;
    }
  }
  displayDirty = true;
}

void randomizeCurrentLane() {
  const uint8_t baseStep = visibleRange ? 8 : 0;
  const int8_t noteSpread = (int8_t)(patControls.randomAmount / 8);
  const int8_t lockSpread = (int8_t)(patControls.randomAmount / 8);

  for (uint8_t slot = 0; slot < kVisibleStepCount; ++slot) {
    const uint8_t step = baseStep + slot;
    if (activeLane == LANE_NOTE) {
      const int8_t offset = (int8_t)random(-noteSpread, noteSpread + 1);
      patterns[activePattern].note[step] = constrain((int16_t)patterns[activePattern].note[step] + offset, 12, 84);
    } else if (activeLane == LANE_GATE) {
      setStepGate(activePattern, step, random(100) < patControls.randomAmount);
    } else if (activeLane == LANE_PROB) {
      patterns[activePattern].probability[step] = random(101);
    } else if (activeLane == LANE_RATCHET) {
      patterns[activePattern].ratchet[step] = (uint8_t)random(1, 5);
    } else {
      patterns[activePattern].voicingLock[step] = (int8_t)random(-lockSpread, lockSpread + 1);
    }
  }
  displayDirty = true;
}

void panicVoices() {
  for (uint8_t voice = 0; voice < kSynthVoiceCount; ++voice) {
    stopVoiceTracked(voice);
  }
  remainingRatchets = 0;
  ratchetStep = 255;
}

void applySynthControls() {
  if (oscControls.waveform != configuredWaveform) {
    edgar.setWave(kMainVoice, kWaveIds[oscControls.waveform]);
    edgar.setWave(kChordVoiceA, kWaveIds[oscControls.waveform]);
    edgar.setWave(kChordVoiceB, kWaveIds[oscControls.waveform]);
    configuredWaveform = oscControls.waveform;
  }

  if (osc2Controls.waveform != configuredOsc2Waveform) {
    edgar.setWave(kOsc2Voice, kWaveIds[osc2Controls.waveform]);
    configuredOsc2Waveform = osc2Controls.waveform;
  }

  if (envControls.envelope != configuredEnvelope) {
    edgar.setEnvelope(kMainVoice, envControls.envelope);
    edgar.setEnvelope(kChordVoiceA, envControls.envelope);
    edgar.setEnvelope(kChordVoiceB, envControls.envelope);
    configuredEnvelope = envControls.envelope;
  }

  if (osc2Controls.envelope != configuredOsc2Envelope) {
    if (osc2Controls.envelope == 0) {
      stopVoiceTracked(kOsc2Voice);
    } else {
      edgar.setEnvelope(kOsc2Voice, osc2Controls.envelope - 1);
    }
    configuredOsc2Envelope = osc2Controls.envelope;
  }

  if (envControls.length != configuredLength) {
    for (uint8_t voice = 0; voice < kSynthVoiceCount; ++voice) {
      edgar.setLength(voice, envControls.length);
    }
    configuredLength = envControls.length;
  }

  if (envControls.modulation != configuredModulation) {
    for (uint8_t voice = 0; voice < kSynthVoiceCount; ++voice) {
      edgar.setMod(voice, envControls.modulation);
    }
    configuredModulation = envControls.modulation;
  }

  if (fxControls.sampleHoldFrames != configuredSampleHoldFrames) {
    edgar.setSampleHold(fxControls.sampleHoldFrames);
    configuredSampleHoldFrames = fxControls.sampleHoldFrames;
  }
}

void runSequencer(unsigned long now) {
  if (remainingRatchets > 0 && (unsigned long)(now - lastRatchetMs) >= ratchetIntervalMs) {
    lastRatchetMs += ratchetIntervalMs;
    --remainingRatchets;
    triggerStepPlayback(ratchetStep);
    return;
  }

  if ((unsigned long)(now - lastStepMs) < nextStepIntervalMs) {
    return;
  }

  lastStepMs = now;
  nextStepIntervalMs = intervalAfterStep(currentStep);
  playStep(currentStep, nextStepIntervalMs);
  playingStep = currentStep;
  currentStep = (currentStep + 1) & 0x0F;
  displayDirty = true;
}

uint16_t bpmStepMs() {
  return (uint16_t)(15000UL / max((uint8_t)1, seqControls.bpm));
}

uint16_t intervalAfterStep(uint8_t step) {
  const uint16_t tempo = bpmStepMs();
  const uint16_t offset = ((uint32_t)tempo * seqControls.swing) / 200;

  if (offset == 0) {
    return tempo;
  }

  if (step & 0x01) {
    return tempo + offset;
  }

  return tempo > offset + 5 ? tempo - offset : 5;
}

void playStep(uint8_t step, uint16_t stepIntervalMs) {
  remainingRatchets = 0;
  ratchetStep = 255;

  if (!stepGate(activePattern, step)) {
    stopVoicesForRest();
    return;
  }

  const uint8_t probability = patterns[activePattern].probability[step];
  if (probability < 100 && random(100) >= probability) {
    stopVoicesForRest();
    return;
  }

  triggerStepPlayback(step);

  const uint8_t ratchets = constrain(patterns[activePattern].ratchet[step], 1, 4);
  if (ratchets > 1) {
    remainingRatchets = ratchets - 1;
    ratchetStep = step;
    ratchetIntervalMs = max((uint16_t)1, (uint16_t)(stepIntervalMs / ratchets));
    lastRatchetMs = lastStepMs;
  }
}

void triggerStepPlayback(uint8_t step) {
  if (oscControls.playMode == MODE_CHORD) {
    triggerChordStep(step);
  } else if (oscControls.playMode == MODE_ARP) {
    triggerArpStep(step);
  } else {
    triggerScaleStep(step);
  }
}

void triggerScaleStep(uint8_t step) {
  const uint8_t note = quantizeNoteWithTranspose(patterns[activePattern].note[step]);
  uint8_t usedMask = 0;

  triggerVoiceWithPitch(kMainVoice, pitchWordForNote(note));
  usedMask |= (1 << kMainVoice);

  if (osc2Enabled()) {
    triggerVoiceWithPitch(kOsc2Voice, osc2PitchWordForNote(note));
    usedMask |= (1 << kOsc2Voice);
  }

  stopUnusedVoices(usedMask);
}

void triggerChordStep(uint8_t step) {
  uint8_t chordNotes[3];
  buildChordNotes(quantizeNoteWithTranspose(patterns[activePattern].note[step]), chordNotes);
  applyVoicing(chordNotes, constrain((int8_t)(oscControls.voicing + patterns[activePattern].voicingLock[step]), -12, 12));

  uint8_t usedMask = 0;
  triggerVoiceWithPitch(kMainVoice, pitchWordForNote(chordNotes[0]));
  usedMask |= (1 << kMainVoice);

  if (osc2Enabled()) {
    triggerVoiceWithPitch(kOsc2Voice, osc2PitchWordForNote(chordNotes[1]));
    triggerVoiceWithPitch(kChordVoiceA, pitchWordForNote(chordNotes[2]));
    usedMask |= (1 << kOsc2Voice) | (1 << kChordVoiceA);
  } else {
    triggerVoiceWithPitch(kChordVoiceA, pitchWordForNote(chordNotes[1]));
    triggerVoiceWithPitch(kChordVoiceB, pitchWordForNote(chordNotes[2]));
    usedMask |= (1 << kChordVoiceA) | (1 << kChordVoiceB);
  }

  stopUnusedVoices(usedMask);
}

void triggerArpStep(uint8_t step) {
  uint8_t chordNotes[3];
  buildChordNotes(quantizeNoteWithTranspose(patterns[activePattern].note[step]), chordNotes);
  applyVoicing(chordNotes, constrain((int8_t)(oscControls.voicing + patterns[activePattern].voicingLock[step]), -12, 12));

  const uint8_t note = chordNotes[arpIndex % 3];
  ++arpIndex;

  uint8_t usedMask = 0;
  triggerVoiceWithPitch(kMainVoice, pitchWordForNote(note));
  usedMask |= (1 << kMainVoice);

  if (osc2Enabled()) {
    triggerVoiceWithPitch(kOsc2Voice, osc2PitchWordForNote(note));
    usedMask |= (1 << kOsc2Voice);
  }

  stopUnusedVoices(usedMask);
}

void triggerVoiceWithPitch(uint8_t voice, uint16_t pitch) {
  targetPitchWord[voice] = pitch;

  if (envControls.glide == 0 || currentPitchWord[voice] == 0) {
    currentPitchWord[voice] = targetPitchWord[voice];
    edgar.setPitchWord(voice, currentPitchWord[voice]);
  }

  edgar.trigger(voice);
}

void stopVoiceTracked(uint8_t voice) {
  edgar.stopVoice(voice);
  currentPitchWord[voice] = 0;
  targetPitchWord[voice] = 0;
}

void stopUnusedVoices(uint8_t usedMask) {
  for (uint8_t voice = 0; voice < kSynthVoiceCount; ++voice) {
    if ((usedMask & (1 << voice)) == 0) {
      stopVoiceTracked(voice);
    }
  }
}

void stopVoicesForRest() {
  if (envControls.glide == 0) {
    stopUnusedVoices(0);
  }
}

void updateGlide() {
  if (envControls.glide == 0) {
    return;
  }

  for (uint8_t voice = 0; voice < kSynthVoiceCount; ++voice) {
    if (targetPitchWord[voice] != 0 && currentPitchWord[voice] != targetPitchWord[voice]) {
      currentPitchWord[voice] = glidePitch(currentPitchWord[voice], targetPitchWord[voice]);
      edgar.setPitchWord(voice, currentPitchWord[voice]);
    }
  }
}

uint16_t glidePitch(uint16_t current, uint16_t target) {
  const uint8_t divisor = map(envControls.glide, 1, 127, 2, 24);
  const int32_t difference = (int32_t)target - current;

  if (difference == 0) {
    return target;
  }

  int32_t step = difference / divisor;
  if (step == 0) {
    step = difference > 0 ? 1 : -1;
  }

  return (uint16_t)(current + step);
}

bool osc2Enabled() {
  return osc2Controls.envelope > 0;
}

uint8_t quantizeNoteWithTranspose(uint8_t note) {
  int16_t transposed = (int16_t)note + oscControls.transpose;
  if (transposed < 0) {
    transposed = 0;
  } else if (transposed > SYNTH_MIDI_NOTE_MAX) {
    transposed = SYNTH_MIDI_NOTE_MAX;
  }

  return quantizeNote((uint8_t)transposed, seqControls.scale);
}

uint16_t pitchWordForNote(uint8_t note) {
  if (note > SYNTH_MIDI_NOTE_MAX) {
    note = SYNTH_MIDI_NOTE_MAX;
  }
  return pgm_read_word(&PITCHS[note]);
}

uint16_t osc2PitchWordForNote(uint8_t note) {
  const uint8_t transposed = transposeNote(note, osc2Controls.transpose);
  const uint16_t pitchWord = pitchWordForNote(transposed);
  return pitchWord + (((uint32_t)pitchWord * osc2Controls.detune) / 512);
}

void buildChordNotes(uint8_t root, uint8_t notes[3]) {
  int8_t third = 4;
  int8_t fifth = 7;

  if (seqControls.scale == SCALE_MINOR || seqControls.scale == SCALE_BLUES) {
    third = 3;
  } else if (seqControls.scale == SCALE_PENTATONIC) {
    third = 5;
  }

  notes[0] = root;
  notes[1] = transposeNote(root, third);
  notes[2] = transposeNote(root, fifth);
}

void sortChordNotes(int16_t notes[3]) {
  for (uint8_t i = 0; i < 2; ++i) {
    for (uint8_t j = i + 1; j < 3; ++j) {
      if (notes[j] < notes[i]) {
        const int16_t temp = notes[i];
        notes[i] = notes[j];
        notes[j] = temp;
      }
    }
  }
}

uint8_t clampMidiNote(int16_t note) {
  if (note < 0) {
    return 0;
  }
  if (note > SYNTH_MIDI_NOTE_MAX) {
    return SYNTH_MIDI_NOTE_MAX;
  }
  return (uint8_t)note;
}

void applyVoicing(uint8_t notes[3], int8_t voicing) {
  int16_t voiced[3] = {notes[0], notes[1], notes[2]};

  while (voicing > 0) {
    sortChordNotes(voiced);
    voiced[0] += 12;
    --voicing;
  }

  while (voicing < 0) {
    sortChordNotes(voiced);
    voiced[2] -= 12;
    ++voicing;
  }

  sortChordNotes(voiced);
  notes[0] = clampMidiNote(voiced[0]);
  notes[1] = clampMidiNote(voiced[1]);
  notes[2] = clampMidiNote(voiced[2]);
}

uint8_t transposeNote(uint8_t note, int8_t semitones) {
  int16_t transposed = (int16_t)note + semitones;

  if (transposed < 0) {
    return 0;
  }

  if (transposed > SYNTH_MIDI_NOTE_MAX) {
    return SYNTH_MIDI_NOTE_MAX;
  }

  return (uint8_t)transposed;
}

uint8_t quantizeNote(uint8_t note, uint8_t scale) {
  if (scale == SCALE_CHROMATIC) {
    return note;
  }

  for (uint8_t distance = 0; distance < 6; ++distance) {
    if (note >= distance && noteAllowedInScale(note - distance, scale)) {
      return note - distance;
    }

    if (note + distance <= SYNTH_MIDI_NOTE_MAX && noteAllowedInScale(note + distance, scale)) {
      return note + distance;
    }
  }

  return note;
}

bool noteAllowedInScale(uint8_t note, uint8_t scale) {
  const uint8_t pitchClass = note % 12;

  switch (scale) {
    case SCALE_MAJOR:
      return pitchClass == 0 || pitchClass == 2 || pitchClass == 4 || pitchClass == 5 ||
             pitchClass == 7 || pitchClass == 9 || pitchClass == 11;
    case SCALE_MINOR:
      return pitchClass == 0 || pitchClass == 2 || pitchClass == 3 || pitchClass == 5 ||
             pitchClass == 7 || pitchClass == 8 || pitchClass == 10;
    case SCALE_PENTATONIC:
      return pitchClass == 0 || pitchClass == 2 || pitchClass == 4 ||
             pitchClass == 7 || pitchClass == 9;
    case SCALE_BLUES:
      return pitchClass == 0 || pitchClass == 3 || pitchClass == 5 ||
             pitchClass == 6 || pitchClass == 7 || pitchClass == 10;
    default:
      return true;
  }
}

const char *waveLabel(uint8_t waveform) {
  switch (waveform) {
    case 0:
      return "SIN";
    case 1:
      return "TRI";
    case 2:
      return "SQR";
    case 3:
      return "SAW";
    case 4:
      return "RMP";
    case 5:
      return "NOI";
    default:
      return "WAV";
  }
}

const char *playModeLabel(uint8_t mode) {
  switch (mode) {
    case MODE_CHORD:
      return "CHD";
    case MODE_ARP:
      return "ARP";
    default:
      return "SCL";
  }
}

const char *osc2EnvelopeLabel(uint8_t envelope) {
  switch (envelope) {
    case 1:
      return "E0 ";
    case 2:
      return "E1 ";
    case 3:
      return "E2 ";
    case 4:
      return "E3 ";
    default:
      return "OFF";
  }
}

const char *scaleLabel(uint8_t scale) {
  switch (scale) {
    case SCALE_MAJOR:
      return "MAJ";
    case SCALE_MINOR:
      return "MIN";
    case SCALE_PENTATONIC:
      return "PEN";
    case SCALE_BLUES:
      return "BLU";
    default:
      return "CHR";
  }
}

const char *laneLabel(uint8_t lane) {
  switch (lane) {
    case LANE_GATE:
      return "G  ";
    case LANE_PROB:
      return "P  ";
    case LANE_RATCHET:
      return "R  ";
    case LANE_LOCK:
      return "L  ";
    default:
      return "N  ";
  }
}

void printPadded2(uint8_t value) {
  if (value < 10) {
    lcd.print('0');
  }
  lcd.print(value);
}

void printPadded3(uint16_t value) {
  if (value < 100) {
    lcd.print(' ');
  }
  if (value < 10) {
    lcd.print(' ');
  }
  lcd.print(value);
}

void printSigned3(int8_t value) {
  if (value >= 0) {
    lcd.print('+');
  } else {
    lcd.print('-');
    value = -value;
  }
  if (value < 10) {
    lcd.print('0');
  }
  lcd.print(value);
}

void printSpaces(uint8_t count) {
  while (count--) {
    lcd.print(' ');
  }
}

void printPageLabel() {
  switch (controlPage) {
    case PAGE_OSC2:
      lcd.print(F("O2 "));
      break;
    case PAGE_ENV:
      lcd.print(F("ENV"));
      break;
    case PAGE_SEQ:
      lcd.print(F("SEQ"));
      break;
    case PAGE_PAT:
      lcd.print(F("PAT"));
      break;
    case PAGE_FX:
      lcd.print(F("FX "));
      break;
    case PAGE_UTL:
      lcd.print(F("UTL"));
      break;
    case PAGE_OSC:
    default:
      lcd.print(F("OSC"));
      break;
  }
}

bool controlsChangedForDisplay() {
  return controlPage != lastDisplayedPage ||
         activePattern != lastDisplayedPattern ||
         activeLane != lastDisplayedLane ||
         visibleRange != lastDisplayedRange ||
         oscControls.waveform != lastDisplayedOscControls.waveform ||
         oscControls.playMode != lastDisplayedOscControls.playMode ||
         oscControls.voicing != lastDisplayedOscControls.voicing ||
         oscControls.transpose != lastDisplayedOscControls.transpose ||
         osc2Controls.waveform != lastDisplayedOsc2Controls.waveform ||
         osc2Controls.envelope != lastDisplayedOsc2Controls.envelope ||
         osc2Controls.detune != lastDisplayedOsc2Controls.detune ||
         osc2Controls.transpose != lastDisplayedOsc2Controls.transpose ||
         envControls.envelope != lastDisplayedEnvControls.envelope ||
         envControls.length != lastDisplayedEnvControls.length ||
         envControls.modulation != lastDisplayedEnvControls.modulation ||
         envControls.glide != lastDisplayedEnvControls.glide ||
         seqControls.bpm != lastDisplayedSeqControls.bpm ||
         seqControls.swing != lastDisplayedSeqControls.swing ||
         seqControls.scale != lastDisplayedSeqControls.scale ||
         patControls.targetSlot != lastDisplayedPatControls.targetSlot ||
         patControls.randomAmount != lastDisplayedPatControls.randomAmount ||
         fxControls.sampleHoldFrames != lastDisplayedFxControls.sampleHoldFrames;
}

void renderDisplay(unsigned long now) {
  const bool controlsChanged = controlsChangedForDisplay();
  const bool stepChanged = renderedStep != playingStep;
  const bool shiftChanged = shiftActive(now) != lastDisplayedShift;

  if (!displayDirty && !controlsChanged && !stepChanged && !shiftChanged) {
    return;
  }

  if (now < editViewUntilMs) {
    renderPageView(now);
  } else {
    renderDashboard(now);
  }

  renderedStep = playingStep;
  lastDisplayedPage = controlPage;
  lastDisplayedPattern = activePattern;
  lastDisplayedLane = activeLane;
  lastDisplayedRange = visibleRange;
  lastDisplayedOscControls = oscControls;
  lastDisplayedOsc2Controls = osc2Controls;
  lastDisplayedEnvControls = envControls;
  lastDisplayedSeqControls = seqControls;
  lastDisplayedPatControls = patControls;
  lastDisplayedFxControls = fxControls;
  lastDisplayedShift = shiftActive(now);
  displayDirty = false;
}

void renderDashboard(unsigned long now) {
  lcd.setCursor(0, 0);
  lcd.print('P');
  lcd.print(activePattern + 1);
  lcd.print(' ');
  lcd.print(laneLabel(activeLane)[0]);
  lcd.print(' ');
  if (visibleRange) {
    lcd.print(F("9-16 "));
  } else {
    lcd.print(F("1-8  "));
  }
  printPadded3(seqControls.bpm);
  lcd.print('B');
  printSpaces(2);

  lcd.setCursor(0, 1);
  printStepMask();
  lcd.print(' ');
  lcd.print(lastEditLabel);
  lcd.print(lastEditValue);
  printSpaces(1);
}

void printText3OrHint(uint8_t pot, const char *text) {
  if (!livePotCaptured[pot] && pickupHint[pot] != ' ') {
    lcd.print(pickupHint[pot]);
    lcd.print(F("  "));
  } else {
    lcd.print(text);
  }
}

void printNumber2OrHint(uint8_t pot, uint8_t value) {
  if (!livePotCaptured[pot] && pickupHint[pot] != ' ') {
    lcd.print(pickupHint[pot]);
    lcd.print(' ');
  } else {
    printPadded2(value);
  }
}

void printNumber3OrHint(uint8_t pot, uint16_t value) {
  if (!livePotCaptured[pot] && pickupHint[pot] != ' ') {
    lcd.print(pickupHint[pot]);
    lcd.print(F("  "));
  } else {
    printPadded3(value);
  }
}

void printSigned3OrHint(uint8_t pot, int8_t value) {
  if (!livePotCaptured[pot] && pickupHint[pot] != ' ') {
    lcd.print(pickupHint[pot]);
    lcd.print(F("  "));
  } else {
    printSigned3(value);
  }
}

void printStepMask() {
  const uint8_t baseStep = visibleRange ? 8 : 0;

  for (uint8_t slot = 0; slot < kVisibleStepCount; ++slot) {
    const uint8_t step = baseStep + slot;
    const bool enabled = stepGate(activePattern, step);

    if (step == playingStep) {
      lcd.print(enabled ? 'O' : 'o');
    } else {
      lcd.print(enabled ? 'x' : '-');
    }
  }
}

void renderPageView(unsigned long now) {
  lcd.setCursor(0, 0);
  printPageLabel();

  if (shiftActive(now)) {
    lcd.print(F(" SHIFT       "));
    lcd.setCursor(0, 1);
    lcd.print(F("1N2G3P4R5C6P7X8?"));
    return;
  }

  switch (controlPage) {
    case PAGE_OSC2:
      lcd.print(F(" W E D T     "));
      lcd.setCursor(0, 1);
      printPickupOrOsc2Values();
      break;
    case PAGE_ENV:
      lcd.print(F(" E L M G     "));
      lcd.setCursor(0, 1);
      printPickupOrEnvValues();
      break;
    case PAGE_SEQ:
      lcd.print(F(" B S C L     "));
      lcd.setCursor(0, 1);
      printPickupOrSeqValues();
      break;
    case PAGE_PAT:
      lcd.print(F(" P D R G     "));
      lcd.setCursor(0, 1);
      printPickupOrPatValues();
      break;
    case PAGE_FX:
      lcd.print(F(" H - - -     "));
      lcd.setCursor(0, 1);
      printPickupOrFxValues();
      break;
    case PAGE_UTL:
      lcd.print(F(" 1P 2I 3G 4X"));
      lcd.setCursor(0, 1);
      lcd.print(F("   "));
      lcd.print(lastEditLabel);
      lcd.print(lastEditValue);
      printSpaces(7);
      break;
    case PAGE_OSC:
    default:
      lcd.print(F(" W M V T     "));
      lcd.setCursor(0, 1);
      printPickupOrOscValues();
      break;
  }
}

void printPickupOrOscValues() {
  printText3OrHint(0, waveLabel(oscControls.waveform));
  lcd.print(' ');
  printText3OrHint(1, playModeLabel(oscControls.playMode));
  lcd.print(' ');
  printSigned3OrHint(2, oscControls.voicing);
  lcd.print(' ');
  printSigned3OrHint(3, oscControls.transpose);
  printSpaces(1);
}

void printPickupOrOsc2Values() {
  printText3OrHint(0, waveLabel(osc2Controls.waveform));
  lcd.print(' ');
  printText3OrHint(1, osc2EnvelopeLabel(osc2Controls.envelope));
  lcd.print(' ');
  printNumber2OrHint(2, osc2Controls.detune);
  lcd.print(' ');
  printSigned3OrHint(3, osc2Controls.transpose);
  printSpaces(2);
}

void printPickupOrEnvValues() {
  if (!livePotCaptured[0] && pickupHint[0] != ' ') {
    lcd.print(pickupHint[0]);
    lcd.print(' ');
  } else {
    lcd.print('E');
    lcd.print(envControls.envelope);
  }
  lcd.print(' ');
  printNumber3OrHint(1, envControls.length);
  lcd.print(' ');
  printNumber3OrHint(2, envControls.modulation);
  lcd.print(' ');
  printNumber3OrHint(3, envControls.glide);
  printSpaces(2);
}

void printPickupOrSeqValues() {
  printNumber3OrHint(0, seqControls.bpm);
  lcd.print(' ');
  printNumber2OrHint(1, seqControls.swing);
  lcd.print(' ');
  printText3OrHint(2, scaleLabel(seqControls.scale));
  lcd.print(' ');
  if (!livePotCaptured[3] && pickupHint[3] != ' ') {
    lcd.print(pickupHint[3]);
  } else {
    lcd.print(laneLabel(activeLane)[0]);
  }
  printSpaces(4);
}

void printPickupOrPatValues() {
  if (!livePotCaptured[0] && pickupHint[0] != ' ') {
    lcd.print(pickupHint[0]);
    lcd.print(' ');
  } else {
    lcd.print('P');
    lcd.print(activePattern + 1);
  }
  lcd.print(' ');
  if (!livePotCaptured[1] && pickupHint[1] != ' ') {
    lcd.print(pickupHint[1]);
    lcd.print(' ');
  } else {
    lcd.print('D');
    lcd.print(patControls.targetSlot + 1);
  }
  lcd.print(' ');
  printNumber3OrHint(2, patControls.randomAmount);
  lcd.print(' ');
  printText3OrHint(3, visibleRange ? "916" : "1-8");
  printSpaces(3);
}

void printPickupOrFxValues() {
  if (!livePotCaptured[0] && pickupHint[0] != ' ') {
    lcd.print(pickupHint[0]);
    lcd.print(F("   --  --  --  "));
  } else {
    lcd.print(F("H"));
    printPadded2(fxControls.sampleHoldFrames);
    lcd.print(F(" -- -- --    "));
  }
}
