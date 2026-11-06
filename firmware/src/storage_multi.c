/* SPDX-License-Identifier: GPL-3.0-only
 * Multi-sector, A/B NOR storage primitives.
 * All calls are main-loop operations. The caller supplies flash callbacks;
 * firmware callbacks must retain the board's independent flash guard.
 */
#include <stddef.h>
#include <string.h>
#include "storage_multi.h"

static uint32_t sm_crc_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    while (n--) {
        crc ^= *p++;
        for (i = 0; i < 8u; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

uint32_t sm_crc32(const void *data, uint32_t length)
{
    return ~sm_crc_update(0xFFFFFFFFu, (const uint8_t *)data, length);
}

uint32_t sm_capacity(const sm_map_t *map, uint32_t copy)
{
    uint32_t n;
    if (!map || copy > 1u || map->sector_count[copy] == 0u)
        return 0u;
    n = map->sector_count[copy];
    return SM_FIRST_CAPACITY + (n - 1u) * SM_SECTOR_SIZE;
}

int sm_map_valid(const sm_map_t *map)
{
    uint32_t c, i, c2, j;
    if (!map || map->object_id == 0xFFFFu || map->max_length == 0u)
        return 0;
    for (c = 0; c < 2u; c++) {
        if (!map->sector_count[c] || map->sector_count[c] > SM_MAX_SECTORS ||
            map->max_length > sm_capacity(map, c))
            return 0;
        for (i = 0; i < map->sector_count[c]; i++) {
            uint32_t a = map->sectors[c][i];
            if ((a & (SM_SECTOR_SIZE - 1u)) != 0u || a > UINT32_MAX - (SM_SECTOR_SIZE - 1u))
                return 0;
            for (c2 = 0; c2 <= c; c2++) {
                uint32_t limit = c2 == c ? i : map->sector_count[c2];
                for (j = 0; j < limit; j++)
                    if (map->sectors[c2][j] == a)
                        return 0;
            }
        }
    }
    return 1;
}

/* Translate a logical payload range into a physical flash address. A request
 * is kept within one physical sector; callers split at this boundary. */
static int sm_phys(const sm_map_t *map, uint32_t copy, uint32_t logical,
                   uint32_t *physical, uint32_t *available)
{
    uint32_t sector_index, within, base;
    if (copy > 1u || logical >= sm_capacity(map, copy))
        return -1;
    if (logical < SM_FIRST_CAPACITY) {
        sector_index = 0u;
        within = SM_PAYLOAD_OFF + logical;
        *available = SM_FIRST_CAPACITY - logical;
    } else {
        logical -= SM_FIRST_CAPACITY;
        sector_index = 1u + logical / SM_SECTOR_SIZE;
        within = logical % SM_SECTOR_SIZE;
        *available = SM_SECTOR_SIZE - within;
    }
    if (sector_index >= map->sector_count[copy])
        return -1;
    base = map->sectors[copy][sector_index];
    *physical = base + within;
    return 0;
}

static int sm_payload_read(const sm_io_t *io, const sm_map_t *map, uint32_t copy,
                           uint32_t logical, void *dst, uint32_t length)
{
    uint8_t *out = (uint8_t *)dst;
    while (length) {
        uint32_t physical, avail, n;
        if (sm_phys(map, copy, logical, &physical, &avail))
            return -1;
        n = length < avail ? length : avail;
        if (io->read(io->context, physical, out, n))
            return -2;
        logical += n;
        out += n;
        length -= n;
    }
    return 0;
}

static int sm_payload_crc(const sm_io_t *io, const sm_map_t *map, uint32_t copy,
                          uint32_t length, uint32_t *result)
{
    uint8_t page[SM_PAGE_SIZE];
    uint32_t off = 0, crc = 0xFFFFFFFFu;
    while (off < length) {
        uint32_t n = length - off;
        if (n > sizeof page) n = sizeof page;
        if (sm_payload_read(io, map, copy, off, page, n))
            return -1;
        crc = sm_crc_update(crc, page, n);
        off += n;
    }
    *result = ~crc;
    return 0;
}

static int sm_copy_check(const sm_io_t *io, const sm_map_t *map, uint32_t copy,
                         sm_header_t *header)
{
    uint32_t crc;
    if (io->read(io->context, map->sectors[copy][0], header, sizeof *header))
        return -2;
    if (header->magic != SM_MAGIC || header->version != SM_VERSION ||
        header->object_id != map->object_id || header->copy != copy ||
        header->sector_count != map->sector_count[copy] ||
        header->length > map->max_length ||
        header->header_crc != sm_crc32(header, (uint32_t)offsetof(sm_header_t, header_crc)))
        return -1;
    if (sm_payload_crc(io, map, copy, header->length, &crc))
        return -2;
    if (crc != header->payload_crc)
        return -1;
    return 0;
}

static int sm_seq_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0; /* wrap-safe while fewer than 2^31 saves apart */
}

int sm_scan(const sm_io_t *io, const sm_map_t *map, sm_state_t *state)
{
    sm_header_t h[2];
    int check[2], chosen;
    if (!io || !io->read || !state || !sm_map_valid(map))
        return -1;
    check[0] = sm_copy_check(io, map, 0u, &h[0]);
    check[1] = sm_copy_check(io, map, 1u, &h[1]);
    chosen = check[1] == 0 && (check[0] != 0 || sm_seq_newer(h[1].sequence, h[0].sequence)) ? 1 : 0;
    if (check[chosen] != 0) {
        state->copy = -1;
        state->sequence = state->length = state->payload_crc = 0u;
        state->uncertain = 0u;
        return check[0] == -2 || check[1] == -2 ? -2 : -1;
    }
    state->copy = (int8_t)chosen;
    state->sequence = h[chosen].sequence;
    state->length = h[chosen].length;
    state->payload_crc = h[chosen].payload_crc;
    state->uncertain = (uint8_t)(check[0] == -2 || check[1] == -2);
    return 0;
}

int sm_read(const sm_io_t *io, const sm_map_t *map, const sm_state_t *state,
            uint32_t offset, void *dst, uint32_t length)
{
    if (!io || !io->read || !state || !sm_map_valid(map) || state->copy < 0 || state->copy > 1 ||
        offset > state->length || length > state->length - offset || (!dst && length))
        return -1;
    return sm_payload_read(io, map, (uint32_t)state->copy, offset, dst, length);
}

int sm_save_stream(const sm_io_t *io, const sm_map_t *map,
                   sm_source_read_fn source_read, void *source_context,
                   uint32_t length, sm_state_t *new_state)
{
    sm_state_t current;
    sm_header_t header, verify;
    uint8_t page[SM_PAGE_SIZE];
    uint32_t dst, seq, i, off, crc, verify_crc;
    int have_current;
    if (!io || !io->read || !io->erase || !io->program || !source_read ||
        !new_state || !sm_map_valid(map) || length > map->max_length)
        return -1;

    {
        int scan_rc = sm_scan(io, map, &current);
        if (scan_rc == -2 || (scan_rc == 0 && current.uncertain))
            return -9; /* a failed peer read is not an empty store: never erase on uncertainty */
        have_current = scan_rc == 0;
    }
    dst = have_current ? (uint32_t)(current.copy ^ 1) : 0u;
    seq = have_current ? current.sequence + 1u : 1u;

    /* The previous committed copy is never erased during this transaction. */
    for (i = 0; i < map->sector_count[dst]; i++)
        if (io->erase(io->context, map->sectors[dst][i]))
            return -2;

    off = 0u;
    crc = 0xFFFFFFFFu;
    while (off < length) {
        uint32_t physical, sector_avail, page_avail, n;
        if (sm_phys(map, dst, off, &physical, &sector_avail))
            return -3;
        page_avail = SM_PAGE_SIZE - (physical & (SM_PAGE_SIZE - 1u));
        n = length - off;
        if (n > sizeof page) n = sizeof page;
        if (n > sector_avail) n = sector_avail;
        if (n > page_avail) n = page_avail;
        if (!n || source_read(source_context, off, page, n))
            return -4;
        crc = sm_crc_update(crc, page, n);
        if (io->program(io->context, physical, page, n))
            return -5;
        off += n;
    }
    crc = ~crc;

    if (sm_payload_crc(io, map, dst, length, &verify_crc) || verify_crc != crc)
        return -6;

    memset(&header, 0xFF, sizeof header);
    header.magic = SM_MAGIC;
    header.version = SM_VERSION;
    header.object_id = map->object_id;
    header.copy = dst;
    header.sequence = seq;
    header.length = length;
    header.sector_count = map->sector_count[dst];
    header.payload_crc = crc;
    header.header_crc = sm_crc32(&header, (uint32_t)offsetof(sm_header_t, header_crc));

    /* Header is the commit record and is programmed only after payload verify. */
    if (io->program(io->context, map->sectors[dst][0], &header, sizeof header))
        return -7;
    if (sm_copy_check(io, map, dst, &verify) || memcmp(&verify, &header, sizeof header))
        return -8;

    new_state->copy = (int8_t)dst;
    new_state->sequence = seq;
    new_state->length = length;
    new_state->payload_crc = crc;
    new_state->uncertain = 0u;
    return 0;
}
