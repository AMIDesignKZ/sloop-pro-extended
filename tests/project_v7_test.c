/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests for the isolated System 37 FUN6 -> FUN7 migration.
 */
#include <stdio.h>
#include <string.h>
#include "project_v7.h"

static int check(const char *name, int ok)
{
    printf("%-62s %s\n", name, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    static project_v6_t old;
    static project_v7_t now;
    static param_desc_t common_defaults[P_E0], analog_defaults[8];
    uint32_t t, s, k;
    uint8_t lock_loss = 0;
    int bad = 0, ok = 1;
    for (k = 0; k < P_E0; ++k) common_defaults[k].def = (int16_t)(k + 10u);
    for (k = 0; k < 8u; ++k) analog_defaults[k].def = (int16_t)((int32_t)k - 3);

    old.magic = PROJ_V6_MAGIC;
    old.size = sizeof old;
    old.sel = 3;
    old.drdly = 37;
    for (k = 0; k < 32u; ++k) old.g[k] = (int16_t)(k * 3u - 12u);

    for (t = 0; t < 4u; ++t) {
        proj_trk_v6_t *tr = &old.t[t];
        for (k = 0; k < PROJ_V6_NC; ++k) tr->p[k] = (int16_t)((int32_t)t * 10 + (int32_t)k - 40);
        for (k = 0; k < 8u; ++k) tr->e[k] = (int16_t)(-((int32_t)t * 10 + (int32_t)k));
        for (k = 0; k < PROJ_V6_NX; ++k) tr->x[k] = (uint8_t)(k + t);
        tr->engine = t == PROJ_V6_DRUM_TRACK ? 0u : (uint8_t)(t + 2u);
        tr->preset = t == PROJ_V6_DRUM_TRACK ? 0u : (uint8_t)(t + 10u);
        for (s = 0; s < NSTEP; ++s) {
            tr->mf[s] = (uint8_t)(((s + t) & 63u) | ((s % 4u) << 6));
            tr->step[s].note[0] = (uint8_t)(36u + s);
            tr->step[s].n = 1;
            tr->step[s].time = ST_NOTE;
        }
        for (k = 0; k < PROJ_V6_LOCKS; ++k) {
            tr->lock[k].step = LOCK_FREE;
            tr->lock[k].param = 0;
            tr->lock[k].val = 0;
        }
        /* Deliberately unsorted; duplicate-step ties must retain slot order. */
        tr->lock[0] = (plock_t){20, P_DIST, 200};
        tr->lock[1] = (plock_t){3, P_REV, 34};
        tr->lock[2] = (plock_t){3, P_GLIDE, 35};
        tr->lock[3] = (plock_t){63, P_PAN, 636};
        tr->lock[4] = (plock_t){1, P_E0, 17};
        tr->lock[5] = (plock_t){64, P_E1, 648}; /* malformed step: ignored */
        tr->lock[6] = (plock_t){5, 255, 55}; /* malformed parameter: ignored */
        for (k = 7; k < PROJ_V6_LOCKS; ++k)
            tr->lock[k] = (plock_t){(uint8_t)(10u + k), (uint8_t)(P_E0 + (k % 8u)), (int16_t)k};
    }
    /* Give the old drum track real lane data (rather than synth-note bytes). */
    for (s = 0; s < NSTEP; ++s) {
        dstep_t *d = &old.t[3].dstep[s];
        memset(d, 0, sizeof *d);
        d->on[0] = (uint8_t)(1u << (s & 7u));
        d->lvl[0] = (uint8_t)((s & 3u) << (2u * (s & 3u)));
        d->rat[0] = (uint8_t)(((s + 1u) & 3u) << (2u * (s & 3u)));
    }
    old.sum = proj_v7_hash(&old, sizeof old - 4u);

    bad += check("wire layouts: FUN6 3816 bytes; FUN7 5366 bytes; 6 tracks x 881", 
                 sizeof old == 3816u && sizeof now == 5366u && sizeof(proj_trk_v7_t) == 881u);
    bad += check("valid FUN6 image migrates to valid FUN7", proj_v6_to_v7(&now, &old, sizeof old, &lock_loss, common_defaults, analog_defaults) && proj_v7_valid(&now));
    bad += check("all valid legacy locks fit without migration loss", lock_loss == 0u);
    ok = now.sel == 5u && now.drdly == old.drdly && !memcmp(now.g, old.g, sizeof old.g);
    for (t = 0; t < 3u; ++t) {
        const proj_trk_v6_t *a = &old.t[t];
        const proj_trk_v7_t *b = &now.t[t];
        for (k = 0; k < PROJ_V6_NC; ++k) ok &= b->p[k] == a->p[k];
        ok &= !memcmp(a->e, b->e, sizeof a->e) &&
              !memcmp(a->x, b->x, sizeof a->x) && a->engine == b->engine && a->preset == b->preset &&
              !memcmp(a->step, b->step, sizeof a->step);
        for (s = 0; s < NSTEP; ++s) {
            int32_t micro = a->mf[s] & 63u;
            if (micro >= 32) micro -= 64;
            ok &= proj_v7_micro_get(b, s) == micro;
            ok &= ((b->fill[s / 4u] >> (2u * (s % 4u))) & 3u) == (a->mf[s] >> 6);
        }
        ok &= b->lock[0].step == 1 && b->lock[0].val == 17 &&
              b->lock[1].step == 3 && b->lock[1].val == 34 &&
              b->lock[2].step == 3 && b->lock[2].val == 35 &&
              b->lock[3].step == 17 && b->lock[3].val == 7 &&
              b->lock[6].step == 20 && b->lock[6].param == P_DIST && b->lock[6].val == 200 &&
              b->lock[7].step == 20 && b->lock[7].param == P_E2 && b->lock[7].val == 10;
    }
    bad += check("three legacy synth tracks, parameters, engine IDs and 64-step patterns preserved", ok);
    bad += check("fill conditions and signed six-bit microtiming both retained",
                 now.t[0].fill[0] != 0 && now.t[0].fill[15] != 0 && proj_v7_micro_get(&now.t[0], 0u) == 0 && proj_v7_micro_get(&now.t[0], 63u) == -1);
    bad += check("locks sorted by step with stable ties; all 22 valid locks retained",
                 now.t[0].lock[0].step == 1 && now.t[0].lock[1].step == 3 &&
                 now.t[0].lock[2].step == 3 && now.t[0].lock[3].step == 17 &&
                 now.t[0].lock[6].step == 20 && now.t[0].lock[6].param == P_DIST &&
                 now.t[0].lock[7].step == 20 && now.t[0].lock[7].param == P_E2 &&
                 now.t[0].lock[21].step == 63);
    bad += check("new synth tracks 4–5 start empty with defaults",
                 now.t[3].engine == 0 && now.t[3].preset == 0xFFu && now.t[3].p[0] == common_defaults[0].def &&
                 now.t[3].x[0] == common_defaults[PROJ_V6_NC].def && now.t[3].e[0] == analog_defaults[0].def &&
                 now.t[4].engine == 0 && now.t[4].preset == 0xFFu &&
                 now.t[3].step[0].time == ST_REST && now.t[4].step[63].time == ST_REST &&
                 now.t[3].lock[0].step == LOCK_FREE && now.t[4].lock[23].step == LOCK_FREE);
    /* The legacy drum track must move to track 6 intact, not be silently reset. */
    {
        const proj_trk_v6_t *a = &old.t[3];
        const proj_trk_v7_t *b = &now.t[PROJ_V7_DRUM_TRACK];
        for (k = 0; k < PROJ_V6_NC; ++k) ok &= b->p[k] == a->p[k];
        ok &= !memcmp(a->e, b->e, sizeof a->e) &&
             !memcmp(a->x, b->x, sizeof a->x) && a->engine == b->engine && a->preset == b->preset &&
             !memcmp(a->dstep, b->dstep, sizeof a->dstep) &&
             b->lock[0].step == 1 && b->lock[21].step == 63 &&
             b->lock[22].step == LOCK_FREE && b->lock[23].step == LOCK_FREE;
        for (s = 0; s < NSTEP; ++s) {
            int32_t micro = a->mf[s] & 63u;
            if (micro >= 32) micro -= 64;
            ok &= proj_v7_micro_get(b, s) == micro;
            ok &= ((b->fill[s / 4u] >> (2u * (s % 4u))) & 3u) == (a->mf[s] >> 6);
        }
    }
    bad += check("legacy drum kit, parameters, 64-step lane pattern and fill move to track 6", ok);
    /* All 24 valid legacy locks now fit in FUN7 without truncation. */
    for (k = 0; k < PROJ_V6_LOCKS; ++k)
        old.t[0].lock[k] = (plock_t){(uint8_t)(23u - k), (uint8_t)(P_E0 + (k % 8u)), (int16_t)k};
    old.sum = proj_v7_hash(&old, sizeof old - 4u);
    lock_loss = 0xFFu;
    ok = proj_v6_to_v7(&now, &old, sizeof old, &lock_loss, common_defaults, analog_defaults) &&
         lock_loss == 0u && now.t[0].lock[0].step == 0 && now.t[0].lock[23].step == 23;
    bad += check("all 24 valid locks migrate without truncation", ok);

    for (k = 0; k < PROJ_V6_LOCKS; ++k)
        old.t[0].lock[k] = (plock_t){LOCK_FREE, 0, 0};
    old.sum = proj_v7_hash(&old, sizeof old - 4u);
    lock_loss = 0;
    ok = proj_v6_to_v7(&now, &old, sizeof old, &lock_loss, common_defaults, analog_defaults) &&
         !(lock_loss & 1u) && now.t[0].lock[0].step == LOCK_FREE &&
         now.t[0].lock[23].step == LOCK_FREE;
    bad += check("empty lock array remains empty", ok);

    old.t[0].mf[0] ^= 1u;
    bad += check("damaged FUN6 checksum rejected", !proj_v6_to_v7(&now, &old, sizeof old, NULL, common_defaults, analog_defaults));
    old.t[0].mf[0] ^= 1u;
    bad += check("wrong input length rejected", !proj_v6_to_v7(&now, &old, sizeof old - 1u, NULL, common_defaults, analog_defaults));
    bad += check("null arguments rejected", !proj_v6_to_v7(NULL, &old, sizeof old, NULL, common_defaults, analog_defaults) &&
                 !proj_v6_to_v7(&now, NULL, sizeof old, NULL, common_defaults, analog_defaults));
    return bad ? 1 : 0;
}
