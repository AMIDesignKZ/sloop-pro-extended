/* SPDX-License-Identifier: GPL-3.0-only */
/* Host tests for the System 37 six-track live-project adapter. */
#include <stdio.h>
#include <string.h>
#include "project_v7_live.h"

typedef struct {
    project_v7_t live;
    project_v7_t applied;
    uint32_t capture_tracks;
    uint32_t validate_tracks;
    uint32_t apply_tracks;
    uint32_t apply_globals;
    uint8_t fail_capture_track;
    uint8_t fail_validate_track;
    int last_all;
} fixture_t;

static int check(const char *name, int ok)
{
    printf("%-68s %s\n", name, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void seal(project_v7_t *p)
{
    p->magic = PROJ_V7_MAGIC;
    p->size = sizeof *p;
    p->sum = proj_v7_hash(p, (uint32_t)sizeof *p - 4u);
}

static void make_live(fixture_t *f)
{
    uint32_t track, step, lock;
    memset(f, 0, sizeof *f);
    for (track = 0; track < PROJ_V7_TRACKS; ++track) {
        proj_trk_v7_t *t = &f->live.t[track];
        t->engine = (uint8_t)(track % 4u);
        t->preset = (uint8_t)(track + 2u);
        for (step = 0; step < NSTEP; ++step) {
            t->step[step].time = ST_REST;
            t->step[step].note[0] = (uint8_t)(36u + track + step % 12u);
            t->fill[step / 4u] |= (uint8_t)(((step + track) % 3u) << (2u * (step % 4u)));
        }
        for (lock = 0; lock < PROJ_V7_LOCKS; ++lock)
            t->lock[lock] = (plock_t){LOCK_FREE, 0u, 0};
        t->lock[0] = (plock_t){(uint8_t)track, P_LEVEL, (int16_t)(80u + track)};
    }
    f->live.sel = 4u;
    f->live.drdly = 23u;
    for (track = 0; track < 32u; ++track)
        f->live.g[track] = (int16_t)(track * 2u);
    seal(&f->live);
    f->fail_capture_track = 0xFFu;
    f->fail_validate_track = 0xFFu;
}

static int capture_globals(void *ctx, project_v7_t *out)
{
    fixture_t *f = (fixture_t *)ctx;
    memcpy(out->g, f->live.g, sizeof out->g);
    out->sel = f->live.sel;
    out->drdly = f->live.drdly;
    memcpy(out->rsv, f->live.rsv, sizeof out->rsv);
    return 0;
}

static int capture_track(void *ctx, uint8_t track, proj_trk_v7_t *out)
{
    fixture_t *f = (fixture_t *)ctx;
    f->capture_tracks++;
    if (track == f->fail_capture_track)
        return -1;
    *out = f->live.t[track];
    return 0;
}

static int validate_globals(void *ctx, const project_v7_t *in, int all)
{
    fixture_t *f = (fixture_t *)ctx;
    (void)in;
    f->last_all = all;
    return 0;
}

static int validate_track(void *ctx, uint8_t track, const proj_trk_v7_t *in)
{
    fixture_t *f = (fixture_t *)ctx;
    f->validate_tracks++;
    if (track == f->fail_validate_track)
        return -1;
    return in->engine >= 12u ? -1 : 0;
}

static void apply_globals(void *ctx, const project_v7_t *in, int all)
{
    fixture_t *f = (fixture_t *)ctx;
    f->apply_globals++;
    f->last_all = all;
    memcpy(f->applied.g, in->g, sizeof in->g);
    f->applied.sel = in->sel;
    f->applied.drdly = in->drdly;
}

static void apply_track(void *ctx, uint8_t track, const proj_trk_v7_t *in)
{
    fixture_t *f = (fixture_t *)ctx;
    f->apply_tracks++;
    f->applied.t[track] = *in;
}

static s37_project_v7_live_ops_t make_ops(fixture_t *f)
{
    s37_project_v7_live_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.context = f;
    ops.capture_globals = capture_globals;
    ops.capture_track = capture_track;
    ops.validate_globals = validate_globals;
    ops.validate_track = validate_track;
    ops.apply_globals = apply_globals;
    ops.apply_track = apply_track;
    return ops;
}

int main(void)
{
    static fixture_t f;
    static project_v7_t captured, bad;
    s37_project_v7_live_ops_t ops;
    int failures = 0;
    uint32_t track;

    make_live(&f);
    ops = make_ops(&f);
    failures += check("capture serialises all six tracks and seals FUN7 checksum",
                      s37_project_v7_capture(&captured, &ops) == 0 &&
                      proj_v7_valid(&captured) &&
                      f.capture_tracks == PROJ_V7_TRACKS);
    failures += check("capture preserves project globals and all six track records",
                      captured.sel == f.live.sel && captured.drdly == f.live.drdly &&
                      !memcmp(captured.g, f.live.g, sizeof captured.g) &&
                      !memcmp(captured.t, f.live.t, sizeof captured.t));

    memset(&f.applied, 0, sizeof f.applied);
    f.validate_tracks = f.apply_tracks = f.apply_globals = 0;
    failures += check("apply preflights then applies all six tracks as a full project",
                      s37_project_v7_apply(&captured, &ops, 1) == 0 &&
                      f.validate_tracks == PROJ_V7_TRACKS &&
                      f.apply_globals == 1u && f.apply_tracks == PROJ_V7_TRACKS &&
                      f.last_all == 1 &&
                      !memcmp(f.applied.t, captured.t, sizeof captured.t));

    f.validate_tracks = f.apply_tracks = f.apply_globals = 0;
    failures += check("section apply still updates all six tracks with partial-global mode",
                      s37_project_v7_apply(&captured, &ops, 0) == 0 &&
                      f.validate_tracks == PROJ_V7_TRACKS &&
                      f.apply_globals == 1u && f.apply_tracks == PROJ_V7_TRACKS &&
                      f.last_all == 0);

    f.validate_tracks = f.apply_tracks = f.apply_globals = 0;
    f.fail_validate_track = 5u;
    failures += check("failed sixth-track preflight prevents every live mutation",
                      s37_project_v7_apply(&captured, &ops, 1) == -2 &&
                      f.validate_tracks == 6u && f.apply_globals == 0u &&
                      f.apply_tracks == 0u);
    f.fail_validate_track = 0xFFu;

    bad = captured;
    bad.t[5].engine = 12u;
    seal(&bad);
    f.validate_tracks = f.apply_tracks = f.apply_globals = 0;
    failures += check("unsupported engine rejected during preflight without partial apply",
                      s37_project_v7_apply(&bad, &ops, 1) == -2 &&
                      f.apply_globals == 0u && f.apply_tracks == 0u);

    bad = captured;
    bad.sum ^= 1u;
    f.validate_tracks = f.apply_tracks = f.apply_globals = 0;
    failures += check("invalid FUN7 checksum rejected before callbacks",
                      s37_project_v7_apply(&bad, &ops, 1) == -1 &&
                      f.validate_tracks == 0u && f.apply_globals == 0u &&
                      f.apply_tracks == 0u);

    f.capture_tracks = 0;
    f.fail_capture_track = 3u;
    failures += check("capture callback failure aborts incomplete six-track image",
                      s37_project_v7_capture(&captured, &ops) == -1 &&
                      f.capture_tracks == 4u && !proj_v7_valid(&captured));

    f.fail_capture_track = 0xFFu;
    f.capture_tracks = 0;
    failures += check("successful capture visits every track index 0–5 exactly once",
                      s37_project_v7_capture(&captured, &ops) == 0 &&
                      (track = f.capture_tracks, track == PROJ_V7_TRACKS));

    return failures ? 1 : 0;
}
