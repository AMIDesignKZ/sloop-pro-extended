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
    uint32_t t, s, k;
    int bad = 0, ok = 1;

    old.magic = PROJ_V6_MAGIC;
    old.size = sizeof old;
    old.sel = 2;
    old.drdly = 37;
    for (k = 0; k < 32u; ++k) old.g[k] = (int16_t)(k * 3u - 12u);

    for (t = 0; t < 4u; ++t) {
        proj_trk_v6_t *tr = &old.t[t];
        for (k = 0; k < PROJ_V6_NC; ++k) tr->p[k] = (int16_t)(t * 100u + k);
        for (k = 0; k < 8u; ++k) tr->e[k] = (int16_t)(-((int32_t)t * 10 + (int32_t)k));
        for (k = 0; k < PROJ_V6_NX; ++k) tr->x[k] = (uint8_t)(k + t);
        tr->engine = (uint8_t)(t + 2u);
        tr->preset = (uint8_t)(t + 10u);
        for (s = 0; s < NSTEP; ++s) {
            tr->mf[s] = (uint8_t)(((s + t) & 63u) | ((s % 3u) << 6));
            tr->step[s].note[0] = (uint8_t)(36u + s);
            tr->step[s].n = 1;
            tr->step[s].time = ST_NOTE;
        }
        for (k = 0; k < NLOCK; ++k) {
            tr->lock[k].step = LOCK_FREE;
            tr->lock[k].param = 0;
            tr->lock[k].val = 0;
        }
        /* Deliberately unsorted; duplicate-step ties must retain slot order. */
        tr->lock[0] = (plock_t){20, 2, 200};
        tr->lock[1] = (plock_t){3, 4, 34};
        tr->lock[2] = (plock_t){3, 5, 35};
        tr->lock[3] = (plock_t){63, 6, 636};
        tr->lock[4] = (plock_t){1, 7, 17};
        tr->lock[5] = (plock_t){64, 8, 648}; /* malformed step: ignored */
        tr->lock[6] = (plock_t){5, 255, 55}; /* malformed parameter: ignored */
        for (k = 7; k < NLOCK; ++k)
            tr->lock[k] = (plock_t){(uint8_t)(10u + k), (uint8_t)(k % P_COUNT), (int16_t)k};
    }
    old.sum = proj_v7_hash(&old, sizeof old - 4u);

    bad += check("wire layouts: FUN6 3816 bytes; FUN7 5108 bytes; 6 tracks x 838", 
                 sizeof old == 3816u && sizeof now == 5108u && sizeof(proj_trk_v7_t) == 838u);
    bad += check("valid FUN6 image migrates to valid FUN7", proj_v6_to_v7(&now, &old, sizeof old) && proj_v7_valid(&now));
    ok = now.sel == old.sel && now.drdly == old.drdly && !memcmp(now.g, old.g, sizeof old.g);
    for (t = 0; t < 4u; ++t) {
        const proj_trk_v6_t *a = &old.t[t];
        const proj_trk_v7_t *b = &now.t[t];
        ok &= !memcmp(a->p, b->p, sizeof a->p) && !memcmp(a->e, b->e, sizeof a->e) &&
              !memcmp(a->x, b->x, sizeof a->x) && a->engine == b->engine && a->preset == b->preset &&
              !memcmp(a->step, b->step, sizeof a->step);
        for (s = 0; s < NSTEP; ++s)
            ok &= ((b->fill[s / 4u] >> (2u * (s % 4u))) & 3u) == (a->mf[s] >> 6);
        ok &= b->lock[0].step == 1 && b->lock[0].val == 17 &&
              b->lock[1].step == 3 && b->lock[1].val == 34 &&
              b->lock[2].step == 3 && b->lock[2].val == 35 &&
              b->lock[3].step == 17 && b->lock[3].val == 7 &&
              b->lock[6].step == 20 && b->lock[6].val == 200 &&
              b->lock[7].step == 20 && b->lock[7].val == 10;
        for (k = 4; k < PROJ_V7_LOCKS; ++k)
            ok &= b->lock[k].step == LOCK_FREE;
    }
    bad += check("all four legacy tracks, parameters, engine IDs and 64-step patterns preserved", ok);
    bad += check("fill conditions retained in 16 packed bytes; microtiming removed",
                 now.t[0].fill[0] != 0 && now.t[0].fill[15] != 0);
    bad += check("locks sorted by step, stable ties, earliest 12 retained, invalid entries skipped",
                 now.t[0].lock[0].step == 1 && now.t[0].lock[1].step == 3 &&
                 now.t[0].lock[2].step == 3 && now.t[0].lock[3].step == 17 &&
                 now.t[0].lock[11].step == 24);
    bad += check("tracks 5 and 6 start empty with all locks free",
                 now.t[4].lock[0].step == LOCK_FREE && now.t[5].lock[11].step == LOCK_FREE);
    old.t[0].mf[0] ^= 1u;
    bad += check("damaged FUN6 checksum rejected", !proj_v6_to_v7(&now, &old, sizeof old));
    old.t[0].mf[0] ^= 1u;
    bad += check("wrong input length rejected", !proj_v6_to_v7(&now, &old, sizeof old - 1u));
    bad += check("null arguments rejected", !proj_v6_to_v7(NULL, &old, sizeof old) &&
                 !proj_v6_to_v7(&now, NULL, sizeof old));
    return bad ? 1 : 0;
}
