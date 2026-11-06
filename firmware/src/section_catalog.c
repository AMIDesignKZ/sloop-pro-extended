/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: eight-section catalogue validation and streaming overlays.
 */
#include <string.h>
#include "section_catalog.h"

static uint32_t project_offset(uint32_t record)
{
    return S37_CAT_PROJECT_OFFSET + record * (uint32_t)sizeof(project_v7_t);
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
    if (h.working_sum) {
        if (sm_read(io, map, state, project_offset(S37_CAT_WORKING), scratch, sizeof *scratch) ||
            !proj_v7_valid(scratch) || scratch->sum != h.working_sum)
            return -3;
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

int s37_cat_load_working(const sm_io_t *io, const sm_map_t *map,
                         const sm_state_t *state, project_v7_t *out)
{
    s37_cat_header_t h;
    if (!io || !map || !state || !out || state->length != S37_CAT_PAYLOAD_SIZE ||
        sm_read(io, map, state, 0u, &h, sizeof h) || !s37_cat_header_valid(&h) || !h.working_sum)
        return -1;
    if (sm_read(io, map, state, project_offset(S37_CAT_WORKING), out, sizeof *out) ||
        !proj_v7_valid(out) || out->sum != h.working_sum)
        return -2;
    return 0;
}

int s37_cat_prepare_image(s37_cat_image_t *image,
                          const project_v7_t *const sections[S37_CAT_SECTIONS],
                          const uint16_t bars[S37_CAT_SECTIONS],
                          uint8_t present_mask, uint8_t default_section,
                          const project_v7_t *working)
{
    uint32_t i;
    if (!image || !sections || !bars || !working || !proj_v7_valid(working) ||
        (present_mask && (default_section >= S37_CAT_SECTIONS || !(present_mask & (1u << default_section)))) ||
        (!present_mask && default_section != 0xFFu))
        return -1;
    memset(&image->header, 0, sizeof image->header);
    image->header.magic = S37_CAT_MAGIC;
    image->header.version = S37_CAT_VERSION;
    image->header.header_size = S37_CAT_HEADER_SIZE;
    image->header.payload_size = S37_CAT_PAYLOAD_SIZE;
    image->header.present_mask = present_mask;
    image->header.default_section = default_section;
    image->header.working_sum = working->sum;
    image->working = working;
    for (i = 0; i < S37_CAT_SECTIONS; ++i) {
        image->section[i] = sections[i];
        if (present_mask & (uint8_t)(1u << i)) {
            if (!sections[i] || !bars[i] || !proj_v7_valid(sections[i]))
                return -1;
            image->header.section[i].project_sum = sections[i]->sum;
            image->header.section[i].bars = bars[i];
            image->header.section[i].flags = S37_CAT_PRESENT;
        } else if (sections[i] || bars[i]) {
            return -1;
        }
    }
    return s37_cat_header_valid(&image->header) ? 0 : -1;
}

int s37_cat_image_read(void *context, uint32_t offset,
                       uint8_t *dst, uint32_t length)
{
    s37_cat_image_t *image = (s37_cat_image_t *)context;
    uint32_t i, end;
    if (!image || !dst || !image->working || !proj_v7_valid(image->working) ||
        !s37_cat_header_valid(&image->header) || image->header.working_sum != image->working->sum ||
        offset > S37_CAT_PAYLOAD_SIZE || length > S37_CAT_PAYLOAD_SIZE - offset)
        return -1;
    memset(dst, 0xFF, length);
    end = offset + length;
    if (offset < S37_CAT_HEADER_SIZE && end > 0u) {
        uint32_t from = offset, to = end < S37_CAT_HEADER_SIZE ? end : S37_CAT_HEADER_SIZE;
        memcpy(dst, (const uint8_t *)&image->header + from, to - from);
    }
    for (i = 0; i < S37_CAT_RECORDS; ++i) {
        const project_v7_t *p = i == S37_CAT_WORKING ? image->working : image->section[i];
        uint32_t a = project_offset(i), b = a + sizeof(project_v7_t), from, to;
        if (i < S37_CAT_SECTIONS && !(image->header.present_mask & (uint8_t)(1u << i)))
            continue;
        if (!p || !proj_v7_valid(p))
            return -1;
        if (offset < b && end > a) {
            from = offset > a ? offset : a;
            to = end < b ? end : b;
            memcpy(dst + (from - offset), (const uint8_t *)p + (from - a), to - from);
        }
    }
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

int s37_cat_prepare_erase_overlay(s37_cat_overlay_t *o,
                                  const sm_io_t *io, const sm_map_t *map,
                                  const sm_state_t *state, project_v7_t *scratch,
                                  uint8_t section)
{
    s37_cat_header_t h;
    sm_state_t latest;
    uint32_t i;
    if (!o || !scratch || !state || state->uncertain || section >= S37_CAT_SECTIONS ||
        sm_scan(io, map, &latest) || latest.uncertain || latest.copy != state->copy ||
        latest.sequence != state->sequence || latest.length != state->length ||
        latest.payload_crc != state->payload_crc || s37_cat_validate(io, map, state, scratch, &h) ||
        !(h.present_mask & (uint8_t)(1u << section)))
        return -1;
    h.present_mask &= (uint8_t)~(1u << section);
    memset(&h.section[section], 0, sizeof h.section[section]);
    if (h.default_section == section) {
        h.default_section = 0xFFu;
        for (i = 0; i < S37_CAT_SECTIONS; i++)
            if (h.present_mask & (uint8_t)(1u << i)) {
                h.default_section = (uint8_t)i;
                break;
            }
    }
    o->io = io;
    o->map = map;
    o->base = *state;
    o->replacement = 0;
    o->header = h;
    o->section = section;
    return 0;
}

int s37_cat_prepare_working_overlay(s37_cat_overlay_t *o,
                                    const sm_io_t *io, const sm_map_t *map,
                                    const sm_state_t *state, project_v7_t *scratch,
                                    project_v7_t *replacement)
{
    s37_cat_header_t h;
    sm_state_t latest;
    if (!o || !replacement || !scratch || scratch == replacement || !state || state->uncertain ||
        sm_scan(io, map, &latest) || latest.uncertain || latest.copy != state->copy ||
        latest.sequence != state->sequence || latest.length != state->length ||
        latest.payload_crc != state->payload_crc || s37_cat_validate(io, map, state, scratch, &h) ||
        !proj_v7_valid(replacement))
        return -1;
    h.working_sum = replacement->sum;
    o->io = io;
    o->map = map;
    o->base = *state;
    o->replacement = replacement;
    o->header = h;
    o->section = S37_CAT_WORKING;
    return 0;
}

int s37_cat_overlay_read(void *context, uint32_t offset,
                         uint8_t *dst, uint32_t length)
{
    s37_cat_overlay_t *o = (s37_cat_overlay_t *)context;
    uint32_t record_start, end;
    if (!o || !o->io || !o->map || o->section > S37_CAT_WORKING || !dst ||
        !s37_cat_header_valid(&o->header) ||
        (o->replacement && !proj_v7_valid(o->replacement)) ||
        (o->section < S37_CAT_SECTIONS &&
         (o->replacement ? (!(o->header.section[o->section].flags & S37_CAT_PRESENT) ||
                            o->header.section[o->section].project_sum != o->replacement->sum) :
                           ((o->header.section[o->section].flags & S37_CAT_PRESENT) ||
                            o->header.section[o->section].project_sum || o->header.section[o->section].bars))) ||
        (o->section == S37_CAT_WORKING &&
         (!o->replacement || o->header.working_sum != o->replacement->sum)) ||
        offset > S37_CAT_PAYLOAD_SIZE || length > S37_CAT_PAYLOAD_SIZE - offset)
        return -1;
    if (sm_read(o->io, o->map, &o->base, offset, dst, length))
        return -1;
    end = offset + length;
    if (offset < S37_CAT_HEADER_SIZE && end > 0u) {
        uint32_t from = offset, to = end < S37_CAT_HEADER_SIZE ? end : S37_CAT_HEADER_SIZE;
        memcpy(dst + (from - offset), (const uint8_t *)&o->header + from, to - from);
    }
    if (o->replacement) {
        record_start = project_offset(o->section);
        if (offset < record_start + sizeof(project_v7_t) && end > record_start) {
            uint32_t from = offset > record_start ? offset : record_start;
            uint32_t to = end < record_start + sizeof(project_v7_t) ? end : record_start + sizeof(project_v7_t);
            memcpy(dst + (from - offset), (const uint8_t *)o->replacement + (from - record_start), to - from);
        }
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
