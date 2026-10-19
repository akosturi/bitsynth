#ifndef _SYNTH
#define _SYNTH
//*************************************************************************************
//  Arduino synth V4.1
//  Optimized audio driver, modulation engine, envelope engine.
//
//  Dzl/Illutron 2014
//*************************************************************************************

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

#define SET(x,y) (x |= (1 << y))
#define CLR(x,y) (x &= (~(1 << y)))
#define CHK(x,y) (x & (1 << y))
#define TOG(x,y) (x ^= (1 << y))

const uint8_t SYNTH_VOICE_COUNT = 4;
const uint8_t SYNTH_DRONE_COUNT = 8;
const uint8_t SYNTH_MIDI_NOTE_MAX = 127;
const uint8_t SYNTH_LENGTH_MAX = 127;
const uint8_t SYNTH_WAVETABLE_INDEX_BITS = 9;
const uint8_t SYNTH_WAVETABLE_SHIFT = 16 - SYNTH_WAVETABLE_INDEX_BITS;

volatile uint16_t PCW[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
volatile uint16_t FTW[SYNTH_VOICE_COUNT] = {1000, 200, 300, 400};
volatile uint8_t AMP[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
volatile uint16_t PITCH[SYNTH_VOICE_COUNT] = {500, 500, 500, 500};
volatile int16_t MOD[SYNTH_VOICE_COUNT] = {0, 0, 0, 0};
volatile uint16_t wavs[SYNTH_VOICE_COUNT] = {
  (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable, (uint16_t)SinTable
};
volatile uint16_t envs[SYNTH_VOICE_COUNT] = {
  (uint16_t)Env0, (uint16_t)Env0, (uint16_t)Env0, (uint16_t)Env0
};
volatile uint16_t EPCW[SYNTH_VOICE_COUNT] = {0x8000, 0x8000, 0x8000, 0x8000};
volatile uint16_t EFTW[SYNTH_VOICE_COUNT] = {10, 10, 10, 10};
volatile uint8_t divider = 4;
volatile uint16_t tim = 0;
volatile uint8_t tik = 0;
volatile uint8_t output_mode = CHA;
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
volatile bool drumNoiseOutput = false;
volatile uint8_t drumType = 0;
volatile uint8_t drumFilterShift = 2;
volatile uint16_t drumEnvelope = 0;
volatile uint16_t drumEnvelopeStep = 1;
volatile uint16_t drumAge = 0;
volatile uint16_t drumPhaseA = 0;
volatile uint16_t drumPhaseB = 0;
volatile uint16_t drumPhaseC = 0;
volatile uint16_t drumPhaseD = 0;
volatile uint16_t drumFtwA = 0;
volatile uint16_t drumFtwB = 0;
volatile uint16_t drumFtwC = 0;
volatile uint16_t drumFtwD = 0;
volatile uint16_t drumFtwTarget = 0;
volatile uint16_t drumNoiseLfsr = 0xACE1;
volatile int16_t drumNoiseLow = 0;

static inline __attribute__((always_inline)) int8_t nextVoiceSample(uint8_t voice)
{
  const uint16_t phase = PCW[voice] + FTW[voice];
  PCW[voice] = phase;
  return (int8_t)pgm_read_byte(wavs[voice] + (phase >> SYNTH_WAVETABLE_SHIFT));
}

static inline __attribute__((always_inline)) int8_t nextMorphedVoiceSample(
  uint8_t voice, uint16_t secondaryWave, uint8_t morphAmount)
{
  const uint16_t phase = PCW[voice] + FTW[voice];
  PCW[voice] = phase;
  const uint16_t index = phase >> SYNTH_WAVETABLE_SHIFT;
  const int8_t sampleA = (int8_t)pgm_read_byte(wavs[voice] + index);
  const int8_t sampleB = (int8_t)pgm_read_byte(secondaryWave + index);
  const int16_t difference = (int16_t)sampleB - sampleA;
  return sampleA + (((int16_t)(difference * morphAmount)) >> 7);
}

//*********************************************************************************************
//  Audio driver interrupt
//*********************************************************************************************

ISR(TIMER2_COMPA_vect)
{
  //-------------------------------
  // Time division
  //-------------------------------
  divider++;
  if (!(divider &= 0x03)) {
    tik = 1;
  }

  int16_t mixQ7;

  if (droneMode) {
    droneRampVoice = (droneRampVoice + 1) & 0x07;
    uint8_t amplitude = droneAmp[droneRampVoice];
    const uint8_t target = droneTargetAmp[droneRampVoice];
    if (amplitude < target) {
      amplitude = (target - amplitude > 15) ? amplitude + 15 : target;
    } else if (amplitude > target) {
      amplitude = (amplitude - target > 15) ? amplitude - 15 : target;
    }
    droneAmp[droneRampVoice] = amplitude;

    droneMixPair = (droneMixPair + 1) & 0x03;
    const uint8_t pair = droneMixPair;
    int16_t pairMixQ7 = 0;
    const uint8_t firstVoice = pair << 1;
    for (uint8_t voice = firstVoice; voice < firstVoice + 2; ++voice) {
      amplitude = droneAmp[voice];
      if (amplitude != 0) {
        const int8_t sample = (int8_t)pgm_read_byte(
          droneWavs[voice] + ((dronePCW[voice] += droneFTW[voice] << 2) >> SYNTH_WAVETABLE_SHIFT));
        pairMixQ7 += ((int16_t)sample * amplitude) >> 3;
      }
    }

    const uint32_t lfoStep = droneLfoFTW[pair];
    if (lfoStep != 0) {
      droneLfoPCW[pair] += lfoStep;
      const uint8_t trianglePhase = droneLfoPCW[pair] >> 25;
      const uint8_t gain = (trianglePhase & 0x40) ? 127 - trianglePhase : trianglePhase;
      pairMixQ7 = (((pairMixQ7 >> 4) * gain) >> 2);
    }
    dronePairMixQ7[pair] = pairMixQ7;
    mixQ7 = dronePairMixQ7[0] + dronePairMixQ7[1] +
      dronePairMixQ7[2] + dronePairMixQ7[3];
  } else {
    //-------------------------------
    // Volume envelope generator
    //-------------------------------

    if (((uint8_t*)&EPCW[divider])[1] & 0x80) {
      AMP[divider] = 0;
    } else {
      EPCW[divider] += EFTW[divider];
      const uint8_t envelopeIndex = ((uint8_t*)&EPCW[divider])[1];
      AMP[divider] = (envelopeIndex & 0x80) ? 0 : pgm_read_byte(envs[divider] + envelopeIndex);
    }

    //-------------------------------
    //  Synthesizer/audio mixer
    //-------------------------------

    int16_t sourceZeroQ7 = 0;
    if (drumActive) {
      int16_t drumSample = 0;
      if (drumEnvelope <= drumEnvelopeStep) {
        drumEnvelope = 0;
        drumActive = false;
        drumNoiseOutput = false;
      } else {
        drumEnvelope -= drumEnvelopeStep;
        ++drumAge;
        const uint16_t lfsr = drumNoiseLfsr;
        drumNoiseLfsr = (lfsr >> 1) ^
          (((uint16_t)-(int16_t)(lfsr & 1)) & 0xB400);
        const int8_t noiseSample = (int8_t)drumNoiseLfsr;

        if (drumType == 0 || drumType == 2) {
          if (drumFtwA > drumFtwTarget) {
            const uint16_t difference = drumFtwA - drumFtwTarget;
            uint16_t drop = difference >> (drumType == 0 ? 6 : 7);
            if (drop == 0) drop = 1;
            drumFtwA -= drop;
          }
          const int8_t body = (int8_t)pgm_read_byte(
            SinTable + ((drumPhaseA += drumFtwA) >> SYNTH_WAVETABLE_SHIFT));
          if (drumType == 0 && drumAge < 48) drumSample = ((int16_t)body * 3 >> 2) + (noiseSample >> 2);
          else drumSample = body;
        } else if (drumType == 1) {
          const int8_t bodyA = (int8_t)pgm_read_byte(
            SinTable + ((drumPhaseA += drumFtwA) >> SYNTH_WAVETABLE_SHIFT));
          const int8_t bodyB = (int8_t)pgm_read_byte(
            SinTable + ((drumPhaseB += drumFtwB) >> SYNTH_WAVETABLE_SHIFT));
          drumNoiseLow += ((int16_t)noiseSample - drumNoiseLow) >> drumFilterShift;
          const int16_t highNoise = (int16_t)noiseSample - drumNoiseLow;
          drumSample = ((int16_t)bodyA + bodyB) >> 2;
          drumSample += highNoise >> 2;
        } else {
          int16_t metal = ((drumPhaseA += drumFtwA) & 0x8000) ? 20 : -20;
          metal += ((drumPhaseB += drumFtwB) & 0x8000) ? 20 : -20;
          metal += ((drumPhaseC += drumFtwC) & 0x8000) ? 20 : -20;
          metal += ((drumPhaseD += drumFtwD) & 0x8000) ? 20 : -20;
          metal += noiseSample >> 2;
          drumNoiseLow += (metal - drumNoiseLow) >> drumFilterShift;
          drumSample = (metal - drumNoiseLow) >> 1;
        }

        const uint8_t linearGain = drumEnvelope >> 8;
        const uint8_t gain = ((uint16_t)linearGain * linearGain) >> 8;
        drumSample = (drumSample * gain) >> 2;
      }

      sourceZeroQ7 = drumSample;
    } else {
      sourceZeroQ7 = ((int16_t)nextVoiceSample(0) * AMP[0]) >> 2;
    }

    if (playMorphActive) {
      if (divider == 0) {
        playMorphLfoPCW += playMorphLfoFTW;
        const uint8_t trianglePhase = ((uint8_t*)&playMorphLfoPCW)[3];
        playMorphAmount = (trianglePhase & 0x80) ?
          (uint8_t)(256U - trianglePhase) : trianglePhase;
      }
      const uint16_t secondaryWave = playMorphWavPtr;
      const uint8_t morphAmount = playMorphAmount;
      mixQ7 = sourceZeroQ7 +
        (((int16_t)nextMorphedVoiceSample(1, secondaryWave, morphAmount) * AMP[1]) >> 2) +
        (((int16_t)nextMorphedVoiceSample(2, secondaryWave, morphAmount) * AMP[2]) >> 2) +
        (((int16_t)nextMorphedVoiceSample(3, secondaryWave, morphAmount) * AMP[3]) >> 2);
    } else {
      mixQ7 = sourceZeroQ7 +
        (((int16_t)nextVoiceSample(1) * AMP[1]) >> 2) +
        (((int16_t)nextVoiceSample(2) * AMP[2]) >> 2) +
        (((int16_t)nextVoiceSample(3) * AMP[3]) >> 2);
    }
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
  const bool shapeOutput = droneMode ? !droneNoiseMask : !(noiseVoiceMask || (drumActive && drumNoiseOutput));

  if (updateOutput) {
    if (shapeOutput) {
      const uint8_t feedback = (pwmDitherLfsr & 0x01) ? 0xB8 : 0x00;
      pwmDitherLfsr = (pwmDitherLfsr >> 1) ^ feedback;
      const int16_t dither =
        ((int8_t)(pwmDitherLfsr & 0x0F) - (int8_t)(pwmDitherLfsr >> 4)) << 2;
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

  //************************************************
  //  Modulation engine
  //************************************************
  if (!droneMode && (!drumActive || divider != 0)) {
    FTW[divider] = PITCH[divider] + (int16_t)((((PITCH[divider] >> 6) * (EPCW[divider] >> 6)) / 128) * MOD[divider]);
  }
  tim++;
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

    uint16_t frequencyWord(uint16_t frequency) const
    {
      return ((uint32_t)frequency * 65535UL + FS / 2) / FS;
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
      output_mode = CHA;
      configureTimer1Pwm();
      SET(TCCR1A, COM1A1);
      CLR(TCCR1A, COM1A0);
      SET(DDRB, 1);
    }

    void configureChbOutput()
    {
      output_mode = CHB;
      configureTimer1Pwm();
      SET(TCCR1A, COM1B1);
      CLR(TCCR1A, COM1B0);
      SET(DDRB, 2);
    }

    void configureDifferentialOutput()
    {
      output_mode = DIFF;
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

    //*********************************************************************
    //  Startup default
    //*********************************************************************

    void begin()
    {
      begin(CHA);
    }

    //*********************************************************************
    //  Startup selecting various output modes
    //*********************************************************************

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

    //*********************************************************************
    //  Timing/sequencing functions
    //*********************************************************************

    uint8_t synthTick()
    {
      if (tik)
      {
        tik = 0;
        return 1;
      }
      return 0;
    }

    uint8_t voiceFree(uint8_t voice)
    {
      if (!validVoice(voice)) {
        return 0;
      }

      if (!(((uint8_t*)&EPCW[voice])[1] & 0x80)) {
        return 0;
      }

      return 1;
    }

    //*********************************************************************
    //  Setup voice parameters in MIDI range
    //  voice[0-3], wave[0-5], pitch[0-127], envelope[0-3],
    //  length[0-127], mod[0-127:64=no mod]
    //*********************************************************************

    void setupVoice(uint8_t voice, uint8_t wave, uint8_t pitch, uint8_t env, uint8_t length, uint8_t mod)
    {
      setWave(voice, wave);
      setPitch(voice, pitch);
      setEnvelope(voice, env);
      setLength(voice, length);
      setMod(voice, mod);
    }

    void setupVoice(uint8_t voice, uint8_t wave, uint8_t pitch, uint8_t env, uint8_t length, uint8_t mod, uint16_t)
    {
      setupVoice(voice, wave, pitch, env, length, mod);
    }

    //*********************************************************************
    //  Setup wave [0-5]
    //*********************************************************************

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

    void configurePlayMorph(
      uint8_t primaryWave, uint8_t secondaryWave, uint16_t frequencyTenths, bool secondaryEnabled)
    {
      const uint16_t primaryPointer = wavePointer(primaryWave);
      const uint16_t secondaryPointer = wavePointer(secondaryWave);
      const uint32_t lfoStep = frequencyTenths == 0 ? 0 :
        (uint32_t)frequencyTenths * 85899UL;
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

    //*********************************************************************
    //  Setup Pitch [0-127]
    //*********************************************************************

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

    //*********************************************************************
    //  Setup Envelope [0-3]
    //*********************************************************************

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

    //*********************************************************************
    //  Setup Length [0-127]
    //*********************************************************************

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

    //*********************************************************************
    //  Legacy compatibility. This synth engine has no audio filter stage.
    //*********************************************************************

    void setFilter(uint8_t, uint16_t)
    {
    }

    //*********************************************************************
    //  Setup mod [0-127:64=no mod]
    //*********************************************************************

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

    //*********************************************************************
    //  Midi trigger
    //*********************************************************************

    void mTrigger(uint8_t voice, uint8_t midiNote)
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
        EPCW[voice] = 0;
        FTW[voice] = pitch;
      }
    }

    //*********************************************************************
    //  Set frequency direct
    //*********************************************************************

    void setFrequency(uint8_t voice, float frequency)
    {
      if (!validVoice(voice)) {
        return;
      }

      const uint16_t pitch = (uint16_t)(frequency / (FS / 65535.0));
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        PITCH[voice] = pitch;
      }
    }

    //*********************************************************************
    //  Set time
    //*********************************************************************

    void setTime(uint8_t voice, float seconds)
    {
      if (!validVoice(voice) || seconds <= 0.0) {
        return;
      }

      const uint16_t envelopeSpeed = (uint16_t)((1.0 / seconds) / (FS / (32767.5 * 10.0)));
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        EFTW[voice] = envelopeSpeed;
      }
    }

    //*********************************************************************
    //  Simple trigger
    //*********************************************************************

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

    void triggerDrum(uint8_t type, uint8_t tone, uint8_t decay)
    {
      if (type > 3) type = 3;
      if (tone > 127) tone = 127;
      if (decay > 127) decay = 127;

      uint16_t frequencyA = 0;
      uint16_t frequencyB = 0;
      uint16_t frequencyC = 0;
      uint16_t frequencyD = 0;
      uint16_t targetFrequency = 0;
      uint16_t durationMs = 100;
      uint8_t filterShift = 2;
      bool noisy = false;

      if (type == 0) {
        targetFrequency = 45 + ((uint16_t)tone * 30) / 127;
        frequencyA = 145 + ((uint16_t)tone * 95) / 127;
        durationMs = 90 + ((uint32_t)decay * 410) / 127;
      } else if (type == 1) {
        frequencyA = 165 + ((uint16_t)tone * 60) / 127;
        frequencyB = 290 + ((uint16_t)tone * 120) / 127;
        targetFrequency = frequencyA;
        durationMs = 70 + ((uint32_t)decay * 330) / 127;
        filterShift = tone < 43 ? 4 : (tone < 86 ? 3 : 2);
        noisy = true;
      } else if (type == 2) {
        targetFrequency = 70 + ((uint16_t)tone * 80) / 127;
        frequencyA = targetFrequency + 55 + ((uint16_t)tone * 40) / 127;
        durationMs = 100 + ((uint32_t)decay * 500) / 127;
      } else {
        const uint16_t scale = 75 + ((uint16_t)tone * 50) / 127;
        frequencyA = (307UL * scale) / 100;
        frequencyB = (431UL * scale) / 100;
        frequencyC = (619UL * scale) / 100;
        frequencyD = (887UL * scale) / 100;
        targetFrequency = frequencyA;
        durationMs = 18 + ((uint32_t)decay * 142) / 127;
        filterShift = tone < 64 ? 2 : 1;
        noisy = true;
      }

      const uint32_t durationSamples = (uint32_t)durationMs * (FS / 1000UL);
      uint16_t envelopeStep = 65535UL / durationSamples;
      if (envelopeStep == 0) envelopeStep = 1;
      const uint16_t ftwA = frequencyWord(frequencyA);
      const uint16_t ftwB = frequencyWord(frequencyB);
      const uint16_t ftwC = frequencyWord(frequencyC);
      const uint16_t ftwD = frequencyWord(frequencyD);
      const uint16_t ftwTarget = frequencyWord(targetFrequency);

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        drumActive = false;
        EPCW[0] = 0x8000;
        AMP[0] = 0;
        FTW[0] = 0;
        drumType = type;
        drumFilterShift = filterShift;
        drumEnvelope = 65535;
        drumEnvelopeStep = envelopeStep;
        drumAge = 0;
        drumPhaseA = 0;
        drumPhaseB = 0;
        drumPhaseC = 0;
        drumPhaseD = 0;
        drumFtwA = ftwA;
        drumFtwB = ftwB;
        drumFtwC = ftwC;
        drumFtwD = ftwD;
        drumFtwTarget = ftwTarget;
        drumNoiseLow = 0;
        drumNoiseLfsr ^= tim | 1;
        if (drumNoiseLfsr == 0) drumNoiseLfsr = 0xACE1;
        drumNoiseOutput = noisy;
        drumActive = true;
      }
    }

    void stopDrum()
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        drumActive = false;
        drumNoiseOutput = false;
        drumEnvelope = 0;
        drumNoiseLow = 0;
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

    //*********************************************************************
    //  Suspend/resume synth
    //*********************************************************************

    void suspend()
    {
      CLR(TIMSK2, OCIE2A);
    }

    void resume()
    {
      SET(TIMSK2, OCIE2A);
    }
};

#endif
