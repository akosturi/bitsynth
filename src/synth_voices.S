; BitSynth block renderer for the ATmega328P (called from TIMER2_COMPB in
; synth.h): voice blocks, drum playback, per-block voice control, and the
; bus stage (low-pass filter, VCA, chorus, dither, and quantizer). Every routine is bit-exact
; with the C formulas documented in synth.h and follows the avr-gcc ABI
; (r2-r17 and r28-r29 preserved, r1 zero on return).
;
; void synthRenderVoices(SynthVoice *voice, uint8_t count,
;                        int16_t *accumulator, uint8_t flags, int16_t *dry);
;
; Adds a four-sample block of each of `count` consecutive voices to
; accumulator[0..3] (Q7). With RENDER_CLEAR the accumulators start at zero
; instead of being loaded. With RENDER_SPLIT the first voice is rendered
; unmorphed and stored to dry[0..3], and the remaining voices start from zero.
; Per sample a voice advances its 16-bit phase by its step, reads a signed
; 9-bit sample, and adds floor(sample * amp / 2). With RENDER_MORPH every
; voice crossfades the two block-constant sources described by
; synthMorphSourceA/B, synthMorphKinds, and synthMorphAmount:
; a + floor((b - a) * amount / 128). The caller normalizes a square or noise A
; with a table B to table A, B square or noise, and 128 - amount, which is
; exactly equal.
;
; Looped kernels keep the four accumulators in r2:r3..r8:r9, add each sample
; to r2:r3, and rotate the pairs, so four iterations restore their order.

__zero_reg__ = 1
__tmp_reg__ = 0
IO_SPL = 0x3D
IO_SPH = 0x3E

VOICE_PHASE = 0
VOICE_STEP = 2
VOICE_SOURCE = 4
VOICE_PULSE = 6
VOICE_NOISE = 8
VOICE_AMP = 10
VOICE_ENV_PHASE = 11
VOICE_ENV_STEP = 13
VOICE_ENVELOPE = 15
VOICE_PITCH = 17
VOICE_VIBRATO_PEAK = 19
VOICE_VIBRATO_DEPTH = 21
VOICE_LEVEL = 22
VOICE_SIZE = 23
VOICE_COUNT = 4

; synthMorphKinds bits (a clear pair of bits means a flash wavetable)
KIND_A_SQUARE = 0
KIND_A_NOISE = 1
KIND_B_SQUARE = 2
KIND_B_NOISE = 3

; synthRenderVoices flags
RENDER_MORPH = 0
RENDER_CLEAR = 1
RENDER_SPLIT = 2
; r18 bits while the split voice is pending
SPLIT_PENDING = 7
SPLIT_MORPH = 6

; Register use
;   r2:r3 .. r8:r9  accumulators         r10:r11 phase      r12:r13 step
;   r14:r15 source / slot A              r16 amp            r17 kinds | count
;   r18 remaining voices (+split bits)   r19 morph amount   r20:r21 sample A
;   r22:r23 sample B / difference        r24:r25 slot B     X accumulator ptr
;   Y voice pointer                      Z table pointer    T morph flag

; Adds floor(\lo:\hi * amp / 2) to r2:r3 for a signed 9-bit sample. The high
; byte is 0x00 or 0xFF, so lo * amp - (hi & amp) * 256 is the exact 17-bit
; product; its sign is the subtraction's borrow, shifted back in by ror.
.macro SCALE_ADD_TO lo, hi, alo, ahi
  mul \lo, r16
  and \hi, r16
  sub r1, \hi
  ror r1
  ror r0
  add \alo, r0
  adc \ahi, r1
.endm

.macro SCALE_ADD lo, hi
  SCALE_ADD_TO \lo, \hi, r2, r3
.endm

; r20:r21 += floor((r22:r23 - r20:r21) * r19 / 128), exact, from a 24-bit
; product in r0:r31:r30 built from the low and signed high partial products.
.macro BLEND
  sub r22, r20
  sbc r23, r21
  mul r22, r19
  movw r30, r0
  mulsu r23, r19
  add r31, r0
  clr r0
  adc r0, r1
  lsl r30
  rol r31
  rol r0
  add r20, r31
  adc r21, r0
.endm

; Reads table \slo:\shi at 2 * phase high byte into \dlo:\dhi.
.macro TABLE_READ slo, shi, dlo, dhi
  movw r30, \slo
  add r30, r11
  adc r31, r1
  add r30, r11
  adc r31, r1
  lpm \dlo, Z+
  lpm \dhi, Z
.endm

; Unrolled morph samples: table A (r14:r15) with table B (r24:r25) ...
.macro TT_SAMPLE alo, ahi
  add r10, r12
  adc r11, r13
  TABLE_READ r14, r15, r20, r21
  TABLE_READ r24, r25, r22, r23
  BLEND
  SCALE_ADD_TO r20, r21, \alo, \ahi
  clr r1
.endm

; ... and table A with a square B of pulse width r24:r25.
.macro TS_SAMPLE alo, ahi
  add r10, r12
  adc r11, r13
  TABLE_READ r14, r15, r20, r21
  ldi r22, 0xFF
  ldi r23, 0x00
  cp r10, r24
  cpc r11, r25
  brlo 1f
  ldi r22, 0x00
  ldi r23, 0xFF
1:
  BLEND
  SCALE_ADD_TO r20, r21, \alo, \ahi
  clr r1
.endm

; Moves the next accumulator into r2:r3 and restores the zero register.
.macro ROTATE
  movw r0, r2
  movw r2, r4
  movw r4, r6
  movw r6, r8
  movw r8, r0
  clr r1
.endm

; Reads one sample into \dlo:\dhi from the source kind in r17 bits
; \square/\noise. \slo:\shi holds a table address, a pulse width, or a noise
; LFSR state. \dlo and \dhi must be upper registers.
.macro FETCH square, noise, slo, shi, dlo, dhi
  sbrc r17, \square
  rjmp 1f
  sbrc r17, \noise
  rjmp 2f
  movw r30, \slo
  add r30, r11
  adc r31, r1
  add r30, r11
  adc r31, r1
  lpm \dlo, Z+
  lpm \dhi, Z
  rjmp 3f
1:
  ldi \dlo, 0xFF
  ldi \dhi, 0x00
  cp r10, \slo
  cpc r11, \shi
  brlo 3f
  ldi \dlo, 0x00
  ldi \dhi, 0xFF
  rjmp 3f
2:
  ldi \dhi, 0xB4
  lsr \shi
  ror \slo
  brcc 4f
  eor \shi, \dhi
4:
  mov \dlo, \slo
  mov \dhi, \shi
  andi \dhi, 0x01
  dec \dhi
3:
.endm

  .section .text.synthRenderVoices,"ax",@progbits
  .global synthRenderVoices
  .type synthRenderVoices, @function
synthRenderVoices:
  push r2
  push r3
  push r4
  push r5
  push r6
  push r7
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  push r16
  push r17
  push r28
  push r29
  movw r28, r24
  movw r26, r20
  mov r19, r18
  mov r18, r22
  bst r19, RENDER_MORPH
  sbrs r19, RENDER_SPLIT
  rjmp 3f
  ori r18, 1 << SPLIT_PENDING
  sbrc r19, RENDER_MORPH
  ori r18, 1 << SPLIT_MORPH
  clt
3:
  sbrs r19, RENDER_CLEAR
  rjmp 1f
  clr r2
  clr r3
  movw r4, r2
  movw r6, r2
  movw r8, r2
  rjmp 2f
1:
  ld r2, X+
  ld r3, X+
  ld r4, X+
  ld r5, X+
  ld r6, X+
  ld r7, X+
  ld r8, X+
  ld r9, X
  sbiw r26, 7
2:

.Lvoice:
  ldd r10, Y + VOICE_PHASE
  ldd r11, Y + VOICE_PHASE + 1
  ldd r12, Y + VOICE_STEP
  ldd r13, Y + VOICE_STEP + 1
  ldd r16, Y + VOICE_AMP
  tst r16
  brne 1f
  rjmp .Ladvance
1:
  brtc 2f
  rjmp .Lmorph
2:
  ldd r14, Y + VOICE_SOURCE
  ldd r15, Y + VOICE_SOURCE + 1
  mov r0, r14
  or r0, r15
  brne 3f
  rjmp .Lsquare
3:
  mov r0, r14
  and r0, r15
  inc r0
  brne 4f
  rjmp .Lnoise

  ; Flash wavetable.
4:
  ldi r17, 0x30
.Ltable:
  add r10, r12
  adc r11, r13
  movw r30, r14
  add r30, r11
  adc r31, r1
  add r30, r11
  adc r31, r1
  lpm r20, Z+
  lpm r21, Z
  SCALE_ADD r20, r21
  ROTATE
  subi r17, 0x10
  brcc .Ltable
  rjmp .Lstore

  ; Generated square: both scaled levels are constant for the block.
.Lsquare:
  ldi r20, 0xFF
  mul r20, r16
  movw r14, r0
  lsr r15
  ror r14
  ldi r20, 128
  mul r16, r20
  movw r24, r0
  clr r1
  com r25
  neg r24
  sbci r25, 0xFF
  ldd r22, Y + VOICE_PULSE
  ldd r23, Y + VOICE_PULSE + 1
  ldi r17, 0x30
.Lsquare_loop:
  add r10, r12
  adc r11, r13
  cp r10, r22
  cpc r11, r23
  brsh 3f
  add r2, r14
  adc r3, r15
  rjmp 4f
3:
  add r2, r24
  adc r3, r25
4:
  ROTATE
  subi r17, 0x10
  brcc .Lsquare_loop
  rjmp .Lstore

  ; Generated white noise from the voice's 16-bit Galois LFSR.
.Lnoise:
  ldd r14, Y + VOICE_NOISE
  ldd r15, Y + VOICE_NOISE + 1
  ldi r17, 0x30
.Lnoise_loop:
  ldi r21, 0xB4
  lsr r15
  ror r14
  brcc 5f
  eor r15, r21
5:
  mov r20, r14
  mov r21, r15
  andi r21, 0x01
  dec r21
  SCALE_ADD r20, r21
  ROTATE
  subi r17, 0x10
  brcc .Lnoise_loop
  std Y + VOICE_NOISE, r14
  std Y + VOICE_NOISE + 1, r15
  rjmp .Ladvance

  ; Two-source waveform morph.
.Lmorph:
  lds r17, synthMorphKinds
  lds r19, synthMorphAmount
  lds r14, synthMorphSourceA
  lds r15, synthMorphSourceA + 1
  sbrs r17, KIND_A_SQUARE
  rjmp 6f
  ldd r14, Y + VOICE_PULSE
  ldd r15, Y + VOICE_PULSE + 1
6:
  sbrs r17, KIND_A_NOISE
  rjmp 7f
  ldd r14, Y + VOICE_NOISE
  ldd r15, Y + VOICE_NOISE + 1
7:
  lds r24, synthMorphSourceB
  lds r25, synthMorphSourceB + 1
  sbrs r17, KIND_B_SQUARE
  rjmp 8f
  ldd r24, Y + VOICE_PULSE
  ldd r25, Y + VOICE_PULSE + 1
8:
  sbrs r17, KIND_B_NOISE
  rjmp 9f
  ldd r24, Y + VOICE_NOISE
  ldd r25, Y + VOICE_NOISE + 1
9:
  tst r17
  breq 3f
  rjmp 1f
3:
  TT_SAMPLE r2, r3
  TT_SAMPLE r4, r5
  TT_SAMPLE r6, r7
  TT_SAMPLE r8, r9
  rjmp .Lstore
1:
  cpi r17, 1 << KIND_B_SQUARE
  breq 3f
  rjmp 2f
3:
  TS_SAMPLE r2, r3
  TS_SAMPLE r4, r5
  TS_SAMPLE r6, r7
  TS_SAMPLE r8, r9
  rjmp .Lstore
2:
  ori r17, 0x30
.Lmorph_loop:
  add r10, r12
  adc r11, r13
  FETCH KIND_A_SQUARE, KIND_A_NOISE, r14, r15, r20, r21
  FETCH KIND_B_SQUARE, KIND_B_NOISE, r24, r25, r22, r23
  BLEND
  SCALE_ADD r20, r21
  ROTATE
  subi r17, 0x10
  brcs 1f
  rjmp .Lmorph_loop
1:
  sbrs r17, KIND_A_NOISE
  rjmp 2f
  std Y + VOICE_NOISE, r14
  std Y + VOICE_NOISE + 1, r15
2:
  sbrs r17, KIND_B_NOISE
  rjmp .Lstore
  std Y + VOICE_NOISE, r24
  std Y + VOICE_NOISE + 1, r25
  rjmp .Lstore

  ; Silent or noise voices keep phase continuity without reading a table.
.Ladvance:
  movw r20, r12
  lsl r20
  rol r21
  lsl r20
  rol r21
  add r10, r20
  adc r11, r21

.Lstore:
  std Y + VOICE_PHASE, r10
  std Y + VOICE_PHASE + 1, r11
  adiw r28, VOICE_SIZE
  sbrc r18, SPLIT_PENDING
  rjmp .Lsplit
.Lnext_voice:
  dec r18
  breq 3f
  rjmp .Lvoice

  ; Store the first voice to dry[] (the saved r16:r17 argument, four and
  ; three bytes above the stack pointer) and restart the accumulators.
.Lsplit:
  bst r18, SPLIT_MORPH
  andi r18, 0x3F
  in r30, IO_SPL
  in r31, IO_SPH
  ldd r20, Z + 4
  ldd r21, Z + 3
  movw r30, r20
  st Z+, r2
  st Z+, r3
  st Z+, r4
  st Z+, r5
  st Z+, r6
  st Z+, r7
  st Z+, r8
  st Z, r9
  clr r2
  clr r3
  movw r4, r2
  movw r6, r2
  movw r8, r2
  rjmp .Lnext_voice
3:
  st X+, r2
  st X+, r3
  st X+, r4
  st X+, r5
  st X+, r6
  st X+, r7
  st X+, r8
  st X, r9
  pop r29
  pop r28
  pop r17
  pop r16
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop r7
  pop r6
  pop r5
  pop r4
  pop r3
  pop r2
  ret
  .size synthRenderVoices, . - synthRenderVoices

; void synthFinishBlock(int16_t *bus, const int16_t *dry, uint16_t *output,
;                       uint8_t flags);
;
; Finishes an eight-sample block in one pass. With FINISH_VCA the bus is
; scaled by the low byte of playVcaGain (1-255 or 0), with FINISH_CHORUS it
; passes through the 50% wet chorus, and with FINISH_DRY dry[] is added. The
; sum is dithered, quantized with error feedback, and written as 9-bit PWM
; compare values. Every step is bit-exact with the documented C formulas in
; synth.h.

FINISH_VCA = 0
FINISH_CHORUS = 1
FINISH_DRY = 2
CHORUS_SAMPLES = 108
; -SYNTH_CHORUS_MIN_DELAY_Q8 (8500) for subi/sbci addition.
CHORUS_MIN_DELAY_NEG = 0xDECC

; Register use
;   r2:r3 quantization error  r4:r5 output pointer   r6:r7 chorus dry half
;   r8..r11 chorus step        r12..r15 chorus phase  r16 VCA gain
;   r17 chorus write index     r19 flags | count      X bus pointer
;   Y dry pointer              Z delay line / output pointer

  .section .text.synthFinishBlock,"ax",@progbits
  .global synthFinishBlock
  .type synthFinishBlock, @function
synthFinishBlock:
  push r2
  push r3
  push r4
  push r5
  push r6
  push r7
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  push r16
  push r17
  push r28
  push r29
  movw r26, r24
  movw r28, r22
  movw r4, r20
  mov r19, r18
  ori r19, 0x70
  lds r2, pwmQuantizationError
  lds r3, pwmQuantizationError + 1
  andi r18, (1 << FINISH_VCA) | (1 << FINISH_CHORUS)
  brne 1f
  rjmp .Lquantize_only
1:
  sbrs r19, FINISH_VCA
  rjmp 1f
  lds r16, playVcaGain
1:
  sbrs r19, FINISH_CHORUS
  rjmp .Lfinish_loop
  lds r8, chorusLfoFTW
  lds r9, chorusLfoFTW + 1
  lds r10, chorusLfoFTW + 2
  lds r11, chorusLfoFTW + 3
  lds r12, chorusLfoPCW
  lds r13, chorusLfoPCW + 1
  lds r14, chorusLfoPCW + 2
  lds r15, chorusLfoPCW + 3
  lds r17, chorusWriteIndex

.Lfinish_loop:
  ld r24, X+
  ld r25, X+
  sbrs r19, FINISH_VCA
  rjmp .Lno_vca
  ; scalePlayMix: |x| high * gain + ((|x| low * (gain / 2) * 2 + 128) >> 8).
  tst r16
  brne 2f
  clr r24
  clr r25
  rjmp .Lno_vca
2:
  bst r25, 7
  brtc 3f
  com r25
  neg r24
  sbci r25, 0xFF
3:
  mul r25, r16
  movw r20, r0
  mov r18, r16
  lsr r18
  mul r24, r18
  movw r22, r0
  lsl r22
  rol r23
  subi r22, 0x80
  sbci r23, 0xFF
  clr r1
  add r20, r23
  adc r21, r1
  movw r24, r20
  brtc .Lno_vca
  com r25
  neg r24
  sbci r25, 0xFF
.Lno_vca:
  sbrs r19, FINISH_CHORUS
  rjmp .Lno_chorus
  ; Advance the LFO; its upper 16 bits, mirrored, form a 15-bit triangle.
  add r12, r8
  adc r13, r9
  adc r14, r10
  adc r15, r11
  movw r20, r14
  sbrs r15, 7
  rjmp 4f
  com r20
  com r21
4:
  ; high = triangle >> 7, low = triangle << 1;
  ; delay = 8500 + high*0x49 + (high*0xCC >> 8) + (low*0x49 >> 8).
  mov r22, r20
  lsl r22
  mov r23, r21
  rol r23
  ldi r18, 0x49
  mul r23, r18
  movw r20, r0
  mul r22, r18
  mov r22, r1
  ldi r18, 0xCC
  mul r23, r18
  add r20, r1
  clr r1
  adc r21, r1
  add r20, r22
  adc r21, r1
  subi r20, lo8(CHORUS_MIN_DELAY_NEG)
  sbci r21, hi8(CHORUS_MIN_DELAY_NEG)
  ; Write the dry half, then read the delayed pair. r21 = whole samples,
  ; r20 = Q8 fraction.
  movw r6, r24
  asr r7
  ror r6
  ldi r30, lo8(chorusBuffer)
  ldi r31, hi8(chorusBuffer)
  mov r18, r17
  lsl r18
  add r30, r18
  adc r31, r1
  st Z, r6
  std Z + 1, r7
  mov r18, r17
  sub r18, r21
  brcc 5f
  subi r18, 256 - CHORUS_SAMPLES
5:
  inc r17
  cpi r17, CHORUS_SAMPLES
  brne 6f
  clr r17
6:
  mov r22, r18
  subi r22, 1
  brcc 7f
  ldi r22, CHORUS_SAMPLES - 1
7:
  ldi r30, lo8(chorusBuffer)
  ldi r31, hi8(chorusBuffer)
  lsl r18
  add r30, r18
  adc r31, r1
  ld r24, Z
  ldd r25, Z + 1
  ldi r30, lo8(chorusBuffer)
  ldi r31, hi8(chorusBuffer)
  lsl r22
  add r30, r22
  adc r31, r1
  ld r22, Z
  ldd r23, Z + 1
  ; first + (next - first) * fraction / 256, exact, then add the dry half.
  sub r22, r24
  sbc r23, r25
  mov r18, r20
  mulsu r23, r18
  add r24, r0
  adc r25, r1
  mul r22, r18
  add r24, r1
  clr r1
  adc r25, r1
  add r24, r6
  adc r25, r7
.Lno_chorus:
  sbrs r19, FINISH_DRY
  rjmp 1f
  ld r20, Y+
  ld r21, Y+
  add r24, r20
  adc r25, r21
1:
  ; 8-bit Galois LFSR dither: ((low nibble - high nibble) << 2).
  lds r18, pwmDitherLfsr
  lsr r18
  brcc 2f
  ldi r20, 0xB8
  eor r18, r20
2:
  sts pwmDitherLfsr, r18
  mov r20, r18
  andi r20, 0x0F
  mov r21, r18
  swap r21
  andi r21, 0x0F
  sub r20, r21
  sbc r21, r21
  lsl r20
  rol r21
  lsl r20
  rol r21
  add r24, r2
  adc r25, r3
  add r24, r20
  adc r25, r21
  ; t = sample + 64; error = (t & 127) - 64; PWM = (t >> 7) + 256.
  ; t >> 7 is always -256..255, so the PWM value needs no clipping.
  subi r24, 0xC0
  sbci r25, 0xFF
  mov r2, r24
  andi r24, 0x7F
  subi r24, 64
  sbc r3, r3
  mov r20, r24
  lsl r2
  rol r25
  sbc r21, r21
  inc r21
  movw r30, r4
  st Z+, r25
  st Z+, r21
  movw r4, r30
  mov r2, r20
  subi r19, 0x10
  brcs 8f
  rjmp .Lfinish_loop
8:
  sts pwmQuantizationError, r2
  sts pwmQuantizationError + 1, r3
  rjmp .Lfinish_effects_done

  ; Without bus effects: bus (+ dry) straight to the quantizer, with the
  ; dither LFSR kept in r18.
.Lquantize_only:
  lds r18, pwmDitherLfsr
  movw r30, r4
.Lquantize_loop:
  ld r24, X+
  ld r25, X+
  sbrs r19, FINISH_DRY
  rjmp 1f
  ld r20, Y+
  ld r21, Y+
  add r24, r20
  adc r25, r21
1:
  lsr r18
  brcc 2f
  ldi r20, 0xB8
  eor r18, r20
2:
  mov r20, r18
  andi r20, 0x0F
  mov r21, r18
  swap r21
  andi r21, 0x0F
  sub r20, r21
  sbc r21, r21
  lsl r20
  rol r21
  lsl r20
  rol r21
  add r24, r2
  adc r25, r3
  add r24, r20
  adc r25, r21
  subi r24, 0xC0
  sbci r25, 0xFF
  mov r20, r24
  andi r20, 0x7F
  subi r20, 64
  sbc r3, r3
  mov r2, r20
  lsl r24
  rol r25
  sbc r21, r21
  inc r21
  st Z+, r25
  st Z+, r21
  subi r19, 0x10
  brcc .Lquantize_loop
  sts pwmQuantizationError, r2
  sts pwmQuantizationError + 1, r3
  sts pwmDitherLfsr, r18

.Lfinish_effects_done:
  sbrs r19, FINISH_CHORUS
  rjmp 9f
  sts chorusLfoPCW, r12
  sts chorusLfoPCW + 1, r13
  sts chorusLfoPCW + 2, r14
  sts chorusLfoPCW + 3, r15
  sts chorusWriteIndex, r17
9:
  pop r29
  pop r28
  pop r17
  pop r16
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop r7
  pop r6
  pop r5
  pop r4
  pop r3
  pop r2
  ret
  .size synthFinishBlock, . - synthFinishBlock

; void synthFilterBlock(int16_t *bus, uint8_t coefficient);
;
; One-pole low-pass over an eight-sample bus block, in place. With
; k = coefficient (1-255) and y = busFilterState, per sample exactly:
;   d = (x >> 1) - (y >> 1); y = y + ((d * k) >> 7) modulo 2^16; x = y
; d * k is a signed 24-bit product built from the low and signed high
; partial products; its bits 7..22 are the 16-bit increment.
;
; Register use
;   r18:r19 state y   r20:r21 x, then d   r22 k   r23 count
;   r24:r25 product   X bus read pointer  Z bus write pointer

  .section .text.synthFilterBlock,"ax",@progbits
  .global synthFilterBlock
  .type synthFilterBlock, @function
synthFilterBlock:
  movw r26, r24
  movw r30, r24
  lds r18, busFilterState
  lds r19, busFilterState + 1
  ldi r23, 8
1:
  ld r20, X+
  ld r21, X+
  asr r21
  ror r20
  movw r24, r18
  asr r25
  ror r24
  sub r20, r24
  sbc r21, r25
  mul r20, r22
  movw r24, r0
  mulsu r21, r22
  add r25, r0
  clr r20
  adc r1, r20
  lsl r24
  rol r25
  rol r1
  add r18, r25
  adc r19, r1
  st Z+, r18
  st Z+, r19
  dec r23
  brne 1b
  clr r1
  sts busFilterState, r18
  sts busFilterState + 1, r19
  ret
  .size synthFilterBlock, . - synthFilterBlock

; void synthUpdateVoices(int8_t vibratoLfo);
;
; Once per block, for each of the four voices: advances the envelope, looks
; up its value, and scales it by the voice level to
; amp = envelope * (level + 1) / 256, so level 255 keeps the envelope exact;
; then it sets the oscillator step from the pitch plus
; the exact floor(vibratoPeak * vibratoLfo / 256) vibrato offset.

  .section .text.synthUpdateVoices,"ax",@progbits
  .global synthUpdateVoices
  .type synthUpdateVoices, @function
synthUpdateVoices:
  push r28
  push r29
  mov r23, r24
  ldi r28, lo8(synthVoices)
  ldi r29, hi8(synthVoices)
  ldi r25, VOICE_COUNT
.Lcontrol_voice:
  ldd r20, Y + VOICE_ENV_PHASE
  ldd r21, Y + VOICE_ENV_PHASE + 1
  clr r22
  sbrc r21, 7
  rjmp 1f
  ldd r18, Y + VOICE_ENV_STEP
  ldd r19, Y + VOICE_ENV_STEP + 1
  add r20, r18
  adc r21, r19
  std Y + VOICE_ENV_PHASE, r20
  std Y + VOICE_ENV_PHASE + 1, r21
  sbrc r21, 7
  rjmp 1f
  ldd r30, Y + VOICE_ENVELOPE
  ldd r31, Y + VOICE_ENVELOPE + 1
  add r30, r21
  adc r31, r1
  lpm r22, Z
  ldd r18, Y + VOICE_LEVEL
  mul r22, r18
  add r0, r22
  clr r22
  adc r22, r1
  clr r1
1:
  std Y + VOICE_AMP, r22
  ldd r20, Y + VOICE_PITCH
  ldd r21, Y + VOICE_PITCH + 1
  ldd r18, Y + VOICE_VIBRATO_DEPTH
  tst r18
  breq 2f
  ldd r18, Y + VOICE_VIBRATO_PEAK
  ldd r19, Y + VOICE_VIBRATO_PEAK + 1
  mulsu r23, r19
  add r20, r0
  adc r21, r1
  mulsu r23, r18
  mov r0, r1
  lsl r0
  sbc r0, r0
  add r20, r1
  adc r21, r0
  clr r1
2:
  std Y + VOICE_STEP, r20
  std Y + VOICE_STEP + 1, r21
  adiw r28, VOICE_SIZE
  dec r25
  brne .Lcontrol_voice
  pop r29
  pop r28
  ret
  .size synthUpdateVoices, . - synthUpdateVoices

; void synthRenderDrum(int16_t *accumulator);
;
; Stores the active drum's four-sample block. Per sample, while the envelope
; is nonzero and the sample index is inside the sample, it reads the packed
; offset-binary 9-bit sample, scales it by the envelope's high byte with
; floor(sample * gain / 2), advances the 16.16 sample phase, and decays the
; envelope. When the end is reached, drumActive is cleared and the rest of the
; block is silent, exactly like the C reference.
;
; A packed sample spans bytes index + index / 8 and the next one; multiplying
; that word by 128 >> (index & 7) aligns its nine bits at 7..15.

  .section .text.synthRenderDrum,"ax",@progbits
  .global synthRenderDrum
  .type synthRenderDrum, @function
synthRenderDrum:
  push r6
  push r7
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  push r16
  push r17
  push r28
  push r29
  movw r26, r24
  lds r18, drumSamplePhase
  lds r19, drumSamplePhase + 1
  lds r20, drumSamplePhase + 2
  lds r21, drumSamplePhase + 3
  lds r22, drumSampleStep
  lds r23, drumSampleStep + 1
  lds r24, drumSampleStep + 2
  lds r25, drumSampleStep + 3
  lds r16, drumEnvelope
  lds r17, drumEnvelope + 1
  lds r14, drumEnvelopeStep
  lds r15, drumEnvelopeStep + 1
  lds r12, drumSampleLength
  lds r13, drumSampleLength + 1
  lds r10, drumSampleAddress
  lds r11, drumSampleAddress + 1
  clt
  ldi r29, 4
.Ldrum_sample:
  cp r16, r1
  cpc r17, r1
  breq .Ldrum_end
  cp r20, r12
  cpc r21, r13
  brsh .Ldrum_end
  movw r30, r20
  lsr r31
  ror r30
  lsr r31
  ror r30
  lsr r31
  ror r30
  add r30, r20
  adc r31, r21
  add r30, r10
  adc r31, r11
  lpm r8, Z+
  lpm r9, Z
  mov r30, r20
  andi r30, 0x07
  clr r31
  subi r30, lo8(-(synthPackedSampleScales))
  sbci r31, hi8(-(synthPackedSampleScales))
  lpm r28, Z
  mul r8, r28
  movw r6, r0
  mul r9, r28
  add r7, r0
  ; (aligned >> 7) - 256: low byte r7 after the shift, high byte carry - 1.
  lsl r6
  rol r7
  sbc r9, r9
  com r9
  mul r7, r17
  and r9, r17
  sub r1, r9
  ror r1
  ror r0
  st X+, r0
  st X+, r1
  clr r1
  add r18, r22
  adc r19, r23
  adc r20, r24
  adc r21, r25
  cp r14, r16
  cpc r15, r17
  brlo 1f
  clr r16
  clr r17
  rjmp 2f
1:
  sub r16, r14
  sbc r17, r15
2:
  dec r29
  brne .Ldrum_sample
  rjmp .Ldrum_done
.Ldrum_end:
  set
  clr r16
  clr r17
  st X+, r1
  st X+, r1
  dec r29
  brne .Ldrum_end
.Ldrum_done:
  sts drumSamplePhase, r18
  sts drumSamplePhase + 1, r19
  sts drumSamplePhase + 2, r20
  sts drumSamplePhase + 3, r21
  sts drumEnvelope, r16
  sts drumEnvelope + 1, r17
  brtc 3f
  sts drumActive, r1
3:
  pop r29
  pop r28
  pop r17
  pop r16
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop r7
  pop r6
  ret
  .size synthRenderDrum, . - synthRenderDrum
