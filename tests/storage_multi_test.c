/* SPDX-License-Identifier: GPL-3.0-only
 * Host fault-injection tests for storage_multi.c. No device or SDK required.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../firmware/src/storage_multi.h"

#define NOR_SIZE 0x100000u
static uint8_t nor[NOR_SIZE];
static int fail_after = -1;
static int partial_failure;
static int read_failure;
static uint32_t read_fail_sector = UINT32_MAX;
static uint32_t operations;

static int nor_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    (void)ctx;
    if (read_failure || (read_fail_sector != UINT32_MAX &&
        off < read_fail_sector + 4096u && off + n > read_fail_sector)) return -8;
    if (off > NOR_SIZE || n > NOR_SIZE - off) return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int should_fail(void)
{
    operations++;
    if (fail_after < 0) return 0;
    if (fail_after == 0) return 1;
    fail_after--;
    return 0;
}
static int nor_erase(void *ctx, uint32_t off)
{
    (void)ctx;
    if ((off & 4095u) || off > NOR_SIZE - 4096u) return -1;
    if (should_fail()) return -9;
    memset(nor + off, 0xFF, 4096u);
    return 0;
}
static int nor_program(void *ctx, uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i, write_n = n;
    (void)ctx;
    if (!n || n > 256u || (off & 255u) + n > 256u || off > NOR_SIZE || n > NOR_SIZE - off)
        return -1;
    if (should_fail()) {
        if (partial_failure) write_n = n / 2u;
        else return -9;
    }
    for (i = 0; i < write_n; i++) {
        if ((nor[off + i] & p[i]) != p[i]) return -2; /* NOR cannot set a cleared bit */
        nor[off + i] &= p[i];
    }
    return write_n == n ? 0 : -9;
}

static int check(const char *label, int ok)
{
    printf("%-58s %s\n", label, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

typedef struct { const uint8_t *data; uint32_t length; } mem_source_t;
static int memory_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    mem_source_t *m = (mem_source_t *)ctx;
    if (off > m->length || n > m->length - off) return -1;
    memcpy(dst, m->data + off, n);
    return 0;
}

typedef struct {
    const sm_io_t *io;
    const sm_map_t *map;
    sm_state_t base;
    const uint8_t *replacement;
    uint32_t replace_off, replace_len;
} overlay_source_t;
static int overlay_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    overlay_source_t *o = (overlay_source_t *)ctx;
    uint32_t end = off + n, a = o->replace_off, b = a + o->replace_len;
    if (sm_read(o->io, o->map, &o->base, off, dst, n)) return -1;
    if (off < b && end > a) {
        uint32_t from = off > a ? off : a;
        uint32_t to = end < b ? end : b;
        memcpy(dst + (from - off), o->replacement + (from - a), to - from);
    }
    return 0;
}

static int target_layout_check(void)
{
    static const sm_map_t catalog = {
        .object_id=0x3738u, .max_length=48648u,
        .sectors={
            {0xEB000u,0xEC000u,0xED000u,0xEE000u,0xEF000u,0xF0000u,
             0xF1000u,0xF2000u,0xF3000u,0xFE000u,0xB7000u,0xB8000u},
            {0x9D000u,0x9E000u,0x9F000u,0xA0000u,0xA1000u,0xA2000u,
             0xA3000u,0xA4000u,0xA9000u,0xAA000u,0xB9000u,0xBA000u}
        },
        .sector_count={12u,12u}
    };
    uint32_t c, i, j, total=0u;
    if (!sm_map_valid(&catalog) || sm_capacity(&catalog,0u)!=48896u ||
        sm_capacity(&catalog,1u)!=48896u ||
        sm_capacity(&catalog,0u)-catalog.max_length!=248u)
        return 0;
    for(c=0;c<2u;c++) for(i=0;i<catalog.sector_count[c];i++) {
        uint32_t x=catalog.sectors[c][i];
        int allowed=(x>=0x9D000u && x<=0xA4000u) ||
                    (x>=0xA9000u && x<=0xAA000u) ||
                    (x>=0xB7000u && x<=0xBA000u) ||
                    (x>=0xEB000u && x<=0xF3000u) || x==0xFE000u;
        if(!allowed || (x>=0xC2000u && x<0xEA000u) ||
           (x>=0xBB000u && x<0xC0000u) || (x>=0xF4000u && x<0xFE000u) ||
           (x>=0xFF000u))
            return 0;
        for(j=0;j<total;j++) {
            uint32_t d=j<catalog.sector_count[0] ? catalog.sectors[0][j] :
                catalog.sectors[1][j-catalog.sector_count[0]];
            if(d==x) return 0;
        }
        total++;
    }
    return total==24u;
}

int main(void)
{
    static const sm_map_t map = {
        .object_id = 0x1001u,
        .max_length = 12000u,
        .sectors = {{0x10000u,0x11000u,0x12000u},{0x20000u,0x21000u,0x22000u}},
        .sector_count = {3u,3u}
    };
    static uint8_t a[10000], b[10000], patch[700], got[10000];
    sm_io_t io = {0, nor_read, nor_erase, nor_program};
    sm_state_t state, after;
    mem_source_t src;
    overlay_source_t overlay;
    uint32_t i, baseline_ops;
    int bad = 0, rc;

    for (i = 0; i < sizeof a; i++) { a[i] = (uint8_t)(i * 17u + 3u); b[i] = (uint8_t)(i * 29u + 7u); }
    for (i = 0; i < sizeof patch; i++) patch[i] = (uint8_t)(255u - i);
    memset(nor, 0xFF, sizeof nor);
    bad += check("layout: three non-contiguous sectors per A/B copy", sm_map_valid(&map) && sm_capacity(&map,0)==12032u);
    bad += check("System 37 catalogue: 24 A/B sectors are unique and avoid user samples/protected flash",
                 target_layout_check());
    {
        sm_map_t overlap = map;
        overlap.sectors[1][2] = overlap.sectors[0][1];
        bad += check("layout: duplicate sector rejected", !sm_map_valid(&overlap));
        overlap = map; overlap.sectors[0][0] += 1u;
        bad += check("layout: unaligned sector rejected", !sm_map_valid(&overlap));
        overlap = map; overlap.max_length = 13000u;
        bad += check("layout: capacity overflow rejected", !sm_map_valid(&overlap));
    }
    bad += check("empty store: no valid copy", sm_scan(&io,&map,&state) != 0 && state.copy == -1);
    read_failure = 1;
    bad += check("read uncertainty: scan reports I/O failure", sm_scan(&io,&map,&state) == -2);
    src.data = a; src.length = sizeof a;
    bad += check("read uncertainty: save refuses to erase when scan cannot inspect copies",
                 sm_save_stream(&io,&map,memory_read,&src,sizeof a,&state) == -9);
    read_failure = 0;

    bad += check("first save: commits copy A", sm_save_stream(&io,&map,memory_read,&src,sizeof a,&state)==0 && state.copy==0 && state.sequence==1u);
    read_fail_sector = map.sectors[1][0];
    bad += check("degraded scan: readable copy remains available but is marked uncertain",
                 sm_scan(&io,&map,&state)==0 && state.copy==0 && state.uncertain);
    bad += check("degraded scan: save refuses to overwrite either copy under uncertainty",
                 sm_save_stream(&io,&map,memory_read,&src,sizeof a,&after)==-9);
    read_fail_sector = UINT32_MAX;
    bad += check("first read: crosses first-sector payload boundary", sm_read(&io,&map,&state,3700u,got,800u)==0 && !memcmp(got,a+3700u,800u));
    src.data = b; src.length = sizeof b;
    bad += check("second save: commits copy B", sm_save_stream(&io,&map,memory_read,&src,sizeof b,&state)==0 && state.copy==1 && state.sequence==2u);
    bad += check("second read: full segmented payload", sm_read(&io,&map,&state,0,got,sizeof b)==0 && !memcmp(got,b,sizeof b));

    /* Overlay one logical record without allocating a full catalogue buffer. */
    overlay.io=&io; overlay.map=&map; overlay.base=state; overlay.replacement=patch;
    overlay.replace_off=4200u; overlay.replace_len=sizeof patch;
    bad += check("overlay save: streams old payload plus one changed record",
                 sm_save_stream(&io,&map,overlay_read,&overlay,sizeof b,&after)==0 && after.copy==0 && after.sequence==3u);
    bad += check("overlay read: changed bytes only at target range",
                 sm_read(&io,&map,&after,0,got,sizeof b)==0 &&
                 !memcmp(got,b,4200u) && !memcmp(got+4200u,patch,sizeof patch) &&
                 !memcmp(got+4200u+sizeof patch,b+4200u+sizeof patch,sizeof b-4200u-sizeof patch));

    /* Fail each erase, payload page and final header operation in turn. */
    src.data=a; src.length=sizeof a; operations=0;
    rc=sm_save_stream(&io,&map,memory_read,&src,sizeof a,&state);
    baseline_ops=operations;
    bad += check("fault test setup: successful replacement", rc==0 && state.copy==1 && baseline_ops>10u);
    for (i=0;i<baseline_ops;i++) {
        sm_state_t recovered;
        src.data=b; src.length=sizeof b; fail_after=(int)i; partial_failure=(i>=3u); operations=0;
        (void)sm_save_stream(&io,&map,memory_read,&src,sizeof b,&after);
        fail_after=-1; partial_failure=0;
        rc=sm_scan(&io,&map,&recovered);
        if (rc || recovered.copy!=1 || recovered.sequence!=4u ||
            sm_read(&io,&map,&recovered,0,got,sizeof a) || memcmp(got,a,sizeof a)) {
            printf("fault point %u failed to preserve committed copy\n",i);
            bad++;
            break;
        }
    }
    bad += check("fault injection: every erase/page/header failure preserves prior A/B copy", i==baseline_ops);

    /* Make both copies valid again, then corrupt the newer one. */
    src.data=a; src.length=sizeof a;
    bad += check("fault recovery: a subsequent save can replace the damaged inactive copy",
                 sm_save_stream(&io,&map,memory_read,&src,sizeof a,&state)==0 && state.copy==0 && state.sequence==5u);
    nor[map.sectors[0][0]+SM_PAYLOAD_OFF+12u] ^= 1u;
    rc=sm_scan(&io,&map,&state);
    bad += check("recovery: corrupt newer payload falls back to older valid copy",
                 rc==0 && state.copy==1 && state.sequence==4u);
    bad += check("CRC: standard check value", sm_crc32("123456789",9u)==0xCBF43926u);

    printf("%s\n", bad ? "MULTI-SECTOR STORAGE TEST FAILED" : "multi-sector storage tests passed");
    return bad ? 1 : 0;
}
