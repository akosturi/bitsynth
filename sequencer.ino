#include <LiquidCrystal.h>
#include "synth.h"

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

const uint8_t kMuxBit0Pin = 8;
const uint8_t kMuxBit1Pin = 9;
const uint8_t kMuxBit2Pin = 10;

const uint8_t kSettingsAnalogPin = A0;
const uint8_t kStepButtonPin = A1;
const uint8_t kPitchAnalogPin = A2;
const uint8_t kResetButtonPin = A3;

const uint8_t kVoice = 0;
const uint8_t kStepCount = 8;
const uint8_t kSettingsCount = 4;
const uint8_t kWaveCount = 6;
const uint8_t kAnalogSamples = 5;
const uint8_t kButtonDebounceMs = 25;
const uint16_t kDefaultTempoMs = 50;

const uint8_t kWaveIds[kWaveCount] = {SINE, TRIANGLE, SQUARE, SAW, RAMP, NOISE};

struct SynthControls {
  uint8_t waveform;
  uint16_t tempoMs;
  uint8_t length;
  uint8_t modulation;
};

synth edgar;

SynthControls controls = {0, kDefaultTempoMs, 64, 64};
SynthControls lastDisplayedControls = {255, 0, 255, 255};

uint8_t stepPitch[kStepCount] = {0};
bool stepEnabled[kStepCount] = {true, true, true, true, true, true, true, true};

uint8_t buttonStableState[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
uint8_t buttonLastReading[kStepCount] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long buttonLastChangeMs[kStepCount] = {0};

uint8_t currentStep = 0;
uint8_t lastDisplayedStep = 255;
uint8_t renderedStep = 255;
uint8_t configuredWaveform = 255;
uint8_t configuredLength = 255;
uint8_t configuredModulation = 255;
unsigned long lastStepMs = 0;
bool resetHeld = false;
bool lastDisplayedResetHeld = false;
bool displayDirty = true;

void setup() {
  edgar.begin(CHA);
  edgar.setupVoice(kVoice, SINE, 0, ENVELOPE0, controls.length, controls.modulation);

  pinMode(kMuxBit0Pin, OUTPUT);
  pinMode(kMuxBit1Pin, OUTPUT);
  pinMode(kMuxBit2Pin, OUTPUT);
  pinMode(kSettingsAnalogPin, INPUT);
  pinMode(kStepButtonPin, INPUT_PULLUP);
  pinMode(kPitchAnalogPin, INPUT);
  pinMode(kResetButtonPin, INPUT_PULLUP);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print(F("BitSynth"));
  delay(500);
  lcd.clear();
}

void loop() {
  const unsigned long now = millis();

  readControls();
  scanStepButtons(now);
  scanResetButton();
  applySynthControls();
  runSequencer(now);
  renderDisplay();
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(kMuxBit0Pin, bitRead(channel, 0));
  digitalWrite(kMuxBit1Pin, bitRead(channel, 1));
  digitalWrite(kMuxBit2Pin, bitRead(channel, 2));
  delayMicroseconds(5);
}

uint16_t readMedianAnalog(uint8_t pin) {
  uint16_t samples[kAnalogSamples];

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

uint8_t mapToByte(uint16_t value, int16_t outMin, int16_t outMax) {
  return (uint8_t)constrain(map(value, 0, 1023, outMin, outMax), min(outMin, outMax), max(outMin, outMax));
}

void readControls() {
  SynthControls nextControls;

  for (uint8_t channel = 0; channel < kSettingsCount; ++channel) {
    selectMuxChannel(channel);
    const uint16_t value = readMedianAnalog(kSettingsAnalogPin);

    switch (channel) {
      case 0:
        nextControls.waveform = mapToByte(value, kWaveCount - 1, 0);
        break;
      case 1:
        nextControls.tempoMs = (uint16_t)map(value, 0, 1023, 10, 350);
        break;
      case 2:
        nextControls.length = mapToByte(value, 127, 0);
        break;
      case 3:
        nextControls.modulation = mapToByte(value, 127, 0);
        break;
    }
  }

  for (uint8_t step = 0; step < kStepCount; ++step) {
    selectMuxChannel(step);
    stepPitch[step] = mapToByte(readMedianAnalog(kPitchAnalogPin), 84, 12);
  }

  if (nextControls.waveform != controls.waveform ||
      nextControls.tempoMs != controls.tempoMs ||
      nextControls.length != controls.length ||
      nextControls.modulation != controls.modulation) {
    controls = nextControls;
    displayDirty = true;
  }
}

void scanStepButtons(unsigned long now) {
  for (uint8_t step = 0; step < kStepCount; ++step) {
    selectMuxChannel(step);
    const uint8_t reading = digitalRead(kStepButtonPin);

    if (reading != buttonLastReading[step]) {
      buttonLastChangeMs[step] = now;
      buttonLastReading[step] = reading;
    }

    if ((now - buttonLastChangeMs[step]) >= kButtonDebounceMs &&
        reading != buttonStableState[step]) {
      buttonStableState[step] = reading;

      if (reading == LOW) {
        stepEnabled[step] = !stepEnabled[step];
        displayDirty = true;
      }
    }
  }
}

void scanResetButton() {
  selectMuxChannel(0);
  const bool pressed = digitalRead(kResetButtonPin) == LOW;

  if (pressed && !resetHeld) {
    for (uint8_t step = 0; step < kStepCount; ++step) {
      stepEnabled[step] = true;
    }
    displayDirty = true;
  }

  if (pressed != resetHeld) {
    resetHeld = pressed;
    displayDirty = true;
  }
}

void applySynthControls() {
  if (controls.waveform != configuredWaveform) {
    edgar.setWave(kVoice, kWaveIds[controls.waveform]);
    configuredWaveform = controls.waveform;
  }

  if (controls.length != configuredLength) {
    edgar.setLength(kVoice, controls.length);
    configuredLength = controls.length;
  }

  if (controls.modulation != configuredModulation) {
    edgar.setMod(kVoice, controls.modulation);
    configuredModulation = controls.modulation;
  }
}

void runSequencer(unsigned long now) {
  if ((unsigned long)(now - lastStepMs) < controls.tempoMs) {
    return;
  }

  lastStepMs = now;
  playStep(currentStep);
  lastDisplayedStep = currentStep;
  currentStep = (currentStep + 1) & 0x07;
  displayDirty = true;
}

void playStep(uint8_t step) {
  if (!stepEnabled[step]) {
    return;
  }

  edgar.setPitch(kVoice, stepPitch[step]);
  edgar.trigger(kVoice);
}

void printWaveLabel(uint8_t waveform) {
  switch (waveform) {
    case 0:
      lcd.print(F("SIN"));
      break;
    case 1:
      lcd.print(F("TRI"));
      break;
    case 2:
      lcd.print(F("SQR"));
      break;
    case 3:
      lcd.print(F("SAW"));
      break;
    case 4:
      lcd.print(F("RMP"));
      break;
    default:
      lcd.print(F("WAV"));
      break;
  }
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

void renderDisplay() {
  const bool controlsChanged = controls.waveform != lastDisplayedControls.waveform ||
                               controls.tempoMs != lastDisplayedControls.tempoMs ||
                               controls.length != lastDisplayedControls.length ||
                               controls.modulation != lastDisplayedControls.modulation;
  const bool stepChanged = renderedStep != lastDisplayedStep;
  const bool resetChanged = resetHeld != lastDisplayedResetHeld;

  if (!displayDirty && !controlsChanged && !stepChanged && !resetChanged) {
    return;
  }

  if (displayDirty || stepChanged || controls.waveform != lastDisplayedControls.waveform) {
    lcd.setCursor(0, 0);
    for (uint8_t step = 0; step < kStepCount; ++step) {
      if (step == lastDisplayedStep) {
        lcd.write(0xFF);
      } else {
        lcd.print(stepEnabled[step] ? '.' : ' ');
      }
    }
    lcd.print(F("     "));
    printWaveLabel(controls.waveform);
    renderedStep = lastDisplayedStep;
  }

  if (controlsChanged || resetChanged) {
    lcd.setCursor(0, 1);
    lcd.print(resetHeld ? F("RST ") : F("    "));
    printPadded3(controls.tempoMs);
    lcd.print(' ');
    printPadded3(controls.length);
    lcd.print(' ');
    printPadded3(controls.modulation);
    lcd.print(' ');
  }

  lastDisplayedControls = controls;
  lastDisplayedResetHeld = resetHeld;
  displayDirty = false;
}
