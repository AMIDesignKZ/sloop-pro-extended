# System 37 — live runtime migration audit

Date: 6 November 2026  
Branch: `extended-firmware`

## Purpose and boundary

This is a source-level dependency audit for the six-track runtime conversion. It supersedes the earlier assumption that the next task is simply to implement board callbacks for `project_v7_live`. The adapter is ready as an interface, but the current live model is not.

**No runtime source, constants, linker placement, flash map, or target firmware behaviour is changed by this audit.** The current model remains `NPART=3`, `NTRK=4`, `TRK_DRUM=3`, `NLOCK=24`.

## 1. Counts are separate concepts

The target must keep these counts distinct:

| Constant | Current | Target | Meaning |
|---|---:|---:|---|
| Synth parts | `NPART=3` | `5` | Parts with synth voices, synth engines and per-part engine state |
| Sequenced tracks | `NTRK=4` | `6` | All tracks, including drums |
| Drum index | `TRK_DRUM=3` | `5` | Dedicated drum track |
| Runtime parameter locks | `NLOCK=24` | `12` | Locks available on each live track |
| Drum voice pool | separate | separate | Existing `drums.v[]`, not part of the synth voice budget |
| Synth voice slots | 3 × 8 active arrays | 5 × 8 | Eight slots per synth part, with the existing global eight-sounding-voice ceiling |

Do not use `NTRK` for synth-only arrays or `NPART` for all-track arrays. The drum render unit remains separate from the five synth render units.

## 2. Confirmed source dependencies

### Core and voice allocation

- `core.h` embeds `voice_t v[NVOICE]` inside every `track_t`. The drum track does not use this array; its actual voices are held separately in `drums.v[]`.
- `voice.c` iterates synth voices using `NPART` and accesses them directly through `t->v[]` / `trk[p].v[]`. The shared eight-voice budget counts sounding voices across synth parts, not the total number of allocated slots.
- `engines.c` allocates `eng_arena[NPART]`; arena ownership and PHYS state are indexed by synth-part pointer offset. This is per synth part, not per sequenced track.
- `project.c` checks active synth voices with `NPART`; drum voices are checked separately through `drums.v[]`.

**Memory design candidate:** remove the inline `voice_t v[NVOICE]` from `track_t` and hold synth voice slots in a dedicated `voice_t synth_voice[NPART][NVOICE]` pool, accessed through a small track-to-part helper. The drum track then owns no unused synth voice array. This keeps the distinction explicit and avoids allocating six voice arrays when only five are required. The direct voice accesses in `voice.c` and `engines.c` must be converted together; any other direct access must be found before implementation. This is a candidate, not yet a committed design.

### Audio rendering and effects

- `fx.c` uses `NPART` for the synth INSERT array, the drum track's four separate INSERT states, the drum render-unit bit (`1u << NPART`), `U_ALL`, core-1 work masks, synth mix loops and FORMANT scheduling.
- `mix_units()` renders synth parts 0..`NPART-1` and then the separate drum unit. `units_split()` also reasons about synth engines and per-part state.
- The track filter array `tflt[NTRK]` is all-track state; the INSERT arena is synth-part state plus four drum paths. These must not be conflated.
- The engine arena is currently a union of GRAIN, DRAWBAR, FM6 and PHYS state. Five instances may be materially larger than three. The actual compiled `sizeof(eng_arena_t)`, linker placement and peak runtime use must be measured before selecting a final arrangement.

### Sequencer, recording and MIDI

- `seq.c` uses `NTRK` for MIDI Program Change state, MIDI note bookkeeping, sustain/held-note state, per-track roll and recording state, track selection and output note-off loops.
- Synth MIDI channel selection currently reads `G_CH1 + i` for synth indices below `NPART`; the current global parameter set exposes only `G_CH1`–`G_CH3`. Five independently configurable synth channels therefore require a versioned settings/UI treatment for channels 4 and 5, while preserving the meaning of the existing three settings. Drum channel remains separate.
- Drum-specific input, roll and recording paths compare against `TRK_DRUM`. These should remain explicit drum-role checks after the index moves to 5.
- Recording and solo masks are byte-sized today. Six tracks fit, but every bit shift, mask clear, serialised field and editor protocol bound still needs review.
- The 12-lock target changes not only `track_t.lock[]`, but also active-lock restoration arrays `lk_param[]`, `lk_base[]`, `lk_set[]`, `lk_n`, lock allocation, lock undo, lock editor messages and lock processing in `seq.c`.

### Project persistence and migration

- `project.c` now aliases its project structs to explicit FUN6 wire types, so the wire structure itself is frozen at four tracks and 24 locks.
- However, several legacy reader/converter loops still use live `NTRK`, `NPART`, `NLOCK` and `TRK_DRUM` (including lock loops, track conversion, drum identification and project capture/apply). Those uses must be replaced with explicit FUN6 constants or kept inside functions whose loop bounds are explicitly legacy values before any runtime count changes.
- The legacy drum index is 3 even after the new runtime drum index becomes 5. Legacy migration must use a fixed `PROJ_V6_DRUM_TRACK=3`, never the new `TRK_DRUM`.
- The new `project_v7_live` adapter is count-independent and iterates six FUN7 records through callbacks. It is not yet connected to board runtime state.
- The old `proj_capture()` / `proj_apply()` pair still operates on the four-track FUN6 runtime. They must not be allowed to silently become six-track FUN7 operations through changed constants.

### UI, editor and panel

- `ui_input.c` contains an explicit four-entry track colour table sized by `NTRK`, as well as track selection bounds and all-track loops. The colour table needs six deliberate entries.
- `editor.c` sizes the editor's track-value cache from `NTRK`, sends `NTRK` in the track-list protocol, and validates track indices against it. The browser editor must be updated in lockstep with the device protocol.
- `main.c` contains a four-fader loop. This is a physical-control count, not a track count; it must remain four. The two-layer assignment for six track levels still needs a verified mapping against the actual SMK-37 controls. No new fader mapping is assumed by this audit.
- `ui.c`, `ui_input.c`, `editor.c`, `project.c` and `arranger_scene.c` contain generic loops over all tracks and drum-specific branches that must be classified as either all-track or synth-only.

### Arrangement and sections

- The 16-entry chain is already independent of the four scene limit. The section catalogue and prefetch prototypes support A–H, but current runtime section application still works with the legacy four-slot model.
- `arranger_scene.c` applies a section by releasing all tracks and calling the legacy `proj_apply()`; this must be replaced by the validated FUN7 live apply path after the six-track runtime is ready.
- `seq.c` has multiple transition routes (arrangement boundary, live jump and quick chain). All must share one prepared-section hand-off; none may perform flash I/O or catalogue validation in the audio path.

## 3. Lock and microtiming migration

The target lock count is 12, but FUN6 remains 24-lock. The runtime reduction must not change the legacy wire layout or its import loops. FUN7 already has a 12-lock record and no microtiming array.

Before changing `NLOCK`:
1. Introduce an explicit legacy constant such as `PROJ_V6_LOCKS=24` and use it in all FUN6 decode/encode/conversion paths.
2. Keep runtime active-lock arrays at the same count as the live lock store; no 24-entry shadow arrays should remain unless there is a documented compatibility reason.
3. Confirm the lock ordering policy in migration (12 earliest-step valid locks; original slot order breaks ties).
4. Verify undo, lock replacement, lock clearing, and restoration of base parameter values with exactly 12 slots.
5. Remove runtime microtiming state and every `micro[]` / nudge access only after the FUN6 importer has copied fill conditions and intentionally discarded nudge values.

## 4. RAM and engine-state gates

The previous linker audit reported approximately 10,164 bytes free in `.pool`, but that figure predates a target build of the current branch and is not a reliable allowance for this conversion. Eight FUN7 records cannot live in `.noinit`; the proposed one-buffer staging model remains the only current candidate.

The target runtime adds two track records and two synth parts. The likely savings are:
- remove unused synth voice slots from the drum track by separating synth voice storage from `track_t`;
- remove the 64-byte per-track microtiming array;
- halve runtime lock storage from 24 to 12 entries, including active-lock bookkeeping;
- disable/remove selectable engines outside Analog and FM6 after preserving their IDs and writing explicit legacy project/preset mapping.

The likely costs are:
- two additional full track runtime records;
- two additional synth voice-slot groups (16 `voice_t` records net versus the three active synth parts today);
- two additional per-part engine state arenas if the current union is retained;
- FUN7 working/staging, song restore and autosave buffers, subject to proving buffer lifetimes and a target linker map.

No exact byte saving or target fit is claimed. The next memory checkpoint must measure `sizeof(track_t)`, `sizeof(voice_t)`, `sizeof(eng_arena_t)`, `sizeof(project_v7_t)`, all linker sections, stack high-water and worst-case audio CPU on the exact SMK-37 toolchain.

## 5. Recommended implementation sequence

1. **Freeze legacy constants:** define and use explicit FUN6 counts and drum index throughout every legacy import/export path. Add regression assertions that FUN6 remains 3,816 bytes, four tracks and 24 locks.
2. **Measure the present target:** build the unchanged runtime branch and record section sizes, `track_t`, `voice_t`, engine arena size, stack margin and CPU baseline. If the exact target toolchain is unavailable in the environment, request its build/map output only at this gate.
3. **Refactor synth voice ownership:** separate the five synth voice pools from the six sequencer track records, while keeping the current three-synth runtime behaviour unchanged. Run existing voice, audio, engine and drum regression tests.
4. **Reduce runtime locks and remove microtiming:** change live storage to 12 locks and no micro array while preserving the FUN6 wire reader. Add focused lock/undo and step timing tests.
5. **Expand runtime tracks and synth parts together:** change the role constants and all audio, engine-arena, FX, MIDI, recorder, UI and editor arrays as one coherent patch. Do not expose a half-six-track state.
6. **Implement board callbacks:** connect `project_v7_live` to the new runtime, with preflight validation and an IRQ-safe apply transaction. Only then integrate section prefetch and the arranger.
7. **Re-measure and validate:** target build, linker map, stack and CPU, complete host suite, project migration/round-trip and hardware recovery checks.

## 6. Current status

This audit is source-derived and committed as a design checkpoint only. No board callbacks, runtime constants, `track_t`, voice pool, lock storage, engine arena, sequencer, UI, editor, flash map or linker layout have been changed. The project remains at the four-track runtime baseline. The next safe code change is step 1: isolate all FUN6 reader dependencies from live runtime constants and add explicit legacy regression checks.
