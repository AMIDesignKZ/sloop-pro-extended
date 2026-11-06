/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: eight-section catalogue validation and streaming overlays.
 */
#include <string.h>
#include "section_catalog.h"

static uint32_t project_offset(uint32_t section)
{
    return S37_CAT_PROJECT_OFFSET + section * (uint32_t)sizeof(project_v7_t);
}

int s37_cat_header_valid(const s37_cat_header_t *h)
{
    uint32_t i;
    uint8_t mask = 0u;
    if (!h || h->magic != S37_CAT_MAGIC || h->version != S37_CAT_VERSION ||
        h->header_size != S37_CAT_HEADER_SIZE || h->payload_size != S37_CAT_PAYLOAD_SIZE ||
        (h->default_section > 7u && h->default_section != 0xFFu) ||
        h->reserved[0] || h->reserved[1])
        return 0;
    for (i = 0; i < S37_CAT_SECTIONS; ++i) {
        const s37_cat_entry_t *e = &h->section[i];
        if ((e->flags & (uint8_t)~S37_CAT_PRESENT) || e->reserved)
            return 0;
        if (e->flags & S37_CAT_PRESENT) {
            if (!e->bars)
                return 0;
            mask |= (uint8_t)(1u << i);
        } else if (e->bars || e->project_sum) {
            return 0;
        }
    }
    if (mask != h->present_mask)
        return 0;
    if (mask && (h->default_section > 7u ||
                 !(mask & (uint8_t)(1u << h->default_section))))
        return 0;
    if (!mask && h->default_section != 0xFFu)
        return 0;
    return 1;
}

int s37_cat_validate(const sm_io_t *io, const sm_map_t *map,
                     const sm_state_t *state, project_v7_t *scratch,
                     s37_cat_header_t *header)
{
    s37_cat_header_t h;
    uint32_t i;
    if (!io || !map || !state || !scratch || !header ||
        state->length != S37_CAT_PAYLOAD_SIZE ||
        sm_read(io, map, state, 0u, &h, sizeof h) ||
        !s37_cat_header_valid(&h))
        return -1;
    for (i = 0; i < S37_CAT_SECTIONS; ++i) {
        if (!(h.section[i].flags & S37_CAT_PRESENT))
            continue;
        if (sm_read(io, map, state, project_offset(i), scratch, sizeof *scratch) ||
            !proj_v7_valid(scratch) || scratch->sum != h.section[i].project_sum)
            return -2;
    }
    *header = h;
    return 0;
}

int s37_cat_load_section(const sm_io_t *io, const sm_map_t *map,
                         const sm_state_t *state, uint8_t section,
                         project_v7_t *out, s37_cat_header_t *header)
{
    s37_cat_header_t h;
    if (!io || !map || !state || !out || section >= S37_CAT_SECTIONS ||
        state->length != S37_CAT_PAYLOAD_SIZE ||
        sm_read(io, map, state, 0u, &h, sizeof h) || !s37_cat_header_valid(&h) ||
        !(h.section[section].flags & S37_CAT_PRESENT))
        return -1;
    if (sm_read(io, map, state, project_offset(section), out, sizeof *out) ||
        !proj_v7_valid(out) || out->sum != h.section[section].project_sum)
        return -2;
    if (header)
        *header = h;
    return 0;
}

int s37_cat_prepare_create(s37_cat_overlay_t *o,
                           project_v7_t *replacement, uint8_t section,
                           uint16_t bars, uint8_t default_section)
{
    s37_cat_header_t h;
    if (!o || !replacement || section >= S37_CAT_SECTIONS || !bars ||
        default_section != section || !proj_v7_valid(replacement))
        return -1;
    memset(&h, 0, sizeof h);
    h.magic = S37_CAT_MAGIC;
    h.version = S37_CAT_VERSION;
    h.header_size = S37_CAT_HEADER_SIZE;
    h.payload_size = S37_CAT_PAYLOAD_SIZE;
    h.present_mask = (uint8_t)(1u << section);
    h.default_section = default_section;
    h.section[section].project_sum = replacement->sum;
    h.section[section].bars = bars;
    h.section[section].flags = S37_CAT_PRESENT;
    o->io = NULL;
    o->map = NULL;
    memset(&o->base, 0, sizeof o->base);
    o->replacement = replacement;
    o->header = h;
    o->section = section;
    return 0;
}

int s37_cat_prepare_overlay(s37_cat_overlay_t *o,
                            const sm_io_t *io, const sm_map_t *map,
                            const sm_state_t *state, project_v7_t *scratch,
                            project_v7_t *replacement, uint8_t section,
                            uint16_t bars, uint8_t default_section)
{
    s37_cat_header_t h;
    sm_state_t latest;
    if (!o || !replacement || !scratch || scratch == replacement || !state || state->uncertain ||
        section >= S37_CAT_SECTIONS || !bars || default_section >= S37_CAT_SECTIONS ||
        sm_scan(io, map, &latest) || latest.uncertain ||
        latest.copy != state->copy || latest.sequence != state->sequence ||
        latest.length != state->length || latest.payload_crc != state->payload_crc ||
        s37_cat_validate(io, map, state, scratch, &h) ||
        !proj_v7_valid(replacement))
        return -1;
    h.section[section].flags = S37_CAT_PRESENT;
    h.section[section].bars = bars;
    h.section[section].project_sum = replacement->sum;
    h.section[section].reserved = 0u;
    h.present_mask |= (uint8_t)(1u << section);
    h.default_section = default_section;
    if (!s37_cat_header_valid(&h))
        return -2;
    o->io = io;
    o->map = map;
    o->base = *state;
    o->replacement = replacement;
    o->header = h;
    o->section = section;
    return 0;
}

int s37_cat_overlay_read(void *context, uint32_t offset,
                         uint8_t *dst, uint32_t length)
{
    s37_cat_overlay_t *o = (s37_cat_overlay_t *)context;
    uint32_t end, record_start, record_end;
    if (!o || !o->io || !o->map || !o->replacement || o->section >= S37_CAT_SECTIONS || !dst ||
        !proj_v7_valid(o->replacement) || !s37_cat_header_valid(&o->header) ||
        !(o->header.section[o->section].flags & S37_CAT_PRESENT) ||
        o->header.section[o->section].project_sum != o->replacement->sum ||
        offset > S37_CAT_PAYLOAD_SIZE ||
        length > S37_CAT_PAYLOAD_SIZE - offset)
        return -1;
    if (sm_read(o->io, o->map, &o->base, offset, dst, length))
        return -2;

    end = offset + length;
    if (offset < S37_CAT_HEADER_SIZE && end > 0u) {
        uint32_t from = offset;
        uint32_t to = end < S37_CAT_HEADER_SIZE ? end : S37_CAT_HEADER_SIZE;
        memcpy(dst, (const uint8_t *)&o->header + from, to - from);
    }

    record_start = project_offset(o->section);
    record_end = record_start + sizeof(project_v7_t);
    if (offset < record_end && end > record_start) {
        uint32_t from = offset > record_start ? offset : record_start;
        uint32_t to = end < record_end ? end : record_end;
        memcpy(dst + (from - offset),
               (const uint8_t *)o->replacement + (from - record_start),
               to - from);
    }
    return 0;
}

int s37_cat_blank_read(void *context, uint32_t offset,
                       uint8_t *dst, uint32_t length)
{
    s37_cat_header_t h;
    (void)context;
    if (!dst || offset > S37_CAT_PAYLOAD_SIZE ||
        length > S37_CAT_PAYLOAD_SIZE - offset)
        return -1;
    memset(dst, 0xFF, length);
    memset(&h, 0, sizeof h);
    h.magic = S37_CAT_MAGIC;
    h.version = S37_CAT_VERSION;
    h.header_size = S37_CAT_HEADER_SIZE;
    h.payload_size = S37_CAT_PAYLOAD_SIZE;
    h.default_section = 0xFFu;
    if (offset < sizeof h && offset + length > 0u) {
        uint32_t from = offset;
        uint32_t to = offset + length < sizeof h ? offset + length : sizeof h;
        memcpy(dst, (const uint8_t *)&h + from, to - from);
    }
    return 0;
}

int s37_cat_create_read(void *context, uint32_t offset,
                        uint8_t *dst, uint32_t length)
{
    s37_cat_overlay_t *o = (s37_cat_overlay_t *)context;
    uint32_t end, record_start, record_end;
    if (!o || !o->replacement || !dst || o->section >= S37_CAT_SECTIONS ||
        !proj_v7_valid(o->replacement) || !o->header.section[o->section].bars ||
        !s37_cat_header_valid(&o->header) ||
        !(o->header.section[o->section].flags & S37_CAT_PRESENT) ||
        o->header.section[o->section].project_sum != o->replacement->sum ||
        offset > S37_CAT_PAYLOAD_SIZE ||
        length > S37_CAT_PAYLOAD_SIZE - offset)
        return -1;
    if (s37_cat_blank_read(NULL, offset, dst, length))
        return -1;
    end = offset + length;
    if (offset < S37_CAT_HEADER_SIZE && end > 0u) {
        uint32_t from = offset;
        uint32_t to = end < S37_CAT_HEADER_SIZE ? end : S37_CAT_HEADER_SIZE;
        memcpy(dst, (const uint8_t *)&o->header + from, to - from);
    }
    record_start = project_offset(o->section);
    record_end = record_start + sizeof(project_v7_t);
    if (offset < record_end && end > record_start) {
        uint32_t from = offset > record_start ? offset : record_start;
        uint32_t to = end < record_end ? end : record_end;
        memcpy(dst + (from - offset),
               (const uint8_t *)o->replacement + (from - record_start),
               to - from);
    }
    return 0;
}
