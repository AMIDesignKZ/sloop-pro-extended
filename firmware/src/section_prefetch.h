/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37: single-buffer section prefetch hand-off.
 *
 * The main loop owns the staging buffer and performs all reads/validation.
 * The audio side only consumes a fully published section at a block boundary.
 * This module does not know the buffer's type or perform I/O.
 */
#ifndef SYSTEM37_SECTION_PREFETCH_H
#define SYSTEM37_SECTION_PREFETCH_H

#include <stdint.h>

#define S37_PF_IDLE      0u
#define S37_PF_REQUESTED 1u
#define S37_PF_LOADING   2u
#define S37_PF_READY     3u
#define S37_PF_FAILED    4u

typedef struct {
    volatile uint32_t generation;
    volatile uint32_t ready_generation;
    volatile uint8_t requested_section;
    volatile uint8_t ready_section;
    volatile uint8_t state;
    volatile uint8_t error;
} s37_prefetch_t;

void s37_pf_init(s37_prefetch_t *pf);

/* Main/UI side: request a section. Repeating the same in-flight request is
 * idempotent. A different request supersedes an older load/ready record.
 * Returns the non-zero generation ticket for this request. */
uint32_t s37_pf_request(s37_prefetch_t *pf, uint8_t section);

/* Main-loop side: claim the pending request before filling the staging
 * buffer. Returns 1 and writes section/ticket, or 0 if nothing is pending. */
int s37_pf_begin(s37_prefetch_t *pf, uint8_t *section, uint32_t *ticket);

/* Main-loop side: call only after the staging buffer is fully read AND
 * validated. A stale completion is rejected and returns 0. */
int s37_pf_complete(s37_prefetch_t *pf, uint32_t ticket,
                    uint8_t section, int success);

/* Audio-boundary side: consume only a complete record for expected_section.
 * Returns 1 exactly once. The caller may apply the staging buffer immediately
 * after this returns; the main loop may reuse it after the callback exits. */
int s37_pf_take(s37_prefetch_t *pf, uint8_t expected_section);

/* Cancel any pending, loading, ready or failed request. Invalidates tickets. */
void s37_pf_cancel(s37_prefetch_t *pf);

#endif
