/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: eight-section catalogue stored as one multi-sector A/B object.
 */
#ifndef SYSTEM37_SECTION_CATALOG_H
#define SYSTEM37_SECTION_CATALOG_H

#include <stdint.h>
#include "project_v7.h"
#include "storage_multi.h"

#define S37_CAT_MAGIC 0x43373353u /* "S37C" in little-endian flash */
#define S37_CAT_VERSION 1u
#define S37_CAT_SECTIONS 8u
#define S37_CAT_PRESENT 0x01u
#define S37_CAT_HEADER_SIZE 80u
#define S37_CAT_PROJECT_OFFSET S37_CAT_HEADER_SIZE
#define S37_CAT_PAYLOAD_SIZE (S37_CAT_HEADER_SIZE + S37_CAT_SECTIONS * sizeof(project_v7_t))

typedef struct {
    uint32_t project_sum;
    uint16_t bars;
    uint8_t flags;
    uint8_t reserved;
} s37_cat_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint8_t present_mask;
    uint8_t default_section; /* 0..7, or 0xFF when no section exists */
    uint8_t reserved[2];
    s37_cat_entry_t section[S37_CAT_SECTIONS];
} s37_cat_header_t;

typedef struct {
    const sm_io_t *io;
    const sm_map_t *map;
    sm_state_t base;
    project_v7_t *replacement;
    s37_cat_header_t header;
    uint8_t section;
} s37_cat_overlay_t;

_Static_assert(sizeof(s37_cat_entry_t) == 8u, "catalogue directory entry layout");
_Static_assert(sizeof(s37_cat_header_t) == S37_CAT_HEADER_SIZE, "catalogue header layout");
_Static_assert(sizeof(project_v7_t) == 5396u, "catalogue project record layout");
_Static_assert(S37_CAT_PAYLOAD_SIZE == 43248u, "catalogue payload layout");

int s37_cat_header_valid(const s37_cat_header_t *header);
int s37_cat_validate(const sm_io_t *io, const sm_map_t *map,
                     const sm_state_t *state, project_v7_t *scratch,
                     s37_cat_header_t *header);
int s37_cat_load_section(const sm_io_t *io, const sm_map_t *map,
                         const sm_state_t *state, uint8_t section,
                         project_v7_t *out, s37_cat_header_t *header);
int s37_cat_prepare_create(s37_cat_overlay_t *overlay,
                           project_v7_t *replacement, uint8_t section,
                           uint16_t bars, uint8_t default_section);
int s37_cat_prepare_overlay(s37_cat_overlay_t *overlay,
                            const sm_io_t *io, const sm_map_t *map,
                            const sm_state_t *state, project_v7_t *scratch,
                            project_v7_t *replacement, uint8_t section,
                            uint16_t bars, uint8_t default_section);
int s37_cat_overlay_read(void *context, uint32_t offset,
                         uint8_t *dst, uint32_t length);
int s37_cat_blank_read(void *context, uint32_t offset,
                       uint8_t *dst, uint32_t length);
int s37_cat_create_read(void *context, uint32_t offset,
                        uint8_t *dst, uint32_t length);

#endif
