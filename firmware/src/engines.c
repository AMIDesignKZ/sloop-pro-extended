/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* System 37 engine table: Analog and FM6 only. */
#include "dsp.c"
/* FM6 has one bounded state arena per synth part. Analog keeps its state in voice_t; drums use drums.v[]. */
static void *eng_arena_of(const track_t *t, uint32_t eng);
/* (pitch_inc: felucca_tables.h, SLOOP 2.5: from the top octave) */
/* (after Clipper 1.2) UNISON's start-phase spread (voice.c unison_spread): r (Q32: a turn of the note at pitch p16,
 * 1/16 semitone) in turns of an oscillator d16 (1/16 semitone) above it, the share of the same time; per note */
static uint32_t uni_turns(uint32_t r, int32_t p16, int32_t d16)
{
    uint32_t b = pitch_inc((uint32_t)clamp(p16, 0, 2047));
    return b ? (uint32_t)(((uint64_t)r * pitch_inc((uint32_t)clamp(p16 + d16, 0, 2047))) / b) : r;
}

#include "eng_analog.c"
#include "eng_fm6.c"

static fm6_part_t fm6_arena[NPART] __attribute__((section(".pool")));
static uint8_t eng_arena_own[NPART];
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NPART || eng >= NENGINES)
        return 0;
    if (eng_arena_own[p] != eng + 1u) {
        memset(&fm6_arena[p], 0, sizeof fm6_arena[p]);
        eng_arena_own[p] = (uint8_t)(eng + 1u);
    }
    return eng == ENGI_FM6 ? &fm6_arena[p] : 0;
}

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_FM6};
_Static_assert(ENGI_ANALOG == 0u && ENGI_FM6 == 1u && NENGINES == 2u, "System 37 engine IDs");

#include "preset_trim.h"
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
    uint32_t legacy = e == ENGI_FM6 ? 9u : 0u;
    return pi < PT_MAX ? PRESET_TRIM[legacy][pi] : 0;
}
