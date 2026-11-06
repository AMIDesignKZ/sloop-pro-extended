/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: isolated project-format definitions and migration API.
 *
 * Format 6 is mirrored here as a read-only wire structure. Do not change its
 * field order: existing SLOOP projects are persisted as raw structures.
 */
#ifndef SYSTEM37_PROJECT_V7_H
#define SYSTEM37_PROJECT_V7_H
#include <stdint.h>
#include "core.h"

#define PROJ_V6_MAGIC 0x46554E36u /* FUN6 */
#define PROJ_V7_MAGIC 0x46554E37u /* FUN7 */
#define PROJ_V7_LOCKS 12u
#define PROJ_V7_TRACKS 6u
#define PROJ_V7_FILL_BYTES (NSTEP / 4u)

#define PROJ_V6_NC ((uint32_t)P_ITYPE)
#define PROJ_V6_NX ((uint32_t)(P_E0 - P_ITYPE))

typedef struct {
    int16_t p[PROJ_V6_NC];
    int16_t e[8];
    uint8_t x[PROJ_V6_NX];
    uint8_t engine, preset;
    union { step_t step[NSTEP]; dstep_t dstep[NSTEP]; };
    uint8_t mf[NSTEP]; /* low 6 bits: microtiming; high 2 bits: fill condition */
    plock_t lock[NLOCK];
} proj_trk_v6_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[32];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v6_t t[4];
    uint32_t sum;
} project_v6_t;

typedef struct {
    int16_t p[PROJ_V6_NC];
    int16_t e[8];
    uint8_t x[PROJ_V6_NX];
    uint8_t engine, preset;
    union { step_t step[NSTEP]; dstep_t dstep[NSTEP]; };
    uint8_t fill[PROJ_V7_FILL_BYTES]; /* four 2-bit FC_* values per byte */
    plock_t lock[PROJ_V7_LOCKS];
} proj_trk_v7_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[32];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v7_t t[PROJ_V7_TRACKS];
    uint32_t sum;
} project_v7_t;

_Static_assert(sizeof(plock_t) == 4u, "parameter lock wire size");
_Static_assert(sizeof(proj_trk_v6_t) == 934u, "format 6 track wire size");
_Static_assert(sizeof(project_v6_t) == 3816u, "format 6 project wire size");
_Static_assert(sizeof(proj_trk_v7_t) == 838u, "format 7 track wire size");
_Static_assert(sizeof(project_v7_t) == 5108u, "format 7 project wire size");

uint32_t proj_v7_hash(const void *data, uint32_t length);
int proj_v7_valid(const project_v7_t *project);
int proj_v6_to_v7(project_v7_t *out, const void *data, uint32_t length, uint8_t *lock_loss_mask);

#endif
