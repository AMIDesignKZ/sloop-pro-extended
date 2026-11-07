/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37: six-track FUN7 live-project adapter. */
#include "project_v7_live.h"
#include <stddef.h>

/* The JieLi target libc headers omit string.h; host builds use the system
 * declaration, while the target resolves this to its freestanding libc. */
#if defined(__has_include)
# if __has_include(<string.h>)
#  include <string.h>
# else
extern void *memset(void *dst, int value, unsigned length);
# endif
#else
extern void *memset(void *dst, int value, unsigned length);
#endif

static int ops_capture_valid(const s37_project_v7_live_ops_t *ops)
{
    return ops && ops->capture_globals && ops->capture_track;
}

static int ops_apply_valid(const s37_project_v7_live_ops_t *ops)
{
    return ops && ops->validate_globals && ops->validate_track &&
           ops->apply_globals && ops->apply_track;
}

int s37_project_v7_capture(project_v7_t *out,
                           const s37_project_v7_live_ops_t *ops)
{
    uint32_t track;

    if (!out || !ops_capture_valid(ops))
        return -1;

    memset(out, 0, sizeof *out);
    out->magic = PROJ_V7_MAGIC;
    out->size = sizeof *out;

    if (ops->capture_globals(ops->context, out))
        return -1;

    for (track = 0; track < PROJ_V7_TRACKS; ++track)
        if (ops->capture_track(ops->context, (uint8_t)track, &out->t[track]))
            return -1;

    out->sum = proj_v7_hash(out, (uint32_t)sizeof *out - 4u);
    return proj_v7_valid(out) ? 0 : -1;
}

int s37_project_v7_apply(const project_v7_t *in,
                         const s37_project_v7_live_ops_t *ops, int all)
{
    uint32_t track;

    if (!in || !ops_apply_valid(ops) || !proj_v7_valid(in))
        return -1;

    /* Complete preflight before touching any live state. */
    if (ops->validate_globals(ops->context, in, all))
        return -2;
    for (track = 0; track < PROJ_V7_TRACKS; ++track)
        if (ops->validate_track(ops->context, (uint8_t)track, &in->t[track]))
            return -2;

    ops->apply_globals(ops->context, in, all);
    for (track = 0; track < PROJ_V7_TRACKS; ++track)
        ops->apply_track(ops->context, (uint8_t)track, &in->t[track]);

    return 0;
}
