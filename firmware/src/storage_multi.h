/* SPDX-License-Identifier: GPL-3.0-only
 * Multi-sector, A/B NOR storage primitives. Kept separate from storage.c's
 * existing one-sector format until migration and board integration are ready.
 */
#pragma once
#include <stdint.h>

#define SM_SECTOR_SIZE 4096u
#define SM_PAGE_SIZE 256u
#define SM_PAYLOAD_OFF 256u
#define SM_FIRST_CAPACITY (SM_SECTOR_SIZE - SM_PAYLOAD_OFF)
#define SM_MAX_SECTORS 12u
#define SM_VERSION 1u
#define SM_MAGIC 0x4F54534Du /* "MSTO" in little-endian flash */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t object_id;
    uint32_t copy;
    uint32_t sequence;
    uint32_t length;
    uint32_t sector_count;
    uint32_t payload_crc;
    uint32_t header_crc;
} sm_header_t;

_Static_assert(sizeof(sm_header_t) == 32u, "multi-sector header layout");

typedef struct {
    uint16_t object_id;
    uint16_t reserved;
    uint32_t max_length;
    uint32_t sectors[2][SM_MAX_SECTORS];
    uint8_t sector_count[2];
} sm_map_t;

typedef struct {
    void *context;
    int (*read)(void *context, uint32_t offset, void *dst, uint32_t length);
    int (*erase)(void *context, uint32_t sector_offset);
    int (*program)(void *context, uint32_t offset, const void *src, uint32_t length);
} sm_io_t;

typedef int (*sm_source_read_fn)(void *context, uint32_t offset, uint8_t *dst, uint32_t length);

typedef struct {
    int8_t copy;             /* 0 or 1; -1 means neither copy is valid */
    uint32_t sequence;
    uint32_t length;
    uint32_t payload_crc;
    uint8_t uncertain;       /* one peer copy could not be inspected; reads may proceed, saves must wait */
} sm_state_t;

uint32_t sm_crc32(const void *data, uint32_t length);
int sm_map_valid(const sm_map_t *map);
uint32_t sm_capacity(const sm_map_t *map, uint32_t copy);
int sm_scan(const sm_io_t *io, const sm_map_t *map, sm_state_t *state);
int sm_read(const sm_io_t *io, const sm_map_t *map, const sm_state_t *state,
            uint32_t offset, void *dst, uint32_t length);
int sm_save_stream(const sm_io_t *io, const sm_map_t *map,
                   sm_source_read_fn source_read, void *source_context,
                   uint32_t length, sm_state_t *new_state);
