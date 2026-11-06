/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: host-testable FUN6 -> FUN7 migration.
 */
#include <string.h>
#include "project_v7.h"

uint32_t proj_v7_hash(const void *data, uint32_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i, hash = 0x811C9DC5u;
    for (i = 0; i < length; ++i)
        hash = (hash ^ p[i]) * 16777619u;
    return hash;
}

int proj_v7_valid(const project_v7_t *p)
{
    return p && p->magic == PROJ_V7_MAGIC && p->size == sizeof *p &&
           p->sum == proj_v7_hash(p, (uint32_t)sizeof *p - 4u);
}

/* Match seq.c p_lockable(): sequencing, arp, key and voice-mode controls
 * must not become parameter locks during migration. */
static int proj_v7_lockable(uint32_t id)
{
    return id <= P_LD_AMP || id == P_SGATE || (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
           id == P_DETUNE || (id >= P_SLCR && id <= P_SLDEPTH) || (id >= P_E0 && id <= P_E7) || id == P_TFLT ||
           (id >= P_IA && id <= P_IMIX);
}

/* Insert in ascending step order. Strictly-greater comparison keeps equal-step
 * locks in their original source-slot order. */
static void keep_lock(proj_trk_v7_t *dst, const plock_t *src, uint32_t *count)
{
    uint32_t at, limit = *count;
    if (src->step == LOCK_FREE || src->step >= NSTEP || src->param >= P_COUNT || !proj_v7_lockable(src->param))
        return;
    if (limit == PROJ_V7_LOCKS) {
        /* The list is sorted: a later/equal step cannot displace an earlier
         * lock. Equal-step arrivals are later source slots, so ties stay stable. */
        if (src->step >= dst->lock[limit - 1u].step)
            return;
        limit--;
    }
    at = limit;
    while (at && dst->lock[at - 1u].step > src->step) {
        if (at < PROJ_V7_LOCKS)
            dst->lock[at] = dst->lock[at - 1u];
        --at;
    }
    dst->lock[at] = *src;
    if (*count < PROJ_V7_LOCKS)
        ++*count;
}

int proj_v6_to_v7(project_v7_t *out, const void *data, uint32_t length)
{
    const project_v6_t *src = (const project_v6_t *)data;
    uint32_t t, s, k;
    if (!out || !data || length != sizeof(project_v6_t) ||
        src->magic != PROJ_V6_MAGIC || src->size != sizeof *src ||
        src->sum != proj_v7_hash(src, (uint32_t)sizeof *src - 4u))
        return 0;

    memset(out, 0, sizeof *out);
    out->magic = PROJ_V7_MAGIC;
    out->size = sizeof *out;
    memcpy(out->g, src->g, sizeof out->g);
    out->sel = src->sel;
    out->drdly = src->drdly;
    memcpy(out->rsv, src->rsv, sizeof out->rsv);

    for (t = 0; t < PROJ_V7_TRACKS; ++t) {
        uint32_t source_track;
        proj_trk_v7_t *b = &out->t[t];
        uint32_t kept = 0;

        /* System 37 orders five synth parts first and the dedicated drums
         * last. The old SLOOP drum track (index 3) therefore moves to 5. */
        if (t == 3u || t == 4u) {
            b->engine = 0u;       /* ANALOG is the neutral initial synth engine */
            b->preset = 0xFFu;    /* runtime resolves this as the default sound */
            for (s = 0; s < NSTEP; ++s)
                b->step[s].time = ST_REST;
            for (k = 0; k < PROJ_V7_LOCKS; ++k)
                b->lock[k].step = LOCK_FREE;
            continue;
        }
        source_track = (t == 5u) ? 3u : t;
        {
            const proj_trk_v6_t *a = &src->t[source_track];
            memcpy(b->p, a->p, sizeof b->p);
            memcpy(b->e, a->e, sizeof b->e);
            memcpy(b->x, a->x, sizeof b->x);
            b->engine = a->engine;
            b->preset = a->preset;
            memcpy(b->step, a->step, sizeof b->step);

            /* Preserve all fill conditions, discard the six-bit microtiming. */
            for (s = 0; s < NSTEP; ++s) {
                uint8_t fill = (uint8_t)(a->mf[s] >> 6);
                b->fill[s / 4u] |= (uint8_t)(fill << (2u * (s % 4u)));
            }

            for (k = 0; k < NLOCK; ++k)
                keep_lock(b, &a->lock[k], &kept);
            for (; kept < PROJ_V7_LOCKS; ++kept) {
                b->lock[kept].step = LOCK_FREE;
                b->lock[kept].param = 0;
                b->lock[kept].val = 0;
            }
        }
    }

    out->sum = proj_v7_hash(out, (uint32_t)sizeof *out - 4u);
    return 1;
}
