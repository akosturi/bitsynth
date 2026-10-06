#line 1 "C:\\Users\\tuako\\Documents\\bitsynth\\synth.h"
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
const uint8_t SYNTH_MIDI_NOTE_MAX = 127;
const uint8_t SYNTH_LENGTH_MAX = 127;

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

  int16_t mixQ7 =
    (((int8_t)pgm_read_byte(wavs[0] + ((uint8_t*)&(PCW[0] += FTW[0]))[1]) * AMP[0]) >> 2) +
    (((int8_t)pgm_read_byte(wavs[1] + ((uint8_t*)&(PCW[1] += FTW[1]))[1]) * AMP[1]) >> 2) +
    (((int8_t)pgm_read_byte(wavs[2] + ((uint8_t*)&(PCW[2] += FTW[2]))[1]) * AMP[2]) >> 2) +
    (((int8_t)pgm_read_byte(wavs[3] + ((uint8_t*)&(PCW[3] += FTW[3]))[1]) * AMP[3]) >> 2);

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
  const bool shapeOutput = !noiseVoiceMask;

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
  FTW[divider] = PITCH[divider] + (int16_t)((((PITCH[divider] >> 6) * (EPCW[divider] >> 6)) / 128) * MOD[divider]);
  tim++;
}

class synth
{
  private:
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

      uint16_t selectedWave = (uint16_t)SinTable;
      switch (wave)
      {
        case TRIANGLE:
          selectedWave = (uint16_t)TriangleTable;
          break;
        case SQUARE:
          selectedWave = (uint16_t)SquareTable;
          break;
        case SAW:
          selectedWave = (uint16_t)SawTable;
          break;
        case RAMP:
          selectedWave = (uint16_t)RampTable;
          break;
        case NOISE:
          selectedWave = (uint16_t)NoiseTable;
          break;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        wavs[voice] = selectedWave;
        if (wave == NOISE) {
          noiseVoiceMask |= (1 << voice);
        } else {
          noiseVoiceMask &= ~(1 << voice);
        }
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
