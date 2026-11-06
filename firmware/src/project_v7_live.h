/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37: host-testable six-track live-project capture/apply boundary.
 *
 * The adapter owns the FUN7 wire image, not the synthesiser runtime. Board
 * code supplies callbacks that translate between proj_trk_v7_t and track_t.
 * Capture/apply callbacks run in the caller's context; apply callers must
 * exclude the audio ISR for the whole transaction.
 */
#ifndef SYSTEM37_PROJECT_V7_LIVE_H
#define SYSTEM37_PROJECT_V7_LIVE_H

#include "project_v7.h"

typedef struct {
    void *context;

    /* Capture the project-wide persistent values (g, sel, drdly, rsv).
     * Do not write magic, size, track records or checksum. */
    int (*capture_globals)(void *context, project_v7_t *out);

    /* Capture one persistent track record. Called exactly once for each
     * track index 0..5, in ascending order. */
    int (*capture_track)(void *context, uint8_t track,
                         proj_trk_v7_t *out);

    /* Validate the complete image against the live runtime's supported
     * engines, parameter descriptors and project/global constraints.
     * This is a preflight only: it must not mutate live state. */
    int (*validate_globals)(void *context, const project_v7_t *in,
                            int all);
    int (*validate_track)(void *context, uint8_t track,
                          const proj_trk_v7_t *in);

    /* Apply callbacks are infallible after successful preflight. The caller
     * must keep the audio ISR excluded until all six tracks are applied. */
    void (*apply_globals)(void *context, const project_v7_t *in, int all);
    void (*apply_track)(void *context, uint8_t track,
                        const proj_trk_v7_t *in);
} s37_project_v7_live_ops_t;

/* Returns 0 on success, -1 for bad arguments/callback failure.
 * The result is checksum-sealed and validated before it is returned. */
int s37_project_v7_capture(project_v7_t *out,
                           const s37_project_v7_live_ops_t *ops);

/* Returns 0 on success, -1 for invalid image/arguments, -2 for failed
 * preflight. No apply callback is called unless every preflight succeeds.
 * 'all' follows the existing project.c semantics for project-wide globals;
 * all six track records are always applied (also for arrangement sections).
 */
int s37_project_v7_apply(const project_v7_t *in,
                         const s37_project_v7_live_ops_t *ops, int all);

#endif
