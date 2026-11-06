/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37: main-loop catalogue loader for the single staging project. */
#include "section_prefetch_io.h"

int s37_pf_service(s37_prefetch_t *pf, const sm_io_t *io,
                   const sm_map_t *map, project_v7_t *staging)
{
    sm_state_t state;
    uint8_t section;
    uint32_t ticket;
    int loaded;

    if (!pf || !io || !map || !staging ||
        !s37_pf_begin(pf, &section, &ticket))
        return 0;

    loaded = !sm_scan(io, map, &state) &&
             !s37_cat_load_section(io, map, &state, section, staging, 0);

    if (!s37_pf_complete(pf, ticket, section, loaded))
        return 0; /* superseded while the synchronous read was in progress */

    return loaded ? 1 : -1;
}
