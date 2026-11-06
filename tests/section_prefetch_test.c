/* SPDX-License-Identifier: GPL-3.0-only */
/* Host tests for the System 37 section-prefetch hand-off. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "section_prefetch.h"

static void test_basic_publish_consume(void)
{
    s37_prefetch_t pf;
    uint8_t section = 0xFFu;
    uint32_t ticket = 0;
    s37_pf_init(&pf);
    assert(pf.state == S37_PF_IDLE);
    assert(!s37_pf_begin(&pf, &section, &ticket));
    ticket = s37_pf_request(&pf, 2u);
    assert(ticket != 0u && pf.state == S37_PF_REQUESTED);
    assert(s37_pf_request(&pf, 2u) == ticket); /* idempotent */
    assert(s37_pf_begin(&pf, &section, &ticket));
    assert(section == 2u && pf.state == S37_PF_LOADING);
    assert(!s37_pf_take(&pf, 2u));             /* never visible while loading */
    assert(s37_pf_complete(&pf, ticket, section, 1));
    assert(pf.state == S37_PF_READY);
    assert(!s37_pf_take(&pf, 1u));             /* wrong boundary leaves it ready */
    assert(s37_pf_take(&pf, 2u));
    assert(pf.state == S37_PF_IDLE);
    assert(!s37_pf_take(&pf, 2u));             /* exactly once */
}

static void test_failed_load_and_retry(void)
{
    s37_prefetch_t pf;
    uint8_t section;
    uint32_t ticket, retry;
    s37_pf_init(&pf);
    ticket = s37_pf_request(&pf, 5u);
    assert(s37_pf_begin(&pf, &section, &ticket));
    assert(s37_pf_complete(&pf, ticket, section, 0)); /* failure was recorded */
    assert(pf.state == S37_PF_FAILED && pf.error);
    assert(!s37_pf_take(&pf, 5u));
    retry = s37_pf_request(&pf, 5u);
    assert(retry != ticket);
    assert(s37_pf_begin(&pf, &section, &ticket));
    assert(section == 5u && ticket == retry);
    assert(s37_pf_complete(&pf, ticket, section, 1));
    assert(s37_pf_take(&pf, 5u));
}

static void test_superseded_load(void)
{
    s37_prefetch_t pf;
    uint8_t section;
    uint32_t old_ticket, new_ticket;
    s37_pf_init(&pf);
    old_ticket = s37_pf_request(&pf, 1u);
    assert(s37_pf_begin(&pf, &section, &old_ticket));
    assert(section == 1u);
    new_ticket = s37_pf_request(&pf, 7u);      /* while old I/O is in progress */
    assert(new_ticket != old_ticket);
    assert(!s37_pf_complete(&pf, old_ticket, 1u, 1));
    assert(pf.state == S37_PF_REQUESTED);
    assert(s37_pf_begin(&pf, &section, &old_ticket));
    assert(section == 7u && old_ticket == new_ticket);
    assert(s37_pf_complete(&pf, old_ticket, section, 1));
    assert(!s37_pf_take(&pf, 1u));
    assert(s37_pf_take(&pf, 7u));
}

static void test_cancel_and_stale_ready(void)
{
    s37_prefetch_t pf;
    uint8_t section;
    uint32_t ticket;
    s37_pf_init(&pf);
    ticket = s37_pf_request(&pf, 3u);
    assert(s37_pf_begin(&pf, &section, &ticket));
    s37_pf_cancel(&pf);
    assert(!s37_pf_complete(&pf, ticket, section, 1));
    assert(pf.state == S37_PF_IDLE);
    ticket = s37_pf_request(&pf, 4u);
    assert(s37_pf_begin(&pf, &section, &ticket));
    assert(s37_pf_complete(&pf, ticket, section, 1));
    assert(s37_pf_request(&pf, 6u) != ticket); /* replace a ready record */
    assert(!s37_pf_take(&pf, 4u));
    assert(s37_pf_begin(&pf, &section, &ticket));
    assert(section == 6u);
}

static void test_invalid_requests_and_generation_wrap(void)
{
    s37_prefetch_t pf;
    s37_pf_init(&pf);
    assert(s37_pf_request(&pf, 8u) == 0u);
    assert(pf.state == S37_PF_IDLE);
    pf.generation = UINT32_MAX;
    assert(s37_pf_request(&pf, 0u) == 1u);
}

int main(void)
{
    test_basic_publish_consume();
    test_failed_load_and_retry();
    test_superseded_load();
    test_cancel_and_stale_ready();
    test_invalid_requests_and_generation_wrap();
    puts("section_prefetch_test: ok");
    return 0;
}
