/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: host-testable FUN6 -> FUN7 migration.
 */
#include <stddef.h>
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

int proj_v6_to_v7(project_v7_t *out, const void *data, uint32_t length, uint8_t *lock_loss_mask, const param_desc_t *common_defaults, const param_desc_t *analog_defaults)
{
    const uint8_t *raw = (const uint8_t *)data;
    uint32_t magic, size, sum;
    uint32_t t, s, k, id;
    if (!out || !data || !common_defaults || !analog_defaults || length != sizeof(project_v6_t))
        return 0;

    /* Read the old image as bytes: callers may pass a project_t object whose
     * effective C type differs even though its frozen FUN6 layout is exact. */
    memcpy(&magic, raw + offsetof(project_v6_t, magic), sizeof magic);
    memcpy(&size, raw + offsetof(project_v6_t, size), sizeof size);
    memcpy(&sum, raw + offsetof(project_v6_t, sum), sizeof sum);
    if (magic != PROJ_V6_MAGIC || size != sizeof(project_v6_t) ||
        sum != proj_v7_hash(raw, (uint32_t)sizeof(project_v6_t) - 4u))
        return 0;

    if (lock_loss_mask)
        *lock_loss_mask = 0;
    memset(out, 0, sizeof *out);
    out->magic = PROJ_V7_MAGIC;
    out->size = sizeof *out;
    memcpy(out->g, raw + offsetof(project_v6_t, g), sizeof out->g);
    memcpy(&out->sel, raw + offsetof(project_v6_t, sel), sizeof out->sel);
    if (out->sel == PROJ_V6_DRUM_TRACK)
        out->sel = PROJ_V7_DRUM_TRACK; /* keep the selected legacy drum track selected */
    else if (out->sel > 2u)
        out->sel = 0u;
    memcpy(&out->drdly, raw + offsetof(project_v6_t, drdly), sizeof out->drdly);
    memcpy(out->rsv, raw + offsetof(project_v6_t, rsv), sizeof out->rsv);

    for (t = 0; t < PROJ_V7_TRACKS; ++t) {
        uint32_t source_track;
        proj_trk_v7_t *b = &out->t[t];
        uint32_t kept = 0, valid_locks = 0;

        /* The two new synth tracks start empty. Preserve all four legacy tracks:
         * the three synths keep their indices and the old drum track moves from
         * index 3 to the dedicated drum slot at index 5. */
        if (t == 3u || t == 4u) {
            uint32_t id;
            b->engine = 0u;
            b->preset = 0xFFu;
            for (id = 0; id < P_E0; ++id) {
                int16_t value = common_defaults[id].def;
                if (id < PROJ_V6_NC)
                    b->p[id] = value;
                else
                    b->x[id - PROJ_V6_NC] = (uint8_t)value;
            }
            for (id = 0; id < 8u; ++id)
                b->e[id] = analog_defaults[id].def;
            for (s = 0; s < NSTEP; ++s)
                b->step[s].time = ST_REST;
            for (k = 0; k < PROJ_V7_LOCKS; ++k)
                b->lock[k].step = LOCK_FREE;
            continue;
        }

        source_track = t == PROJ_V7_DRUM_TRACK ? PROJ_V6_DRUM_TRACK : t;
        {
            const uint8_t *a = raw + offsetof(project_v6_t, t) + source_track * sizeof(proj_trk_v6_t);
            plock_t lock;
            for (id = 0; id < PROJ_V6_NC; ++id) {
                int16_t value;
                memcpy(&value, a + offsetof(proj_trk_v6_t, p) + id * sizeof value, sizeof value);
                if (value < -128) value = -128;
                if (value > 127) value = 127;
                b->p[id] = (int8_t)value;
            }
            memcpy(b->e, a + offsetof(proj_trk_v6_t, e), sizeof b->e);
            memcpy(b->x, a + offsetof(proj_trk_v6_t, x), sizeof b->x);
            memcpy(&b->engine, a + offsetof(proj_trk_v6_t, engine), sizeof b->engine);
            memcpy(&b->preset, a + offsetof(proj_trk_v6_t, preset), sizeof b->preset);
            memcpy(b->step, a + offsetof(proj_trk_v6_t, step), sizeof b->step);

            /* Preserve both the signed six-bit microtiming and two-bit fill condition. */
            for (s = 0; s < NSTEP; ++s) {
                uint8_t mf = a[offsetof(proj_trk_v6_t, mf) + s];
                int32_t micro = mf & 63u;
                if (micro >= 32)
                    micro -= 64;
                proj_v7_micro_set(b, s, micro);
                b->fill[s / 4u] |= (uint8_t)(((mf >> 6) & 3u) << (2u * (s % 4u)));
            }

            for (k = 0; k < PROJ_V6_LOCKS; ++k) {
                memcpy(&lock, a + offsetof(proj_trk_v6_t, lock) + k * sizeof(plock_t), sizeof lock);
                if (lock.step != LOCK_FREE && lock.step < NSTEP &&
                    lock.param < P_COUNT && proj_v7_lockable(lock.param))
                    ++valid_locks;
                keep_lock(b, &lock, &kept);
            }
            if (lock_loss_mask && valid_locks > PROJ_V7_LOCKS)
                *lock_loss_mask |= (uint8_t)(1u << t);
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
