#ifndef _SYNTH
#define _SYNTH
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "tables.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define DIFF 1
#define CHA 2
#define CHB 3

#define SINE     0
#define TRIANGLE 1
#define SQUARE   2
#define SAW      3
#define RAMP     4
#define NOISE    5

#define ENVELOPE0 0
#define ENVELOPE1 1
#define ENVELOPE2 2
#define ENVELOPE3 3

#define FS 20000UL

static_assert(kickSampleRate == FS, "kick sample rate must match the 20 kHz audio engine");
static_assert(snareSampleRate == FS, "snare sample rate must match the 20 kHz audio engine");
static_assert(hihatSampleRate == FS, "hi-hat sample rate must match the 20 kHz audio engine");
static_assert(kickSampleBits == 9, "kick sample must use signed 9-bit PCM");
static_assert(snareSampleBits == 9, "snare sample must use signed 9-bit PCM");
static_assert(hihatSampleBits == 9, "hi-hat sample must use signed 9-bit PCM");
static_assert(sizeof(kickSample) == (kickSampleLength * 9U + 7U) / 8U, "invalid packed kick sample size");
static_assert(sizeof(snareSample) == (snareSampleLength * 9U + 7U) / 8U, "invalid packed snare sample size");
static_assert(sizeof(hihatSample) == (hihatSampleLength * 9U + 7U) / 8U, "invalid packed hi-hat sample size");

#define SET(x,y) (x |= (1 << y))
#define CLR(x,y) (x &= (~(1 << y)))

const uint8_t SYNTH_VOICE_COUNT = 4;
const uint8_t SYNTH_DRONE_COUNT = 8;
const uint8_t SYNTH_MIDI_NOTE_MAX = 127;
const uint8_t SYNTH_LENGTH_MAX = 127;
const uint8_t SYNTH_WAVETABLE_INDEX_BITS = 8;
const uint8_t SYNTH_WAVETABLE_SHIFT = 16 - SYNTH_WAVETABLE_INDEX_BITS;

static_assert(sizeof(SinTable) / sizeof(SinTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid sine table size");
static_assert(sizeof(TriangleTable) / sizeof(TriangleTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid triangle table size");
static_assert(sizeof(SquareTable) / sizeof(SquareTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid square table size");
static_assert(sizeof(SawTable) / sizeof(SawTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid saw table size");
static_assert(sizeof(RampTable) / sizeof(RampTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid ramp table size");
static_assert(sizeof(NoiseTable) / sizeof(NoiseTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid noise table size");

volatile uint16_t PCW[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
volatile uint16_t FTW[SYNTH_VOICE_COUNT] = {1000, 200, 300, 400};
volatile uint8_t AMP[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
volatile uint16_t PITCH[SYNTH_VOICE_COUNT] = {500, 500, 500, 500};
volatile int8_t MOD[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
volatile uint16_t wavs[SYNTH_VOICE_COUNT] = {
  (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable
};
volatile uint16_t envs[SYNTH_VOICE_COUNT] = {
  (uint16_t)Env0, (uint16_t)Env0, (uint16_t)Env0, (uint16_t)Env0
};
volatile uint16_t EPCW[SYNTH_VOICE_COUNT] = {0x8000, 0x8000, 0x8000, 0x8000};
volatile uint16_t EFTW[SYNTH_VOICE_COUNT] = {10, 10, 10, 10};
volatile uint8_t divider = 4;
volatile uint8_t noiseVoiceMask = 0;
volatile int16_t pwmQuantizationError = 0;
volatile uint8_t pwmDitherLfsr = 0xA5;
volatile uint8_t sampleHoldFrames = 1;
volatile uint8_t sampleHoldCounter = 0;
volatile uint16_t heldPwmSample = 256;
volatile bool playMorphActive = false;
volatile uint16_t playMorphWavPtr = (uint16_t)SinTable;
volatile uint32_t playMorphLfoPCW = 0x80000000UL;
volatile uint32_t playMorphLfoFTW = 0;
volatile uint8_t playMorphAmount = 128;
volatile uint8_t playFilterAmount = 0;
volatile uint8_t playFilterDepth = 0;
int16_t playFilterStateQ7 = 0;
volatile uint32_t playFilterLfoPCW = 0x80000000UL;
volatile uint32_t playFilterLfoFTW = 0;
uint8_t playLfoDivider = 0;
volatile bool droneMode = false;
volatile uint16_t dronePCW[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile uint16_t droneFTW[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile uint16_t droneWavs[SYNTH_DRONE_COUNT] = {
  (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable,
  (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable
};
volatile uint8_t droneAmp[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile uint8_t droneTargetAmp[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile uint8_t droneNoiseMask = 0;
volatile uint8_t droneRampVoice = 0;
volatile uint8_t droneMixPair = 0;
volatile int16_t dronePairMixQ7[SYNTH_DRONE_COUNT / 2] = {0, 0, 0, 0};
volatile uint32_t droneLfoPCW[SYNTH_DRONE_COUNT / 2] = {0, 0, 0, 0};
volatile uint32_t droneLfoFTW[SYNTH_DRONE_COUNT / 2] = {0, 0, 0, 0};
volatile bool drumActive = false;
volatile uint16_t drumSampleAddress = 0;
volatile uint16_t drumSampleLength = 0;
volatile uint32_t drumSamplePhase = 0;
volatile uint32_t drumSampleStep = 0x10000UL;
volatile uint16_t drumEnvelope = 0;
volatile uint16_t drumEnvelopeStep = 1;

static inline __attribute__((always_inline)) int16_t readSigned9Sample(
  uint16_t sampleAddress, uint16_t sampleIndex)
{
  return (int16_t)pgm_read_word(sampleAddress + (sampleIndex << 1));
}

// Decodes a signed 9-bit sample packed across byte boundaries.
static inline __attribute__((always_inline)) int16_t readPackedSigned9Sample(
  uint16_t sampleAddress, uint16_t sampleIndex)
{
  const uint16_t byteIndex = sampleIndex + (sampleIndex >> 3);
  const uint8_t shift = sampleIndex & 0x07;
  const uint16_t packed = pgm_read_word(sampleAddress + byteIndex);
  const uint16_t encoded = (packed >> shift) & 0x01FF;
  return (encoded & 0x0100) ? (int16_t)(encoded | 0xFE00) : encoded;
}

// Scales a signed sample with arithmetic-shift rounding.
static inline __attribute__((always_inline)) int16_t scaleSigned9Sample(
  int16_t sample, uint8_t gain, uint8_t shift)
{
  const bool negative = sample < 0;
  const uint16_t magnitude = negative ? -sample : sample;
  uint16_t product = magnitude * gain;
  if (negative) {
    product += (1U << shift) - 1U;
    return -(int16_t)(product >> shift);
  }
  return product >> shift;
}

static inline __attribute__((always_inline)) int16_t nextVoiceSample(uint8_t voice)
{
  const uint16_t phase = PCW[voice] + FTW[voice];
  PCW[voice] = phase;
  return readSigned9Sample(wavs[voice], phase >> SYNTH_WAVETABLE_SHIFT);
}

// Blends two waveforms at a shared phase.
static inline __attribute__((always_inline)) int16_t nextMorphedVoiceSample(
  uint8_t voice, uint16_t secondaryWave, uint8_t morphAmount)
{
  const uint16_t phase = PCW[voice] + FTW[voice];
  PCW[voice] = phase;
  const uint16_t index = phase >> SYNTH_WAVETABLE_SHIFT;
  const int16_t sampleA = readSigned9Sample(wavs[voice], index);
  const int16_t sampleB = readSigned9Sample(secondaryWave, index);
  const uint8_t primaryAmount = 128 - morphAmount;
  return ((sampleA * primaryAmount) + (sampleB * morphAmount)) >> 7;
}

// Applies the selected low-pass strength to the live voice mix.
static inline __attribute__((always_inline)) int16_t filterPlayMix(int16_t inputQ7)
{
  const uint8_t level = playFilterAmount;
  if (level == 0) {
    playFilterStateQ7 = inputQ7;
    return inputQ7;
  }

  int16_t stateQ7 = playFilterStateQ7;
  if (level == 1) {
    stateQ7 = inputQ7 - (inputQ7 >> 3) + (stateQ7 >> 3);
  } else if (level == 2) {
    stateQ7 = inputQ7 - (inputQ7 >> 2) + (stateQ7 >> 2);
  } else {
    const uint8_t shift = level - 2;
    stateQ7 += (inputQ7 >> shift) - (stateQ7 >> shift);
  }
  playFilterStateQ7 = stateQ7;
  return stateQ7;
}

// Computes value / 64 without slow wide shifts on AVR.
static inline __attribute__((always_inline)) uint16_t modulationCoarse(uint16_t value)
{
  const uint8_t high = value >> 8;
  const uint8_t low = (uint8_t)value / 64U;
  return ((uint16_t)high << 2) | low;
}

// Mixes one audio frame and updates time-sliced synthesis state.
// O2 avoids slow size-optimized shift loops in this ISR.
ISR(TIMER2_COMPA_vect, __attribute__((optimize("O2"))))
{
  const uint8_t voiceIndex = (divider + 1) & 0x03;
  divider = voiceIndex;

  int16_t mixQ7;

  if (droneMode) {
    const uint8_t rampVoice = (droneRampVoice + 1) & 0x07;
    droneRampVoice = rampVoice;
    uint8_t amplitude = droneAmp[rampVoice];
    const uint8_t target = droneTargetAmp[rampVoice];
    if (amplitude < target) {
      amplitude = (target - amplitude > 15) ? amplitude + 15 : target;
    } else if (amplitude > target) {
      amplitude = (amplitude - target > 15) ? amplitude - 15 : target;
    }
    droneAmp[rampVoice] = amplitude;

    const uint8_t pair = (droneMixPair + 1) & 0x03;
    droneMixPair = pair;
    int16_t pairMixQ7 = 0;
    const uint8_t firstVoice = pair << 1;
    for (uint8_t voice = firstVoice; voice < firstVoice + 2; ++voice) {
      amplitude = droneAmp[voice];
      if (amplitude != 0) {
        const uint16_t index =
          (dronePCW[voice] += droneFTW[voice] << 2) >> SYNTH_WAVETABLE_SHIFT;
        const int16_t sample = readSigned9Sample(droneWavs[voice], index);
        pairMixQ7 += scaleSigned9Sample(sample, amplitude, 4);
      }
    }

    const uint32_t lfoStep = droneLfoFTW[pair];
    if (lfoStep != 0) {
      const uint32_t lfoPhase = droneLfoPCW[pair] + lfoStep;
      droneLfoPCW[pair] = lfoPhase;
      const uint8_t trianglePhase = (uint8_t)(lfoPhase >> 24) >> 1;
      const uint8_t gain = (trianglePhase & 0x40) ? 127 - trianglePhase : trianglePhase;
      pairMixQ7 = (((pairMixQ7 >> 4) * gain) >> 2);
    }
    dronePairMixQ7[pair] = pairMixQ7;
    mixQ7 = dronePairMixQ7[0] + dronePairMixQ7[1] +
      dronePairMixQ7[2] + dronePairMixQ7[3];
  } else {
    const uint16_t envelopePhase = EPCW[voiceIndex];
    if (envelopePhase & 0x8000) {
      AMP[voiceIndex] = 0;
    } else {
      const uint16_t nextEnvelopePhase = envelopePhase + EFTW[voiceIndex];
      EPCW[voiceIndex] = nextEnvelopePhase;
      const uint8_t envelopeIndex = nextEnvelopePhase >> 8;
      AMP[voiceIndex] = (envelopeIndex & 0x80) ? 0 : pgm_read_byte(envs[voiceIndex] + envelopeIndex);
    }

    bool updatePlayLfos = false;
    if (voiceIndex == 0) {
      playLfoDivider ^= 1;
      updatePlayLfos = playLfoDivider == 0;
    }
    if (updatePlayLfos) {
      const uint32_t filterLfoStep = playFilterLfoFTW;
      if (filterLfoStep != 0) {
        playFilterLfoPCW += filterLfoStep;
        const uint8_t trianglePhase = ((uint8_t*)&playFilterLfoPCW)[3];
        const uint8_t triangleAmount = (trianglePhase & 0x80) ?
          (uint8_t)(256U - trianglePhase) : trianglePhase;
        playFilterAmount = ((uint16_t)playFilterDepth * triangleAmount + 64) >> 7;
      }
    }

    int16_t sourceZeroQ7 = 0;
    if (drumActive) {
      const uint32_t samplePhase = drumSamplePhase;
      const uint16_t envelope = drumEnvelope;
      const uint16_t sampleIndex = samplePhase >> 16;
      if (sampleIndex >= drumSampleLength || envelope == 0) {
        drumEnvelope = 0;
        drumActive = false;
      } else {
        const int16_t sample = readPackedSigned9Sample(drumSampleAddress, sampleIndex);
        drumSamplePhase = samplePhase + drumSampleStep;
        const uint8_t gain = envelope >> 8;
        sourceZeroQ7 = scaleSigned9Sample(sample, gain, 3);
        const uint16_t envelopeStep = drumEnvelopeStep;
        drumEnvelope = envelope <= envelopeStep ? 0 : envelope - envelopeStep;
      }
    } else {
      sourceZeroQ7 = scaleSigned9Sample(nextVoiceSample(0), AMP[0], 3);
    }

    int16_t liveMixQ7;
    if (playMorphActive) {
      if (updatePlayLfos) {
        const uint32_t morphLfoStep = playMorphLfoFTW;
        playMorphLfoPCW += morphLfoStep;
        const uint8_t trianglePhase = ((uint8_t*)&playMorphLfoPCW)[3];
        playMorphAmount = (trianglePhase & 0x80) ?
          (uint8_t)(256U - trianglePhase) : trianglePhase;
      }
      const uint16_t secondaryWave = playMorphWavPtr;
      const uint8_t morphAmount = playMorphAmount;
      liveMixQ7 =
        scaleSigned9Sample(nextMorphedVoiceSample(1, secondaryWave, morphAmount), AMP[1], 3) +
        scaleSigned9Sample(nextMorphedVoiceSample(2, secondaryWave, morphAmount), AMP[2], 3) +
        scaleSigned9Sample(nextMorphedVoiceSample(3, secondaryWave, morphAmount), AMP[3], 3);
    } else {
      liveMixQ7 =
        scaleSigned9Sample(nextVoiceSample(1), AMP[1], 3) +
        scaleSigned9Sample(nextVoiceSample(2), AMP[2], 3) +
        scaleSigned9Sample(nextVoiceSample(3), AMP[3], 3);
    }
    mixQ7 = sourceZeroQ7 + filterPlayMix(liveMixQ7);
  }

  bool updateOutput = true;
  if (sampleHoldFrames > 1) {
    if (sampleHoldCounter == 0) {
      sampleHoldCounter = sampleHoldFrames - 1;
    } else {
      --sampleHoldCounter;
      updateOutput = false;
    }
  }

  uint16_t pwmSample = heldPwmSample;
  const bool shapeOutput = droneMode ? !droneNoiseMask : !noiseVoiceMask;

  if (updateOutput) {
    if (shapeOutput) {
      const uint8_t feedback = (pwmDitherLfsr & 0x01) ? 0xB8 : 0x00;
      const uint8_t lfsr = (pwmDitherLfsr >> 1) ^ feedback;
      pwmDitherLfsr = lfsr;
      const uint8_t highNibble = lfsr / 16U;
      const int16_t dither =
        ((int8_t)(lfsr & 0x0F) - (int8_t)highNibble) << 2;
      mixQ7 += pwmQuantizationError + dither;
    } else {
      pwmQuantizationError = 0;
    }

    const int16_t quantizedMix = (mixQ7 + 64) >> 7;
    pwmQuantizationError = shapeOutput ? mixQ7 - (quantizedMix << 7) : 0;

    int16_t nextPwmSample = 256 + quantizedMix;
    if (nextPwmSample < 0) {
      nextPwmSample = 0;
    } else if (nextPwmSample > 511) {
      nextPwmSample = 511;
    }

    pwmSample = (uint16_t)nextPwmSample;
    heldPwmSample = pwmSample;
  } else if (!shapeOutput) {
    pwmQuantizationError = 0;
  }

  OCR1A = OCR1B = pwmSample;

  if (!droneMode && (!drumActive || voiceIndex != 0)) {
    const uint16_t pitch = PITCH[voiceIndex];
    FTW[voiceIndex] = pitch + (int16_t)(((modulationCoarse(pitch) *
      modulationCoarse(EPCW[voiceIndex])) / 128) * MOD[voiceIndex]);
  }
}

class synth
{
  private:
    uint16_t wavePointer(uint8_t wave) const
    {
      switch (wave)
      {
        case TRIANGLE:
          return (uint16_t)TriangleTable;
        case SQUARE:
          return (uint16_t)SquareTable;
        case SAW:
          return (uint16_t)SawTable;
        case RAMP:
          return (uint16_t)RampTable;
        case NOISE:
          return (uint16_t)NoiseTable;
        case SINE:
        default:
          return (uint16_t)SinTable;
      }
    }

    bool validVoice(uint8_t voice) const
    {
      return voice < SYNTH_VOICE_COUNT;
    }

    void configureTimer2()
    {
      TCCR2A = (1 << WGM21);
      TCCR2B = (1 << CS21);
      TCNT2 = 0;
      OCR2A = (uint8_t)((F_CPU / (8UL * FS)) - 1);
    }

    void enableTimer2Interrupt()
    {
      SET(TIMSK2, OCIE2A);
    }

    void configureTimer1Pwm()
    {
      TCCR1A = (1 << WGM11);
      TCCR1B = (1 << WGM12) | (1 << CS10);
      TCCR1C = 0x00;
      TCNT1 = 0;
      OCR1A = OCR1B = 256;
    }

    void configureChaOutput()
    {
      configureTimer1Pwm();
      SET(TCCR1A, COM1A1);
      CLR(TCCR1A, COM1A0);
      SET(DDRB, 1);
    }

    void configureChbOutput()
    {
      configureTimer1Pwm();
      SET(TCCR1A, COM1B1);
      CLR(TCCR1A, COM1B0);
      SET(DDRB, 2);
    }

    void configureDifferentialOutput()
    {
      configureTimer1Pwm();
      SET(TCCR1A, COM1A1);
      CLR(TCCR1A, COM1A0);
      SET(TCCR1A, COM1B1);
      SET(TCCR1A, COM1B0);
      SET(DDRB, 1);
      SET(DDRB, 2);
    }

  public:
    synth()
    {
    }

    // Starts the audio timers with the selected PWM output mode.
    void begin(uint8_t outputMode)
    {
      cli();
      configureTimer2();

      switch (outputMode)
      {
        case DIFF:
          configureDifferentialOutput();
          break;
        case CHB:
          configureChbOutput();
          break;
        case CHA:
        default:
          configureChaOutput();
          break;
      }

      enableTimer2Interrupt();
      sei();
    }

    // Configures all parameters of one melodic voice.
    void setupVoice(uint8_t voice, uint8_t wave, uint8_t pitch, uint8_t env, uint8_t length, uint8_t mod)
    {
      setWave(voice, wave);
      setPitch(voice, pitch);
      setEnvelope(voice, env);
      setLength(voice, length);
      setMod(voice, mod);
    }

    void setWave(uint8_t voice, uint8_t wave)
    {
      if (!validVoice(voice)) {
        return;
      }

      const uint16_t selectedWave = wavePointer(wave);

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        wavs[voice] = selectedWave;
        if (wave == NOISE) {
          noiseVoiceMask |= (1 << voice);
        } else {
          noiseVoiceMask &= ~(1 << voice);
        }
      }
    }

    // Configures LFO-driven waveform morphing for the live voices.
    void configurePlayMorph(
      uint8_t primaryWave, uint8_t secondaryWave, uint16_t frequencyTenths, bool secondaryEnabled)
    {
      const uint16_t primaryPointer = wavePointer(primaryWave);
      const uint16_t secondaryPointer = wavePointer(secondaryWave);
      const uint32_t lfoStep = frequencyTenths == 0 ? 0 :
        (uint32_t)frequencyTenths * 171799UL;
      const bool active = secondaryEnabled && lfoStep != 0;
      const uint16_t outputPointer = secondaryEnabled && !active ? secondaryPointer : primaryPointer;
      const bool noisy = active ?
        (primaryWave == NOISE || secondaryWave == NOISE) :
        ((secondaryEnabled ? secondaryWave : primaryWave) == NOISE);

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        const bool wasActive = playMorphActive;
        for (uint8_t voice = 1; voice < SYNTH_VOICE_COUNT; ++voice) {
          wavs[voice] = outputPointer;
        }
        noiseVoiceMask = (noiseVoiceMask & 0x01) | (noisy ? 0x0E : 0x00);
        playMorphWavPtr = secondaryPointer;
        playMorphLfoFTW = lfoStep;
        if (active && !wasActive) {
          playMorphLfoPCW = 0x80000000UL;
          playMorphAmount = 128;
        } else if (!active) {
          playMorphAmount = 128;
        }
        playMorphActive = active;
      }
    }

    void disablePlayMorph()
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        playMorphActive = false;
        playMorphLfoFTW = 0;
        playMorphAmount = 128;
      }
    }

    // Configures the live low-pass filter and its optional LFO.
    void configurePlayFilter(uint8_t level, uint16_t frequencyTenths)
    {
      if (level > 7) {
        level = 7;
      }
      const uint32_t lfoStep = level == 0 || frequencyTenths == 0 ? 0 :
        (uint32_t)frequencyTenths * 171799UL;

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        const bool wasActive = playFilterLfoFTW != 0;
        playFilterDepth = level;
        playFilterLfoFTW = lfoStep;
        if (lfoStep == 0) {
          playFilterAmount = level;
        } else if (!wasActive) {
          playFilterLfoPCW = 0x80000000UL;
          playFilterAmount = level;
        }
      }
    }

    void disablePlayFilter()
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        playFilterDepth = 0;
        playFilterAmount = 0;
        playFilterLfoFTW = 0;
      }
    }

    void setPitch(uint8_t voice, uint8_t midiNote)
    {
      if (!validVoice(voice)) {
        return;
      }

      if (midiNote > SYNTH_MIDI_NOTE_MAX) {
        midiNote = SYNTH_MIDI_NOTE_MAX;
      }

      const uint16_t pitch = pgm_read_word(&PITCHS[midiNote]);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        PITCH[voice] = pitch;
      }
    }

    void setPitchWord(uint8_t voice, uint16_t pitch)
    {
      if (!validVoice(voice)) {
        return;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        PITCH[voice] = pitch;
      }
    }

    void setEnvelope(uint8_t voice, uint8_t env)
    {
      if (!validVoice(voice)) {
        return;
      }

      uint16_t selectedEnvelope = (uint16_t)Env0;
      switch (env)
      {
        case ENVELOPE1:
          selectedEnvelope = (uint16_t)Env1;
          break;
        case ENVELOPE2:
          selectedEnvelope = (uint16_t)Env2;
          break;
        case ENVELOPE3:
          selectedEnvelope = (uint16_t)Env3;
          break;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        envs[voice] = selectedEnvelope;
      }
    }

    void setLength(uint8_t voice, uint8_t length)
    {
      if (!validVoice(voice)) {
        return;
      }

      if (length > SYNTH_LENGTH_MAX) {
        length = SYNTH_LENGTH_MAX;
      }

      const uint16_t envelopeSpeed = pgm_read_word(&EFTWS[length]);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        EFTW[voice] = envelopeSpeed;
      }
    }

    // Sets bipolar modulation with 64 as the neutral value.
    void setMod(uint8_t voice, uint8_t mod)
    {
      if (!validVoice(voice)) {
        return;
      }

      if (mod > 127) {
        mod = 127;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        MOD[voice] = (int16_t)mod - 64;
      }
    }

    void setSampleHold(uint8_t frames)
    {
      if (frames < 1) {
        frames = 1;
      } else if (frames > 16) {
        frames = 16;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        sampleHoldFrames = frames;
        sampleHoldCounter = 0;
      }
    }

    void trigger(uint8_t voice)
    {
      if (!validVoice(voice)) {
        return;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        EPCW[voice] = 0;
        FTW[voice] = PITCH[voice];
      }
    }

    void stopVoice(uint8_t voice)
    {
      if (!validVoice(voice)) {
        return;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        EPCW[voice] = 0x8000;
        AMP[voice] = 0;
        FTW[voice] = 0;
      }
    }

    // Starts packed sample playback with tone and decay scaling.
    void triggerDrum(uint8_t type, uint8_t tone, uint8_t decay)
    {
      if (type >= DRUM_SAMPLE_COUNT) type = DRUM_SAMPLE_COUNT - 1;
      if (tone > 127) tone = 127;
      if (decay > 127) decay = 127;

      uint16_t sampleAddress;
      uint16_t sampleLength;
      if (type == 0) {
        sampleAddress = (uint16_t)kickSample;
        sampleLength = kickSampleLength;
      } else if (type == 1) {
        sampleAddress = (uint16_t)snareSample;
        sampleLength = snareSampleLength;
      } else {
        sampleAddress = (uint16_t)hihatSample;
        sampleLength = hihatSampleLength;
      }

      const uint32_t sampleStep = 49152UL +
        (((uint32_t)tone * 32768UL + 63UL) / 127UL);
      const uint32_t playbackFrames =
        (((uint32_t)sampleLength << 16) + sampleStep - 1) / sampleStep;
      const uint16_t decayScale = 32U +
        (((uint16_t)decay * 96U + 63U) / 127U);
      uint32_t durationFrames = (playbackFrames * decayScale) >> 7;
      if (durationFrames == 0) durationFrames = 1;
      uint16_t envelopeStep =
        (65535UL + durationFrames - 1) / durationFrames;
      if (envelopeStep == 0) envelopeStep = 1;

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        drumActive = false;
        EPCW[0] = 0x8000;
        AMP[0] = 0;
        FTW[0] = 0;
        drumSampleAddress = sampleAddress;
        drumSampleLength = sampleLength;
        drumSamplePhase = 0;
        drumSampleStep = sampleStep;
        drumEnvelope = 65535;
        drumEnvelopeStep = envelopeStep;
        drumActive = sampleLength != 0;
      }
    }

    void stopDrum()
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        drumActive = false;
        drumSamplePhase = 0;
        drumEnvelope = 0;
      }
    }

    void setDroneMode(bool enabled)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        droneMode = enabled;
        droneMixPair = 0;
        for (uint8_t pair = 0; pair < SYNTH_DRONE_COUNT / 2; ++pair) {
          dronePairMixQ7[pair] = 0;
          droneLfoPCW[pair] = 0;
        }
        sampleHoldCounter = 0;
        heldPwmSample = 256;
        pwmQuantizationError = 0;
      }
    }

    void setDroneWave(uint8_t voice, uint8_t wave)
    {
      if (voice >= SYNTH_DRONE_COUNT) {
        return;
      }

      const uint16_t selectedWave = wavePointer(wave);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        droneWavs[voice] = selectedWave;
        if (wave == NOISE) {
          droneNoiseMask |= (1 << voice);
        } else {
          droneNoiseMask &= ~(1 << voice);
        }
      }
    }

    void setDronePitchWord(uint8_t voice, uint16_t pitch)
    {
      if (voice >= SYNTH_DRONE_COUNT) {
        return;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        droneFTW[voice] = pitch;
      }
    }

    void setDroneLfoRate(uint8_t pair, uint16_t frequencyTenths)
    {
      if (pair >= SYNTH_DRONE_COUNT / 2) {
        return;
      }

      const uint32_t step = frequencyTenths == 0 ? 0 :
        (uint32_t)frequencyTenths * 85899UL;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (step == 0 || droneLfoFTW[pair] == 0) {
          droneLfoPCW[pair] = 0;
        }
        droneLfoFTW[pair] = step;
      }
    }

    void setDroneGate(uint8_t voice, bool enabled)
    {
      if (voice >= SYNTH_DRONE_COUNT) {
        return;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (enabled && droneTargetAmp[voice] == 0) {
          dronePCW[voice] = 0;
        }
        droneTargetAmp[voice] = enabled ? 255 : 0;
      }
    }

    void stopAllDrones()
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        for (uint8_t voice = 0; voice < SYNTH_DRONE_COUNT; ++voice) {
          droneTargetAmp[voice] = 0;
          droneAmp[voice] = 0;
          dronePCW[voice] = 0;
        }
        for (uint8_t pair = 0; pair < SYNTH_DRONE_COUNT / 2; ++pair) {
          dronePairMixQ7[pair] = 0;
          droneLfoPCW[pair] = 0;
        }
      }
    }

};

#endif
