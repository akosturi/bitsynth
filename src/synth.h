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
#define NOISE    4

#define ENVELOPE0 0
#define ENVELOPE1 1
#define ENVELOPE2 2
#define ENVELOPE3 3
#define ENVELOPE4 4
#define ENVELOPE5 5
#define ENVELOPE6 6
#define ENVELOPE7 7

#define FS 20000UL

#if defined(SYNTH_FIXED_OUTPUT_MODE) && \
    SYNTH_FIXED_OUTPUT_MODE != DIFF && \
    SYNTH_FIXED_OUTPUT_MODE != CHA && \
    SYNTH_FIXED_OUTPUT_MODE != CHB
#error "SYNTH_FIXED_OUTPUT_MODE must be DIFF, CHA, or CHB"
#endif

static_assert(kickSampleRate == FS, "kick sample rate must match the 20 kHz audio engine");
static_assert(snareSampleRate == FS, "snare sample rate must match the 20 kHz audio engine");
static_assert(hihatSampleRate == FS, "hi-hat sample rate must match the 20 kHz audio engine");
static_assert(kickSampleBits == 9, "kick sample must use unsigned 9-bit PCM");
static_assert(snareSampleBits == 9, "snare sample must use unsigned 9-bit PCM");
static_assert(hihatSampleBits == 9, "hi-hat sample must use unsigned 9-bit PCM");
static_assert(sizeof(kickSample) == (kickSampleLength * 9U + 7U) / 8U + 1U, "invalid packed kick sample size");
static_assert(sizeof(snareSample) == (snareSampleLength * 9U + 7U) / 8U + 1U, "invalid packed snare sample size");
static_assert(sizeof(hihatSample) == (hihatSampleLength * 9U + 7U) / 8U + 1U, "invalid packed hi-hat sample size");

#define SET(x,y) (x |= (1 << y))
#define CLR(x,y) (x &= (~(1 << y)))

const uint8_t SYNTH_VOICE_COUNT = 5;
const uint8_t SYNTH_NORMAL_VOICE_COUNT = 4;
const uint8_t SYNTH_BACKGROUND_VOICE = 3;
const uint8_t SYNTH_BACKGROUND_VOICE_COUNT = SYNTH_VOICE_COUNT - SYNTH_BACKGROUND_VOICE;
const uint8_t SYNTH_DRONE_COUNT = 8;
const uint8_t SYNTH_MIDI_NOTE_MAX = 127;
const uint8_t SYNTH_LENGTH_MAX = 127;
const uint8_t SYNTH_ENVELOPE_COUNT = 6;
const uint8_t SYNTH_WAVETABLE_INDEX_BITS = 8;
const uint8_t SYNTH_WAVETABLE_SHIFT = 16 - SYNTH_WAVETABLE_INDEX_BITS;
const uint16_t SYNTH_GENERATED_SQUARE = 0;
const uint16_t SYNTH_GENERATED_NOISE = 0xFFFF;
const uint16_t SYNTH_DEFAULT_PULSE_WIDTH = 0x8000;
const uint32_t SYNTH_VIBRATO_LFO_STEP = 4294967UL;
const uint8_t SYNTH_CHORUS_OFF = 0;
const uint8_t SYNTH_CHORUS_1 = 1;
const uint8_t SYNTH_CHORUS_2 = 2;
const uint16_t SYNTH_CHORUS_BUFFER_SAMPLES = 108;
const uint32_t SYNTH_CHORUS_1_LFO_STEP = 110166UL;
const uint32_t SYNTH_CHORUS_2_LFO_STEP = 185328UL;
const uint16_t SYNTH_CHORUS_MIN_DELAY_Q8 = 8500;

static_assert(sizeof(SinTable) / sizeof(SinTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid sine table size");
static_assert(sizeof(TriangleTable) / sizeof(TriangleTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid triangle table size");
static_assert(sizeof(SawTable) / sizeof(SawTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid saw table size");

const uint8_t SYNTH_BLOCK_SAMPLES = 8;
const uint8_t SYNTH_VOICE_BLOCK_SAMPLES = 4;
const uint8_t SYNTH_OUTPUT_RING_SAMPLES = 2 * SYNTH_BLOCK_SAMPLES;
const uint16_t SYNTH_PWM_CENTER = 256;

uint16_t audioOutputRing[SYNTH_OUTPUT_RING_SAMPLES];
uint8_t audioOutputIndex = 0;
bool audioRenderBusy = false;

// Shared with synth_voices.S: keep SynthVoice in sync with its VOICE_* offsets.
extern "C" {
struct SynthVoice {
  uint16_t phase;
  uint16_t step;
  uint16_t source;
  uint16_t pulseWidth;
  uint16_t noise;
  uint8_t amp;
  uint16_t envelopePhase;
  uint16_t envelopeStep;
  uint16_t envelope;
  uint16_t pitch;
  uint16_t vibratoPeak;
  uint8_t vibratoDepth;
  uint8_t level;
};

const uint16_t SYNTH_LEVEL_BUDGET = 255;

#define SYNTH_VOICE_INIT(noiseSeed) \
  {0, 0, SYNTH_GENERATED_SQUARE, SYNTH_DEFAULT_PULSE_WIDTH, noiseSeed, 0, \
   0x8000, 10, 0, 500, 0, 0, 63}
SynthVoice synthVoices[SYNTH_VOICE_COUNT] = {
  SYNTH_VOICE_INIT(0xA5C3), SYNTH_VOICE_INIT(0xD72B),
  SYNTH_VOICE_INIT(0x9E37), SYNTH_VOICE_INIT(0xF1A5), SYNTH_VOICE_INIT(0xB4C9)
};
#undef SYNTH_VOICE_INIT

const uint8_t SYNTH_KIND_A_SQUARE = 0x01;
const uint8_t SYNTH_KIND_A_NOISE = 0x02;
const uint8_t SYNTH_KIND_B_SQUARE = 0x04;
const uint8_t SYNTH_KIND_B_NOISE = 0x08;
uint16_t synthMorphSourceA = 0;
uint16_t synthMorphSourceB = 0;
uint8_t synthMorphAmount = 0;
uint8_t synthMorphKinds = 0;

void synthUpdateVoices(int8_t vibratoLfo, uint8_t count);

void synthRenderDrum(int16_t *accumulator);
extern const uint8_t synthPackedSampleScales[8] PROGMEM __attribute__((used)) =
  {128, 64, 32, 16, 8, 4, 2, 1};

const uint8_t SYNTH_RENDER_MORPH = 0x01;
const uint8_t SYNTH_RENDER_CLEAR = 0x02;
const uint8_t SYNTH_RENDER_SPLIT = 0x04;
void synthRenderVoices(SynthVoice *voice, uint8_t count, int16_t *accumulator,
  uint8_t flags, int16_t *dry);

const uint8_t SYNTH_FINISH_VCA = 0x01;
const uint8_t SYNTH_FINISH_CHORUS = 0x02;
const uint8_t SYNTH_FINISH_DRY = 0x04;
void synthFinishBlock(int16_t *bus, const int16_t *dry, uint16_t *output,
  uint8_t flags);

void synthFilterBlock(int16_t *bus, uint8_t coefficient, uint8_t damping,
  uint8_t gain);
}
static_assert(sizeof(SynthVoice) == 23, "synth_voices.S expects a 23-byte SynthVoice");
static_assert(SYNTH_BACKGROUND_VOICE == 3, "synth_voices.S splits the dry background at voice 3");

struct SynthVoiceEnvelope {
  uint16_t envelope;
  uint16_t envelopeStep;
  bool releasing;
};
SynthVoiceEnvelope synthVoiceEnvelopes[SYNTH_VOICE_COUNT];
const uint16_t SYNTH_RELEASE_STEP = 255;

uint32_t vibratoLfoPCW = 0x40000000UL;
int16_t pwmQuantizationError = 0;
uint8_t pwmDitherLfsr = 0xA5;
bool backgroundVoices = false;
bool playMorphActive = false;
uint16_t playMorphPrimarySource = SYNTH_GENERATED_SQUARE;
uint16_t playMorphSecondarySource = SYNTH_GENERATED_SQUARE;
uint32_t playMorphLfoPCW = 0x80000000UL;
uint32_t playMorphLfoFTW = 0;
uint8_t playMorphAmount = 128;
uint16_t playVcaGain = 256;
uint16_t playVcaAttenuationDepth = 0;
uint32_t playVcaLfoPCW = 0x80000000UL;
uint32_t playVcaLfoFTW = 0;
bool droneMode = false;
uint16_t dronePCW[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
uint16_t droneFTW[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
uint16_t droneWaveSources[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t droneAmp[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t droneTargetAmp[SYNTH_DRONE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t droneRampVoice = 0;
uint8_t droneMixPair = 0;
int16_t dronePairMixQ7[SYNTH_DRONE_COUNT / 2] = {0, 0, 0, 0};
uint32_t droneLfoPCW[SYNTH_DRONE_COUNT / 2] = {0, 0, 0, 0};
uint32_t droneLfoFTW[SYNTH_DRONE_COUNT / 2] = {0, 0, 0, 0};
bool drumActive = false;
uint16_t drumSampleAddress = 0;
uint16_t drumSampleLength = 0;
uint32_t drumSamplePhase = 0;
uint32_t drumSampleStep = 0x10000UL;
uint16_t drumEnvelope = 0;
uint16_t drumEnvelopeStep = 1;
uint16_t synthLimiterGain = 0xFFFF;
int16_t chorusBuffer[SYNTH_CHORUS_BUFFER_SAMPLES] = {0};
bool chorusActive = false;
uint8_t chorusWriteIndex = 0;
uint32_t chorusLfoPCW = 0;
uint32_t chorusLfoFTW = 0;
const uint8_t SYNTH_FILTER_DAMPING_OPEN = 128;
uint8_t busFilterTarget = 0;
uint8_t busFilterCoefficient = 0;
uint8_t busFilterDampingTarget = SYNTH_FILTER_DAMPING_OPEN;
uint8_t busFilterDamping = SYNTH_FILTER_DAMPING_OPEN;
int16_t busFilterLow = 0;
int16_t busFilterBand = 0;
const uint16_t SYNTH_FILTER_ENV_HOLD_STEP = 26;
uint16_t filterEnvPhase = 0x8000;
uint16_t filterEnvStep = 0;
uint8_t filterEnvAmount = 0;

static_assert(SYNTH_CHORUS_BUFFER_SAMPLES <= 255, "chorus index must fit in one byte");

// Returns floor(sample * gain / 16) for a signed 9-bit sample, branch-free.
static inline __attribute__((always_inline)) int16_t scaleSigned9SampleQ4(
  int16_t sample, uint8_t gain)
{
  int16_t scaled;
  asm("mul %A1, %2\n\t"
      "and %B1, %2\n\t"
      "sub r1, %B1\n\t"
      "ror r1\n\t" "ror r0\n\t"
      "asr r1\n\t" "ror r0\n\t"
      "asr r1\n\t" "ror r0\n\t"
      "asr r1\n\t" "ror r0\n\t"
      "movw %0, r0\n\t"
      "clr r1"
      : "=r"(scaled), "+r"(sample) : "r"(gain));
  return scaled;
}

static inline __attribute__((always_inline)) int16_t readWavetable(
  uint16_t source, uint16_t phase)
{
  return (int16_t)pgm_read_word(source + ((phase >> SYNTH_WAVETABLE_SHIFT) << 1));
}

static inline __attribute__((always_inline)) uint8_t sourceKind(
  uint16_t source, uint8_t squareKind, uint8_t noiseKind)
{
  return source == SYNTH_GENERATED_SQUARE ? squareKind :
    source == SYNTH_GENERATED_NOISE ? noiseKind : 0;
}

// Applies a 9-bit linear gain.
static inline __attribute__((always_inline)) int16_t scalePlayMix(
  int16_t inputQ7, uint8_t gain)
{
  if (gain == 0) return 0;

  const bool negative = inputQ7 < 0;
  const uint16_t magnitude = negative ? -inputQ7 : inputQ7;
  const uint8_t magnitudeHigh = magnitude >> 8;
  const uint8_t magnitudeLow = magnitude;
  const uint8_t halfGain = gain >> 1;
  const uint16_t highProduct =
    __builtin_avr_fmul(magnitudeHigh, gain) >> 1;
  const uint16_t lowProduct =
    __builtin_avr_fmul(magnitudeLow, halfGain);
  const uint16_t scaled = highProduct + ((lowProduct + 128) >> 8);
  return negative ? -(int16_t)scaled : scaled;
}

// Returns (a * b) >> 8.
static inline __attribute__((always_inline)) uint8_t multiplyHigh8(uint8_t a, uint8_t b)
{
  uint8_t high;
  asm("mul %1, %2\n\t" "mov %0, r1\n\t" "clr r1" : "=r"(high) : "r"(a), "r"(b));
  return high;
}

static inline __attribute__((always_inline)) uint8_t lfoTriangle(uint8_t phase)
{
  return (phase & 0x80) ? (uint8_t)(256U - phase) : phase;
}

// Renders one drone sample; one oscillator pair is recalculated per call.
static inline __attribute__((always_inline)) int16_t renderDroneFrame()
{
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
      const uint16_t phase = dronePCW[voice] += droneFTW[voice] << 2;
      const uint16_t source = droneWaveSources[voice];
      const int16_t sample = source == SYNTH_GENERATED_SQUARE ?
        (phase < SYNTH_DEFAULT_PULSE_WIDTH ? 255 : -256) :
        readWavetable(source, phase);
      pairMixQ7 += scaleSigned9SampleQ4(sample, amplitude);
    }
  }

  const uint32_t lfoStep = droneLfoFTW[pair];
  if (lfoStep != 0) {
    const uint32_t lfoPhase = droneLfoPCW[pair] + lfoStep;
    droneLfoPCW[pair] = lfoPhase;
    const uint16_t gain = (uint16_t)lfoTriangle(lfoPhase >> 24) << 1;
    if (gain < 256) {
      pairMixQ7 = scalePlayMix(pairMixQ7, (uint8_t)gain);
    }
  }
  dronePairMixQ7[pair] = pairMixQ7;
  return dronePairMixQ7[0] + dronePairMixQ7[1] +
    dronePairMixQ7[2] + dronePairMixQ7[3];
}

// Renders eight drone samples; out of line so normal blocks skip its register saves.
static __attribute__((noinline, optimize("O2"))) void renderDroneBlock(int16_t *busQ7)
{
  for (uint8_t i = 0; i < SYNTH_BLOCK_SAMPLES; ++i) busQ7[i] = renderDroneFrame();
}

// Advances the vibrato LFO, then envelopes, vibrato, and the bus limiter.
static inline __attribute__((always_inline)) void updateVoiceControls(uint8_t count)
{
  vibratoLfoPCW += SYNTH_VIBRATO_LFO_STEP;
  synthUpdateVoices((int8_t)(lfoTriangle(vibratoLfoPCW >> 24) >> 1) - 32, count);
}

// Renders eight voice and drum samples, runs the bus filter, and returns the finish stages.
static inline __attribute__((always_inline)) uint8_t renderNormalBlock(
  int16_t *busQ7, int16_t *dryQ7)
{
  if (playVcaLfoFTW != 0) {
    const uint32_t phase = playVcaLfoPCW + playVcaLfoFTW;
    playVcaLfoPCW = phase;
    const uint8_t triangle = lfoTriangle(phase >> 24);
    const uint16_t attenuation = (playVcaAttenuationDepth * triangle + 64) >> 7;
    playVcaGain = 256 - attenuation;
  }
  if (playMorphActive) {
    const uint32_t phase = playMorphLfoPCW + playMorphLfoFTW;
    playMorphLfoPCW = phase;
    playMorphAmount = lfoTriangle(phase >> 24);
  }

  const uint8_t morph = playMorphActive ? SYNTH_RENDER_MORPH : 0;
  if (morph) {
    uint16_t primary = playMorphPrimarySource;
    uint16_t secondary = playMorphSecondarySource;
    uint8_t amount = playMorphAmount;
    uint8_t kinds = sourceKind(primary, SYNTH_KIND_A_SQUARE, SYNTH_KIND_A_NOISE) |
      sourceKind(secondary, SYNTH_KIND_B_SQUARE, SYNTH_KIND_B_NOISE);
    if (kinds == SYNTH_KIND_A_SQUARE || kinds == SYNTH_KIND_A_NOISE) {
      const uint16_t swap = primary;
      primary = secondary;
      secondary = swap;
      amount = 128 - amount;
      kinds <<= 2;
    }
    synthMorphSourceA = primary;
    synthMorphSourceB = secondary;
    synthMorphAmount = amount;
    synthMorphKinds = kinds;
  }
  const bool background = backgroundVoices;
  const uint8_t count = synthVoices[SYNTH_VOICE_COUNT - 1].envelopePhase & 0x8000 ?
    SYNTH_NORMAL_VOICE_COUNT : SYNTH_VOICE_COUNT;
  for (uint8_t sub = 0; sub < SYNTH_BLOCK_SAMPLES; sub += SYNTH_VOICE_BLOCK_SAMPLES) {
    updateVoiceControls(count);
    int16_t *bus = busQ7 + sub;
    int16_t *dry = dryQ7 + sub;
    if (drumActive) {
      if (background) {
        synthRenderDrum(dry);
        synthRenderVoices(&synthVoices[0], SYNTH_BACKGROUND_VOICE, bus,
          morph | SYNTH_RENDER_CLEAR, dry);
      } else {
        synthRenderDrum(bus);
        synthRenderVoices(&synthVoices[1], count - 1, bus, morph, dry);
      }
    } else {
      synthRenderVoices(&synthVoices[0], count, bus,
        morph | SYNTH_RENDER_CLEAR | (background ? SYNTH_RENDER_SPLIT : 0), dry);
    }
  }

  uint8_t coefficient = busFilterCoefficient;
  const uint8_t target = busFilterTarget;
  uint8_t damping = busFilterDamping;
  const uint8_t dampingTarget = target ? busFilterDampingTarget : SYNTH_FILTER_DAMPING_OPEN;
  if (damping != dampingTarget) {
    if (dampingTarget > damping) ++damping; else --damping;
    busFilterDamping = damping;
  }
  if (coefficient != target) {
    if (coefficient == 0) coefficient = 255;
    else if (target == 0) {
      if (coefficient != 255) ++coefficient;
      else if (damping == SYNTH_FILTER_DAMPING_OPEN) coefficient = 0;
    } else if (target > coefficient) ++coefficient; else --coefficient;
    busFilterCoefficient = coefficient;
  }
  uint16_t envelopePhase = filterEnvPhase;
  uint8_t envelopeLevel = 0;
  if (!(envelopePhase & 0x8000)) {
    envelopePhase += filterEnvStep;
    filterEnvPhase = envelopePhase;
    if (!(envelopePhase & 0x8000))
      envelopeLevel = pgm_read_byte(Env1 + (envelopePhase >> 8));
  }
  if (coefficient) {
    coefficient += multiplyHigh8(255 - coefficient,
      multiplyHigh8(envelopeLevel, filterEnvAmount));
    synthFilterBlock(busQ7, coefficient, damping,
      (uint8_t)((damping * 5U + 384) >> 3));
  } else { busFilterLow = busQ7[SYNTH_BLOCK_SAMPLES - 1] >> 2; busFilterBand = 0; }

  uint8_t flags = background ? SYNTH_FINISH_DRY : 0;
  if (playVcaGain < 256) flags |= SYNTH_FINISH_VCA;
  if (chorusActive) flags |= SYNTH_FINISH_CHORUS;
  return flags;
}

#if !defined(SYNTH_FIXED_OUTPUT_MODE) || SYNTH_FIXED_OUTPUT_MODE == DIFF
  #define SYNTH_PWM_STORE "sts %[ocrah], r25\n\t" "sts %[ocrbh], r25\n\t" \
    "ld r25, Z\n\t" "sts %[ocral], r25\n\t" "sts %[ocrbl], r25\n\t"
#elif SYNTH_FIXED_OUTPUT_MODE == CHB
  #define SYNTH_PWM_STORE "sts %[ocrbh], r25\n\t" "ld r25, Z\n\t" "sts %[ocrbl], r25\n\t"
#else
  #define SYNTH_PWM_STORE "sts %[ocrah], r25\n\t" "ld r25, Z\n\t" "sts %[ocral], r25\n\t"
#endif
static_assert(SYNTH_OUTPUT_RING_SAMPLES == 16 && SYNTH_BLOCK_SAMPLES == 8,
  "TIMER2_COMPA assumes a sixteen-sample ring of eight-sample halves");
static_assert(SYNTH_VOICE_BLOCK_SAMPLES == 4, "synth_voices.S renders four-sample voice blocks");

// Writes one PWM sample per 20 kHz period and requests a render after each half ring.
ISR(TIMER2_COMPA_vect, ISR_NAKED)
{
  asm volatile(
    "push r24\n\t"
    "in r24, __SREG__\n\t"
    "push r24\n\t"
    "push r25\n\t"
    "push r30\n\t"
    "push r31\n\t"
    "lds r24, %[index]\n\t"
    "mov r30, r24\n\t"
    "lsl r30\n\t"
    "clr r31\n\t"
    "subi r30, lo8(-(%[ring]))\n\t"
    "sbci r31, hi8(-(%[ring]))\n\t"
    "ldd r25, Z+1\n\t"
    SYNTH_PWM_STORE
    "inc r24\n\t"
    "andi r24, 15\n\t"
    "sts %[index], r24\n\t"
    "andi r24, 7\n\t"
    "brne 1f\n\t"
    "lds r24, %[busy]\n\t"
    "tst r24\n\t"
    "brne 1f\n\t"
    "lds r24, %[timsk]\n\t"
    "ori r24, %[ocie]\n\t"
    "sts %[timsk], r24\n\t"
    "1:\n\t"
    "pop r31\n\t"
    "pop r30\n\t"
    "pop r25\n\t"
    "pop r24\n\t"
    "out __SREG__, r24\n\t"
    "pop r24\n\t"
    "reti\n\t"
    :
    : [index] "i"(&audioOutputIndex), [ring] "i"(audioOutputRing),
      [busy] "i"(&audioRenderBusy), [timsk] "n"(_SFR_MEM_ADDR(TIMSK2)),
      [ocie] "n"(_BV(OCIE2B)),
      [ocrah] "n"(_SFR_MEM_ADDR(OCR1AH)), [ocral] "n"(_SFR_MEM_ADDR(OCR1AL)),
      [ocrbh] "n"(_SFR_MEM_ADDR(OCR1BH)), [ocrbl] "n"(_SFR_MEM_ADDR(OCR1BL)));
}

// Renders the half ring just played with interrupts on; it must finish within seven periods.
ISR(TIMER2_COMPB_vect, __attribute__((optimize("O2"))))
{
  TIMSK2 &= ~_BV(OCIE2B);
  audioRenderBusy = true;
  uint16_t *output = &audioOutputRing[(audioOutputIndex & SYNTH_BLOCK_SAMPLES) ^ SYNTH_BLOCK_SAMPLES];
  sei();

  int16_t busQ7[SYNTH_BLOCK_SAMPLES];
  int16_t dryQ7[SYNTH_BLOCK_SAMPLES];
  uint8_t flags = 0;
  if (droneMode) {
    renderDroneBlock(busQ7);
  } else {
    flags = renderNormalBlock(busQ7, dryQ7);
  }
  synthFinishBlock(busQ7, dryQ7, output, flags);
  audioRenderBusy = false;
}

class synth
{
  private:
    uint16_t waveSource(uint8_t wave) const
    {
      switch (wave)
      {
        case TRIANGLE:
          return (uint16_t)TriangleTable;
        case SQUARE:
          return SYNTH_GENERATED_SQUARE;
        case SAW:
          return (uint16_t)SawTable;
        case NOISE:
          return SYNTH_GENERATED_NOISE;
        case SINE:
        default:
          return (uint16_t)SinTable;
      }
    }

    // Ends a release and restores the configured envelope; call with interrupts off.
    __attribute__((noinline)) void restoreEnvelope(uint8_t voice)
    {
      SynthVoiceEnvelope &configured = synthVoiceEnvelopes[voice];
      configured.releasing = false;
      synthVoices[voice].envelope = configured.envelope;
      synthVoices[voice].envelopeStep = configured.envelopeStep;
    }

    bool validVoice(uint8_t voice) const
    {
      return voice < SYNTH_VOICE_COUNT;
    }

    // Vibrato peak: up to about 6% of pitch, scaled by 8 for the renderer.
    static uint16_t vibratoPeakFor(uint16_t pitch, uint8_t depth)
    {
      return (((pitch >> 6) * depth) >> 4) << 3;
    }

    void configureTimer2()
    {
      TCCR2A = (1 << WGM21);
      TCCR2B = (1 << CS21);
      TCNT2 = 0;
      OCR2A = (uint8_t)((F_CPU / (8UL * FS)) - 1);
      OCR2B = 1;
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

    // Starts the audio timers with the selected PWM output.
    void begin(uint8_t outputMode)
    {
      cli();
      configureTimer2();

#ifdef SYNTH_FIXED_OUTPUT_MODE
      outputMode = SYNTH_FIXED_OUTPUT_MODE;
#endif

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

      for (uint8_t i = 0; i < SYNTH_OUTPUT_RING_SAMPLES; ++i) {
        audioOutputRing[i] = SYNTH_PWM_CENTER;
      }
      audioOutputIndex = 0;
      enableTimer2Interrupt();
      sei();
    }

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

      const uint16_t selectedWave = waveSource(wave);

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoices[voice].source = selectedWave;
      }
    }

    void setPulseWidth(uint8_t voice, uint16_t pulseWidth)
    {
      if (!validVoice(voice)) return;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoices[voice].pulseWidth = pulseWidth;
      }
    }

    // Crossfades the first two preset waveforms with an LFO; a zero rate disables it.
    void configurePresetMorph(uint8_t primaryWave, uint8_t secondaryWave,
      uint16_t frequencyHundredths, bool secondaryEnabled)
    {
      const uint16_t primary = waveSource(primaryWave);
      const uint16_t secondary = waveSource(secondaryWave);
      const uint32_t step = frequencyHundredths == 0 ? 0 : (uint32_t)frequencyHundredths * 17180UL;
      const bool active = secondaryEnabled && step != 0;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        playMorphPrimarySource = primary;
        playMorphSecondarySource = secondary;
        playMorphLfoFTW = step;
        if (!active) playMorphAmount = 0;
        playMorphActive = active;
      }
    }

    void disablePresetMorph() {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { playMorphActive = false; playMorphLfoFTW = 0; playMorphAmount = 128; }
    }

    // Sets the VCA tremolo depth (0-256) and rate.
    void configurePlayVca(uint16_t depth, uint16_t frequencyHundredths)
    {
      if (depth > 256) depth = 256;
      const uint32_t step = depth == 0 || frequencyHundredths == 0 ? 0 : (uint32_t)frequencyHundredths * 17180UL;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        const bool wasActive = playVcaLfoFTW != 0;
        playVcaAttenuationDepth = depth;
        playVcaLfoFTW = step;
        if (step == 0) {
          playVcaLfoPCW = 0x80000000UL;
          playVcaGain = 256 - depth;
        } else if (!wasActive) {
          playVcaLfoPCW = 0x80000000UL;
          playVcaGain = 256 - depth;
        }
      }
    }

    void disablePlayVca() {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { playVcaAttenuationDepth = 0; playVcaGain = 256; playVcaLfoFTW = 0; }
    }

    void setPitch(uint8_t voice, uint8_t midiNote)
    {
      if (!validVoice(voice)) {
        return;
      }

      if (midiNote > SYNTH_MIDI_NOTE_MAX) {
        midiNote = SYNTH_MIDI_NOTE_MAX;
      }

      setPitchWord(voice, pitchWordForNote(midiNote));
    }

    void setPitchWord(uint8_t voice, uint16_t pitch)
    {
      if (!validVoice(voice)) {
        return;
      }

      SynthVoice &state = synthVoices[voice];
      const uint16_t peak = vibratoPeakFor(pitch, state.vibratoDepth);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        state.pitch = pitch;
        state.vibratoPeak = peak;
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
        case ENVELOPE4:
          selectedEnvelope = (uint16_t)Env4;
          break;
        case ENVELOPE5:
          selectedEnvelope = (uint16_t)Env5;
          break;
      }

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoiceEnvelopes[voice].envelope = selectedEnvelope;
        if (!synthVoiceEnvelopes[voice].releasing) restoreEnvelope(voice);
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

      const uint16_t envelopeSpeed = envelopeStepForLength(length);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoiceEnvelopes[voice].envelopeStep = envelopeSpeed;
        if (!synthVoiceEnvelopes[voice].releasing) restoreEnvelope(voice);
      }
    }

    // Sets vibrato depth; 64 is neutral.
    void setMod(uint8_t voice, uint8_t mod)
    {
      if (!validVoice(voice)) {
        return;
      }

      if (mod > 127) {
        mod = 127;
      }

      const uint8_t depth = mod < 64 ? 64 - mod : mod - 64;
      SynthVoice &state = synthVoices[voice];
      const uint16_t peak = vibratoPeakFor(state.pitch, depth);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        state.vibratoDepth = depth;
        state.vibratoPeak = peak;
      }
    }

    // Restarts a voice's envelope at an output level, and the shared filter envelope.
    void trigger(uint8_t voice, uint8_t level)
    {
      if (!validVoice(voice)) {
        return;
      }

      SynthVoice &state = synthVoices[voice];
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        restoreEnvelope(voice);
        state.level = level;
        state.envelopePhase = 0;
        state.step = state.pitch;
        if (voice < SYNTH_BACKGROUND_VOICE || !backgroundVoices) {
          const uint16_t step = state.envelopeStep;
          filterEnvStep = step ? step << 3 : SYNTH_FILTER_ENV_HOLD_STEP;
          filterEnvPhase = 0;
        }
      }
    }

    void stopVoice(uint8_t voice)
    {
      if (!validVoice(voice)) {
        return;
      }

      SynthVoice &state = synthVoices[voice];
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        restoreEnvelope(voice);
        state.envelopePhase = 0x8000;
        state.amp = 0;
        state.step = 0;
      }
    }

    // Changes a sounding voice's level without restarting it; release fades keep theirs.
    void setLevel(uint8_t voice, uint8_t level)
    {
      if (validVoice(voice) && !synthVoiceEnvelopes[voice].releasing) {
        synthVoices[voice].level = level;
      }
    }

    // Click-free note-off: fades through Env0 (which must start at 255) in about 21 ms.
    void releaseVoice(uint8_t voice)
    {
      if (!validVoice(voice)) {
        return;
      }

      SynthVoice &state = synthVoices[voice];
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoiceEnvelopes[voice].releasing = true;
        uint8_t level = 0;
        if (!(state.envelopePhase & 0x8000)) {
          const uint8_t value = pgm_read_byte(state.envelope + (state.envelopePhase >> 8));
          level = ((uint16_t)(uint8_t)value * state.level + value) >> 8;
        }
        state.level = level;
        state.envelope = (uint16_t)Env0;
        state.envelopeStep = SYNTH_RELEASE_STEP;
        state.envelopePhase = 0;
      }
    }

    // Starts a drum hit with tone, decay, and level.
    void triggerDrum(uint8_t type, uint8_t tone, uint8_t decay, uint8_t level)
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
      const uint16_t envelopeStart = ((uint16_t)level << 8) | 0xFF;
      uint16_t envelopeStep =
        (envelopeStart + durationFrames - 1) / durationFrames;
      if (envelopeStep == 0) envelopeStep = 1;

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        drumActive = false;
        if (!backgroundVoices) {
          synthVoices[0].envelopePhase = 0x8000;
          synthVoices[0].amp = 0;
          synthVoices[0].step = 0;
        }
        drumSampleAddress = sampleAddress;
        drumSampleLength = sampleLength;
        drumSamplePhase = 0;
        drumSampleStep = sampleStep;
        drumEnvelope = envelopeStart;
        drumEnvelopeStep = envelopeStep;
        drumActive = sampleLength != 0;
      }
    }

    // Fades the drum out within about 26 ms.
    void releaseDrum()
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        const uint16_t step = (drumEnvelope >> 9) + 1;
        if (drumEnvelopeStep < step) drumEnvelopeStep = step;
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

    // Selects chorus off, 1, or 2; the delay line is cleared before it turns on.
    void configureChorus(uint8_t mode)
    {
      const uint32_t lfoStep = mode == SYNTH_CHORUS_1 ? SYNTH_CHORUS_1_LFO_STEP :
        mode == SYNTH_CHORUS_2 ? SYNTH_CHORUS_2_LFO_STEP : 0;
      if (!chorusActive) memset(chorusBuffer, 0, sizeof(chorusBuffer));
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (chorusLfoFTW != lfoStep) {
          chorusActive = lfoStep != 0;
          chorusWriteIndex = 0;
          chorusLfoPCW = 0;
          chorusLfoFTW = lfoStep;
        }
      }
    }

    // Sets the filter target coefficient (0 = open), damping, and envelope amount.
    void setBusFilter(uint8_t coefficient, uint8_t damping, uint8_t envelopeAmount)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        busFilterTarget = coefficient;
        busFilterDampingTarget = damping;
        filterEnvAmount = envelopeAmount;
      }
    }

    // While set, voices 3-4 bypass morph, filter, VCA, and chorus.
    void setBackgroundVoices(bool background)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        backgroundVoices = background;
      }
    }

    // Switches the drone engine and resets the filter and chorus state.
    void setDroneMode(bool enabled)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        droneMode = enabled;
        if (enabled) {
          chorusActive = false;
          chorusLfoFTW = 0;
        }
        busFilterCoefficient = 0;
        busFilterDamping = SYNTH_FILTER_DAMPING_OPEN;
        busFilterLow = 0;
        busFilterBand = 0;
        droneMixPair = 0;
        for (uint8_t pair = 0; pair < SYNTH_DRONE_COUNT / 2; ++pair) {
          dronePairMixQ7[pair] = 0;
          droneLfoPCW[pair] = 0;
        }
        pwmQuantizationError = 0;
      }
    }

    void setDroneWave(uint8_t voice, uint8_t wave)
    {
      if (voice >= SYNTH_DRONE_COUNT || wave == NOISE) {
        return;
      }

      const uint16_t selectedWave = waveSource(wave);
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        droneWaveSources[voice] = selectedWave;
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

    void setDroneLfoRate(uint8_t pair, uint16_t frequencyHundredths)
    {
      if (pair >= SYNTH_DRONE_COUNT / 2) {
        return;
      }

      const uint32_t step = frequencyHundredths == 0 ? 0 :
        (uint32_t)frequencyHundredths * 8590UL;
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
