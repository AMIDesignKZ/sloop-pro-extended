/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37: main-loop service joining catalogue reads to the prefetch hand-off.
 *
 * This function performs flash I/O only in its caller's main-loop context.
 * It owns no project buffer: the caller supplies the single staging record.
 */
#ifndef SYSTEM37_SECTION_PREFETCH_IO_H
#define SYSTEM37_SECTION_PREFETCH_IO_H

#include "section_catalog.h"
#include "section_prefetch.h"

/* Process at most one requested section.
 * Returns 1 when a validated record is published READY, -1 when the request
 * completes in FAILED state, and 0 when there was no request or a newer
 * request superseded this load. The staging buffer is usable only after
 * s37_pf_take() succeeds for the matching section.
 */
int s37_pf_service(s37_prefetch_t *pf, const sm_io_t *io,
                   const sm_map_t *map, project_v7_t *staging);

#endif
