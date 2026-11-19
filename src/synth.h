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
// One trailing byte makes the final unaligned 16-bit flash read safe.
static_assert(sizeof(kickSample) == (kickSampleLength * 9U + 7U) / 8U + 1U, "invalid packed kick sample size");
static_assert(sizeof(snareSample) == (snareSampleLength * 9U + 7U) / 8U + 1U, "invalid packed snare sample size");
static_assert(sizeof(hihatSample) == (hihatSampleLength * 9U + 7U) / 8U + 1U, "invalid packed hi-hat sample size");

#define SET(x,y) (x |= (1 << y))
#define CLR(x,y) (x &= (~(1 << y)))

// Voices 0-3 form the normal pool. While PLAY's background sequence runs,
// voices 3-4 are its dry background voices and voices 0-2 stay live; voice 4
// is used only by the background sequence.
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
// Flash on this target cannot reach this address; reserve it as a generated
// source marker rather than spending flash on a noise wavetable.
const uint16_t SYNTH_GENERATED_NOISE = 0xFFFF;
const uint16_t SYNTH_DEFAULT_PULSE_WIDTH = 0x8000;
// Updated once per four audio frames (5 kHz), giving a fixed 5 Hz vibrato.
const uint32_t SYNTH_VIBRATO_LFO_STEP = 4294967UL;
const uint8_t SYNTH_CHORUS_OFF = 0;
const uint8_t SYNTH_CHORUS_1 = 1;
const uint8_t SYNTH_CHORUS_2 = 2;
// Juno-60-style triangle-modulated delay parameters at the 20 kHz engine rate.
const uint16_t SYNTH_CHORUS_BUFFER_SAMPLES = 108;
const uint32_t SYNTH_CHORUS_1_LFO_STEP = 110166UL; // 0.513 Hz
const uint32_t SYNTH_CHORUS_2_LFO_STEP = 185328UL; // 0.863 Hz
const uint16_t SYNTH_CHORUS_MIN_DELAY_Q8 = 8500; // 1.66 ms

static_assert(sizeof(SinTable) / sizeof(SinTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid sine table size");
static_assert(sizeof(TriangleTable) / sizeof(TriangleTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid triangle table size");
static_assert(sizeof(SawTable) / sizeof(SawTable[0]) == (1U << SYNTH_WAVETABLE_INDEX_BITS), "invalid saw table size");

// Audio-engine state. It is deliberately not volatile: every foreground access
// in class synth is wrapped in ATOMIC_BLOCK, whose cli/SREG-restore are
// compiler memory barriers, and the renderer never runs concurrently with the
// foreground. This lets the renderer keep values in registers.
//
// Rendering is block based. TIMER2_COMPA (20 kHz) only writes the next PWM
// sample from a sixteen-sample ring. After each eight-sample half has been
// played it enables TIMER2_COMPB, whose interrupt re-enables interrupts and
// renders that half while the other half plays. Voices are rendered in two
// four-sample sub-blocks, each preceded by the envelope and vibrato update,
// so the per-voice control rate stays at 5 kHz; the PLAY VCA and morph LFOs
// and the bus filter slew advance once per block (2.5 kHz).
const uint8_t SYNTH_BLOCK_SAMPLES = 8;
const uint8_t SYNTH_VOICE_BLOCK_SAMPLES = 4;
const uint8_t SYNTH_OUTPUT_RING_SAMPLES = 2 * SYNTH_BLOCK_SAMPLES;
const uint16_t SYNTH_PWM_CENTER = 256;

uint16_t audioOutputRing[SYNTH_OUTPUT_RING_SAMPLES];
uint8_t audioOutputIndex = 0;
bool audioRenderBusy = false;

// Per-voice state, grouped so the assembly renderer (synth_voices.S)
// reaches every field through one pointer. Keep the field order and the
// 23-byte size in sync with the VOICE_* offsets there.
extern "C" {
struct SynthVoice {
  // Per-sample oscillator state.
  uint16_t phase;
  uint16_t step;
  // A flash wavetable address, SYNTH_GENERATED_SQUARE, or SYNTH_GENERATED_NOISE.
  uint16_t source;
  // Full phase resolution makes pulse width independent from wavetable size.
  uint16_t pulseWidth;
  // Independent maximal-length 16-bit Galois LFSR; must never be zero.
  uint16_t noise;
  uint8_t amp;
  // Control-rate state, updated once per four-sample block. Bit 15 of
  // envelopePhase marks a finished or stopped envelope.
  uint16_t envelopePhase;
  uint16_t envelopeStep;
  // Flash envelope table; assigned by setupVoice() before any trigger.
  uint16_t envelope;
  uint16_t pitch;
  // Vibrato peak offset in 1/256 steps (eight times the 1/32 pitch-step
  // peak), precomputed from pitch and vibratoDepth by the foreground.
  uint16_t vibratoPeak;
  uint8_t vibratoDepth;
  // Output level; amp = envelope * (level + 1) / 256, so amp <= level.
  uint8_t level;
};

// Each voice adds floor(sample * amp / 2), so one voice reaches full scale.
// The bus has no headroom above full PWM scale: the amps of all voices plus
// the drum's envelope peak must sum to at most SYNTH_LEVEL_BUDGET. Levels may
// exceed it; the bus limiter in synthUpdateVoices then lowers every amp by a
// shared gain. The chorus, VCA, and morph never increase the peak;
// the resonant filter clamps its output, including clipped resonant peaks,
// to the same voice-bus range.
const uint16_t SYNTH_LEVEL_BUDGET = 255;

#define SYNTH_VOICE_INIT(noiseSeed) \
  {0, 0, SYNTH_GENERATED_SQUARE, SYNTH_DEFAULT_PULSE_WIDTH, noiseSeed, 0, \
   0x8000, 10, 0, 500, 0, 0, 63}
SynthVoice synthVoices[SYNTH_VOICE_COUNT] = {
  SYNTH_VOICE_INIT(0xA5C3), SYNTH_VOICE_INIT(0xD72B),
  SYNTH_VOICE_INIT(0x9E37), SYNTH_VOICE_INIT(0xF1A5), SYNTH_VOICE_INIT(0xB4C9)
};
#undef SYNTH_VOICE_INIT

// Block-constant morph description read by synthRenderVoices.
const uint8_t SYNTH_KIND_A_SQUARE = 0x01;
const uint8_t SYNTH_KIND_A_NOISE = 0x02;
const uint8_t SYNTH_KIND_B_SQUARE = 0x04;
const uint8_t SYNTH_KIND_B_NOISE = 0x08;
uint16_t synthMorphSourceA = 0;
uint16_t synthMorphSourceB = 0;
uint8_t synthMorphAmount = 0;
uint8_t synthMorphKinds = 0;

// Advances the first `count` envelopes and vibrato pitch steps by one block.
void synthUpdateVoices(int8_t vibratoLfo, uint8_t count);

// Stores the active drum sample's four-sample block in accumulator.
void synthRenderDrum(int16_t *accumulator);
// 128 >> (index & 7) aligns a packed 9-bit drum sample at bits 7..15.
extern const uint8_t synthPackedSampleScales[8] PROGMEM __attribute__((used)) =
  {128, 64, 32, 16, 8, 4, 2, 1};

// Adds a four-sample block of `count` consecutive voices to accumulator,
// or stores it with SYNTH_RENDER_CLEAR. With SYNTH_RENDER_SPLIT only the first
// SYNTH_BACKGROUND_VOICE voices go to accumulator; the others are stored,
// unmorphed, to dry.
const uint8_t SYNTH_RENDER_MORPH = 0x01;
const uint8_t SYNTH_RENDER_CLEAR = 0x02;
const uint8_t SYNTH_RENDER_SPLIT = 0x04;
void synthRenderVoices(SynthVoice *voice, uint8_t count, int16_t *accumulator,
  uint8_t flags, int16_t *dry);

// Applies the selected bus stages and writes eight dithered PWM samples.
const uint8_t SYNTH_FINISH_VCA = 0x01;
const uint8_t SYNTH_FINISH_CHORUS = 0x02;
const uint8_t SYNTH_FINISH_DRY = 0x04;
void synthFinishBlock(int16_t *bus, const int16_t *dry, uint16_t *output,
  uint8_t flags);

// Low-pass filters an eight-sample bus block in place (formula below).
void synthFilterBlock(int16_t *bus, uint8_t coefficient, uint8_t damping,
  uint8_t gain);
}
static_assert(sizeof(SynthVoice) == 23, "synth_voices.S expects a 23-byte SynthVoice");
static_assert(SYNTH_BACKGROUND_VOICE == 3, "synth_voices.S splits the dry background at voice 3");

// A release borrows a voice's envelope and envelopeStep, so the configured
// values are kept here and restored by trigger() and stopVoice(). While
// releasing is set, setEnvelope() and setLength() only update this copy.
struct SynthVoiceEnvelope {
  uint16_t envelope;
  uint16_t envelopeStep;
  bool releasing;
};
SynthVoiceEnvelope synthVoiceEnvelopes[SYNTH_VOICE_COUNT];
// Env0 starts at 255, so a step below 256 reads it first and the fade begins
// exactly at the current amplitude; Env0 reaches zero after about 21 ms.
const uint16_t SYNTH_RELEASE_STEP = 255;

uint32_t vibratoLfoPCW = 0x40000000UL;
int16_t pwmQuantizationError = 0;
uint8_t pwmDitherLfsr = 0xA5;
// Voices 3-4 are the PLAY background sequence while it runs. They then stay
// dry: no waveform morph, filter, VCA, or chorus. Otherwise voice 3 is a
// normal bus voice and voice 4 only finishes a background release.
bool backgroundVoices = false;
// Performance engines. Preset morphing crossfades two flash-resident waveform
// sources and never duplicates oscillator state.
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
// Bus limiter gain; its high byte k scales amps by (k + 1) / 256.
uint16_t synthLimiterGain = 0xFFFF;
// One 16-bit Q7 mono delay line, written only by the renderer. It stores the
// wet half of the signal, so interpolating adjacent samples stays in int16_t.
int16_t chorusBuffer[SYNTH_CHORUS_BUFFER_SAMPLES] = {0};
bool chorusActive = false;
uint8_t chorusWriteIndex = 0;
uint32_t chorusLfoPCW = 0;
uint32_t chorusLfoFTW = 0;
// Preset resonant bus low-pass. The foreground sets the target coefficient
// (0 = open) and damping; the renderer moves the active ones by one step per
// block. Damping SYNTH_FILTER_DAMPING_OPEN (Q about 1) has unity DC gain.
const uint8_t SYNTH_FILTER_DAMPING_OPEN = 128;
uint8_t busFilterTarget = 0;
uint8_t busFilterCoefficient = 0;
uint8_t busFilterDampingTarget = SYNTH_FILTER_DAMPING_OPEN;
uint8_t busFilterDamping = SYNTH_FILTER_DAMPING_OPEN;
int16_t busFilterLow = 0;
int16_t busFilterBand = 0;
// Shared filter envelope. Each bus-voice trigger restarts it with the smooth
// Env1 decay at four times that voice's envelope speed (eight times its step,
// as it advances once per block), so the filter closes within the first
// quarter of the note while the amplitude still sustains. A held note (step
// 0) uses SYNTH_FILTER_ENV_HOLD_STEP, about 0.5 s. It adds amount * level of
// the remaining distance to the brightest coefficient after the slew.
const uint16_t SYNTH_FILTER_ENV_HOLD_STEP = 26;
uint16_t filterEnvPhase = 0x8000;
uint16_t filterEnvStep = 0;
uint8_t filterEnvAmount = 0;

static_assert(SYNTH_CHORUS_BUFFER_SAMPLES <= 255, "chorus index must fit in one byte");

// Returns floor(sample * gain / 16) for a signed 9-bit sample, branch-free,
// for the quieter eight-oscillator drone mix. The high byte of a 9-bit sample
// is 0x00 or 0xFF, so the product is low * gain - (high & gain) * 256: a
// 17-bit value whose sign is the borrow. synth_voices.S uses the same method.
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

// Applies a 9-bit linear gain with error far below one PWM output step.
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

// Returns (a * b) >> 8 with one 8x8 hardware multiply.
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

// The chorus and the final quantizer are implemented by synthFinishBlock in
// synth_voices.S. Per sample, the chorus computes exactly:
//   lfoPhase += lfoStep; triangle = lfoPhase >> 16, mirrored when bit 31 is set
//   high = triangle >> 7; low = (uint8_t)(triangle << 1)
//   delayQ8 = 8500 + high * 0x49 + (high * 0xCC >> 8) + (low * 0x49 >> 8)
//   (triangle * 18892 / 32768, i.e. 1.66-5.35 ms, within 1/128 sample)
//   dryHalf = in >> 1, stored at the write index; first/next are the samples
//   delayQ8 >> 8 and that plus one older
//   out = dryHalf + first + floor((next - first) * (delayQ8 & 0xFF) / 256)
// and the quantizer, with an 8-bit Galois LFSR (taps 0xB8):
//   s = mix + error + ((lfsr & 15) - (lfsr >> 4)) * 4; t = s + 64
//   pwm = (t >> 7) + 256; error = (t & 127) - 64
// The bus low-pass before them, synthFilterBlock, is a Chamberlin state-
// variable filter. With f = busFilterCoefficient (1-255), q =
// busFilterDamping (1-128), g = (5 * q + 384) >> 3, L = busFilterLow, and
// B = busFilterBand, it computes per sample in int16_t, where sat()
// saturates a sum or difference to the int16_t range:
//   h = x >> 2; L = sat(L + ((B * f) >> 8))
//   H = sat(sat(((h * g) >> 7) - ((B * q) >> 7)) - L)
//   B = sat(B + ((H * f) >> 8))
//   x = clamp(L, -8160, 8128) * 4
// i.e. f = 2 sin(pi fc / 20 kHz) * 256 for a 12 dB/octave cutoff of about
// 100 Hz-3.3 kHz, and Q = 128 / q. The input gain g / 128 sits between 1
// and 1/Q: the passband falls by g / 128 (at most about 6.5 dB) while the
// resonant peak rises by g / q (at most about 9 dB) and is clipped. The
// states run at a quarter of the bus scale, where full-scale square, saw,
// and noise inputs stay below saturation, which only guards against wrapping.
// The output is
// clamped to the voice-bus range (-32640 to 32512), inside the quantizer's
// margin.

// Renders one drone frame: one oscillator pair is recalculated per call.
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

// Renders a block of drone frames. It is kept out of line so its registers
// are not saved for every normal-mode block.
static __attribute__((noinline, optimize("O2"))) void renderDroneBlock(int16_t *busQ7)
{
  for (uint8_t i = 0; i < SYNTH_BLOCK_SAMPLES; ++i) busQ7[i] = renderDroneFrame();
}

// Updates the rendered voices' control state once per block (5 kHz per voice).
// synthUpdateVoices computes, for each of the first `count` voices:
//   if (!(envelopePhase & 0x8000)) { envelopePhase += envelopeStep;
//     amp = (envelopePhase & 0x8000) ? 0 :
//       envelope[envelopePhase >> 8] * (level + 1) >> 8; }
//   else amp = 0
//   step = pitch + floor(vibratoPeak * vibratoLfo / 256) when vibratoDepth != 0
// and then, as a bus limiter, with S = the sum of those amps, Sc = (S + 7)
// >> 3, d = drumActive ? drumEnvelope >> 8 : 0, L = 8192 - 32 * d, and
// G = synthLimiterGain:
//   G = min(G + SYNTH_LIMITER_RELEASE, 0xFFFF); k = G >> 8
//   if (Sc * (k + 1) >= L) { k = the largest k with Sc * (k + 1) < L, else 0;
//     G = k << 8 }
//   every amp = amp * (k + 1) >> 8
// so the amps and the drum peak d never sum above 255. The gain drops at
// once, at the same control rate as the envelopes, and recovers in about
// 0.14 s from the 2x level overcommit (SYNTH_LIMITER_RELEASE = 48). Below
// full scale it is unity, so quieter passages keep their dynamics. A voice
// outside `count` has a finished envelope and an amp of zero.
static inline __attribute__((always_inline)) void updateVoiceControls(uint8_t count)
{
  vibratoLfoPCW += SYNTH_VIBRATO_LFO_STEP;
  // Center the triangle at zero and halve it to keep the math 8-bit.
  synthUpdateVoices((int8_t)(lfoTriangle(vibratoLfoPCW >> 24) >> 1) - 32, count);
}

// Renders eight normal-mode samples into the bus and dry accumulators and
// returns the synthFinishBlock stages they need.
static inline __attribute__((always_inline)) uint8_t renderNormalBlock(
  int16_t *busQ7, int16_t *dryQ7)
{
  // The PLAY VCA and morph LFOs advance once per block (2.5 kHz).
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
      // a + floor((b - a) * m / 128) == b + floor((a - b) * (128 - m) / 128),
      // so a generated/table pair can use a faster table-first kernel.
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
  // Voices 3-4 are either the dry PLAY background (sequence notes or drum)
  // or, for voice 3, a regular bus voice. The background drum replaces both;
  // otherwise the drum replaces voice 0. Voice 4 is rendered and updated only
  // while its envelope runs: a two-voice background note or its release.
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

  // The filter opens and closes through its brightest setting with unity
  // damping, where it is nearly transparent: opening, it switches to bypass
  // only after reaching both. While it is open, the state follows the bus.
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
  // The filter envelope follows the same table/phase rule as the voices and
  // bypasses the slew, so its attack is immediate.
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

// Outputs one PWM sample per 20 kHz period and, after each played half,
// enables TIMER2_COMPB to render it again. Its compare match occurs every
// period just after this one, so it is already pending. This tiny handler is
// naked so that it saves only the registers it touches.
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
    // 16-bit timer registers take the high byte first.
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

// Renders the eight-sample half that has just been played. Interrupts are
// re-enabled so TIMER2_COMPA keeps exact output timing (and nests) while this
// runs; the block must be ready within the seven following output periods.
// audioRenderBusy stops a late block from being started twice.
// O2 avoids slow size-optimized shift loops in the renderer.
ISR(TIMER2_COMPB_vect, __attribute__((optimize("O2"))))
{
  TIMSK2 &= ~_BV(OCIE2B);
  audioRenderBusy = true;
  uint16_t *output = &audioOutputRing[(audioOutputIndex & SYNTH_BLOCK_SAMPLES) ^ SYNTH_BLOCK_SAMPLES];
  sei();

  // Every stage that is used stores before it adds.
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

    // Ends a release and applies the configured envelope and length; call
    // with interrupts disabled.
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

    // Full vibrato depth is about +/- 6% of pitch: (pitch / 64) * depth / 16,
    // stored eight times larger for the ISR's 1/256-step multiply.
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
      // Compare B matches just after every period wrap; its interrupt is
      // enabled only when a rendered block is due.
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

    // Starts the audio timers with the selected PWM output mode.
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

      const uint16_t selectedWave = waveSource(wave);

      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoices[voice].source = selectedWave;
      }
    }

    // Pulse width is the high portion of the 16-bit oscillator phase.
    void setPulseWidth(uint8_t voice, uint16_t pulseWidth)
    {
      if (!validVoice(voice)) return;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoices[voice].pulseWidth = pulseWidth;
      }
    }

    // Crossfades the first two preset waveform sources. A zero rate or an
    // absent secondary source leaves the regular per-voice assignment intact.
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

    // Sets vibrato depth with 64 as neutral; either direction has equal depth.
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

    // Restarts the envelope at an output level (see SYNTH_LEVEL_BUDGET).
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
        // The dry PLAY background voices do not retrigger the bus filter.
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

    // Lowers a voice's output level without restarting its envelope; amp
    // follows at the next control update. One byte, so no atomic block.
    void limitLevel(uint8_t voice, uint8_t level)
    {
      if (validVoice(voice) && synthVoices[voice].level > level) {
        synthVoices[voice].level = level;
      }
    }

    // Click-free note-off: fades the voice from its current amplitude to
    // silence in about 21 ms through the normal envelope path, so the audio
    // ISR does no extra work. amp = 255 * (level + 1) / 256 = level on the
    // first update, so the fade is continuous; a silent voice stays silent.
    // The next trigger() or stopVoice() restores the configured envelope.
    void releaseVoice(uint8_t voice)
    {
      if (!validVoice(voice)) {
        return;
      }

      SynthVoice &state = synthVoices[voice];
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        synthVoiceEnvelopes[voice].releasing = true;
        // amp includes the limiter gain, which still applies during the fade.
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

    // Starts packed sample playback with tone, decay, and level scaling. The
    // envelope starts at level * 256 + 255, so its high byte peaks at level,
    // and its step still reaches zero after the same duration.
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
        synthVoices[0].envelopePhase = 0x8000;
        synthVoices[0].amp = 0;
        synthVoices[0].step = 0;
        drumSampleAddress = sampleAddress;
        drumSampleLength = sampleLength;
        drumSamplePhase = 0;
        drumSampleStep = sampleStep;
        drumEnvelope = envelopeStart;
        drumEnvelopeStep = envelopeStep;
        drumActive = sampleLength != 0;
      }
    }

    // Fades an active drum hit to silence within 512 samples (about 26 ms)
    // instead of cutting it; the renderer already decays the envelope
    // linearly and stops at zero, so only the foreground step changes.
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

    // A macro-controlled, 50/50 dry/wet effect with no feedback path on the
    // voice bus; a running PLAY background sequence on voices 3-4 stays dry.
    void configureChorus(uint8_t mode)
    {
      const uint32_t lfoStep = mode == SYNTH_CHORUS_1 ? SYNTH_CHORUS_1_LFO_STEP :
        mode == SYNTH_CHORUS_2 ? SYNTH_CHORUS_2_LFO_STEP : 0;
      // The ISR leaves an inactive delay line untouched; clear stale audio
      // so enabling the effect cannot replay an old signal.
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

    // Sets the bus low-pass coefficient f (2 sin(pi fc / fs) * 256; 0 is
    // open) and damping q (128 / Q, 1-128). The renderer slews toward both,
    // so changes are click-free. envelopeAmount (0-255) scales the shared
    // filter envelope; 255 sweeps its peak to the brightest coefficient.
    void setBusFilter(uint8_t coefficient, uint8_t damping, uint8_t envelopeAmount)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        busFilterTarget = coefficient;
        busFilterDampingTarget = damping;
        filterEnvAmount = envelopeAmount;
      }
    }

    // While PLAY's background sequence runs, voices 3-4 bypass morph, filter,
    // VCA, and chorus; otherwise voice 3 is mixed like voices 0-2.
    void setBackgroundVoices(bool background)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        backgroundVoices = background;
      }
    }

    void setDroneMode(bool enabled)
    {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        droneMode = enabled;
        if (enabled) {
          chorusActive = false;
          chorusLfoFTW = 0;
        }
        // The drone mix is unfiltered; leaving it, the filter fades in again
        // from silence.
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
