/* SPDX-License-Identifier: GPL-3.0-only
 * System 37: eight-section catalogue stored as one multi-sector A/B object.
 */
#ifndef SYSTEM37_SECTION_CATALOG_H
#define SYSTEM37_SECTION_CATALOG_H

#include <stdint.h>
#include "project_v7.h"
#include "storage_multi.h"

#define S37_CAT_MAGIC 0x43373353u /* "S37C" in little-endian flash */
#define S37_CAT_VERSION 2u
#define S37_CAT_SECTIONS 8u
#define S37_CAT_WORKING 8u
#define S37_CAT_RECORDS 9u
#define S37_CAT_PRESENT 0x01u
#define S37_CAT_HEADER_SIZE 84u
#define S37_CAT_PROJECT_OFFSET S37_CAT_HEADER_SIZE
#define S37_CAT_PAYLOAD_SIZE (S37_CAT_HEADER_SIZE + S37_CAT_RECORDS * sizeof(project_v7_t))

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
    uint32_t working_sum; /* the current working project / power-on autosave */
    s37_cat_entry_t section[S37_CAT_SECTIONS];
} s37_cat_header_t;

typedef struct {
    const sm_io_t *io;
    const sm_map_t *map;
    sm_state_t base;
    project_v7_t *replacement;
    s37_cat_header_t header;
    uint8_t section; /* 0..7: section A..H; S37_CAT_WORKING: autosave */
} s37_cat_overlay_t;

typedef struct {
    s37_cat_header_t header;
    const project_v7_t *section[S37_CAT_SECTIONS];
    const project_v7_t *working;
} s37_cat_image_t;

_Static_assert(sizeof(s37_cat_entry_t) == 8u, "catalogue directory entry layout");
_Static_assert(sizeof(s37_cat_header_t) == S37_CAT_HEADER_SIZE, "catalogue header layout");
_Static_assert(sizeof(project_v7_t) == 5366u, "catalogue project record layout");
_Static_assert(S37_CAT_PAYLOAD_SIZE == 48378u, "eight sections plus working-project payload layout");

int s37_cat_header_valid(const s37_cat_header_t *header);
int s37_cat_validate(const sm_io_t *io, const sm_map_t *map,
                     const sm_state_t *state, project_v7_t *scratch,
                     s37_cat_header_t *header);
int s37_cat_load_section(const sm_io_t *io, const sm_map_t *map,
                         const sm_state_t *state, uint8_t section,
                         project_v7_t *out, s37_cat_header_t *header);
int s37_cat_load_working(const sm_io_t *io, const sm_map_t *map,
                         const sm_state_t *state, project_v7_t *out);
int s37_cat_prepare_image(s37_cat_image_t *image,
                          const project_v7_t *const sections[S37_CAT_SECTIONS],
                          const uint16_t bars[S37_CAT_SECTIONS],
                          uint8_t present_mask, uint8_t default_section,
                          const project_v7_t *working);
int s37_cat_image_read(void *context, uint32_t offset,
                       uint8_t *dst, uint32_t length);
int s37_cat_prepare_working_overlay(s37_cat_overlay_t *overlay,
                                    const sm_io_t *io, const sm_map_t *map,
                                    const sm_state_t *state, project_v7_t *scratch,
                                    project_v7_t *replacement);
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
