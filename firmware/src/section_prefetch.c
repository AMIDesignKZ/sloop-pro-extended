/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37: single-buffer section prefetch hand-off. */
#include "section_prefetch.h"

#define S37_PF_BARRIER() __asm__ volatile("" ::: "memory")

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation ? generation : 1u;
}

void s37_pf_init(s37_prefetch_t *pf)
{
    pf->generation = 0;
    pf->ready_generation = 0;
    pf->requested_section = 0xFFu;
    pf->ready_section = 0xFFu;
    pf->error = 0;
    S37_PF_BARRIER();
    pf->state = S37_PF_IDLE;
}

uint32_t s37_pf_request(s37_prefetch_t *pf, uint8_t section)
{
    uint32_t generation;
    if (section >= 8u)
        return 0;
    if (pf->requested_section == section &&
        (pf->state == S37_PF_REQUESTED || pf->state == S37_PF_LOADING ||
         (pf->state == S37_PF_READY && pf->ready_section == section)))
        return pf->generation;

    /* Invalidate a previously READY record first: an audio interrupt between
     * this store and the new request fields must not consume the old section. */
    S37_PF_BARRIER();
    pf->state = S37_PF_REQUESTED;
    S37_PF_BARRIER();
    generation = next_generation(pf->generation);
    pf->generation = generation;
    pf->requested_section = section;
    pf->ready_generation = 0;
    pf->ready_section = 0xFFu;
    pf->error = 0;
    S37_PF_BARRIER();
    return generation;
}

int s37_pf_begin(s37_prefetch_t *pf, uint8_t *section, uint32_t *ticket)
{
    uint32_t generation;
    if (pf->state != S37_PF_REQUESTED)
        return 0;
    generation = pf->generation;
    *section = pf->requested_section;
    *ticket = generation;
    S37_PF_BARRIER();
    pf->state = S37_PF_LOADING;
    return 1;
}

int s37_pf_complete(s37_prefetch_t *pf, uint32_t ticket,
                    uint8_t section, int success)
{
    if (ticket == 0u || ticket != pf->generation ||
        pf->state != S37_PF_LOADING || section != pf->requested_section)
        return 0;

    if (!success) {
        pf->error = 1;
        pf->ready_generation = 0;
        pf->ready_section = 0xFFu;
        S37_PF_BARRIER();
        pf->state = S37_PF_FAILED;
        return 1;
    }

    pf->ready_section = section;
    pf->ready_generation = ticket;
    pf->error = 0;
    S37_PF_BARRIER();
    pf->state = S37_PF_READY;
    return 1;
}

int s37_pf_take(s37_prefetch_t *pf, uint8_t expected_section)
{
    uint32_t generation;
    if (pf->state != S37_PF_READY ||
        pf->ready_section != expected_section)
        return 0;
    generation = pf->generation;
    if (!generation || pf->ready_generation != generation)
        return 0;

    /* Publish consumption only after the caller has observed the ready data.
     * The caller applies the staging buffer synchronously in this callback. */
    S37_PF_BARRIER();
    pf->ready_generation = 0;
    pf->ready_section = 0xFFu;
    pf->requested_section = 0xFFu;
    pf->state = S37_PF_IDLE;
    return 1;
}

void s37_pf_cancel(s37_prefetch_t *pf)
{
    S37_PF_BARRIER();
    pf->state = S37_PF_IDLE;
    S37_PF_BARRIER();
    pf->generation = next_generation(pf->generation);
    pf->ready_generation = 0;
    pf->requested_section = 0xFFu;
    pf->ready_section = 0xFFu;
    pf->error = 0;
    S37_PF_BARRIER();
}
