# SLOOP Pro Extended — firmware architecture proposal

Target: M-VAVE SMK-37 Pro. This is a source-derived design proposal for review before implementation. No firmware source, flash map or installed device has been changed by this document.

## 1. Architectural boundaries

Separate the firmware into six cooperating domains:

1. **Track/runtime model** — five synth tracks plus one dedicated drum track.
2. **Audio engine** — Analog and FM6 synth engines, drum synthesis/sample playback, shared voice allocator, per-track effects and master mixer.
3. **Sequencer/song model** — 64-step patterns, 12 locks, fill conditions, eight sections and a 16-entry arrangement.
4. **Persistence** — versioned project catalogue, autosave, FM6 library and existing small settings objects.
5. **Control and MIDI** — SMK-37 panel, four fader layers, MIDI/DAW mapping and editor protocol.
6. **Browser editor** — six-track UI, A–H sections, four FM6 banks, two user sample slots, backup/restore and version negotiation.

The target is not achieved by changing NPART/NTRK alone. Every array, bit mask, scheduler unit, MIDI route, project migration path, UI bound and editor payload must be updated as one coordinated change.

## 2. Runtime track model

### Track indexing

Use stable zero-based internal indices:

| Index | User track | Runtime role |
|---:|---|---|
| 0 | 1 | Synth |
| 1 | 2 | Synth |
| 2 | 3 | Synth |
| 3 | 4 | Synth |
| 4 | 5 | Synth |
| 5 | 6 | Dedicated drums |

The target constants are NPART=5, NTRK=6 and TRK_DRUM=5. All drum-unit masks and mixer routing must derive from these constants rather than retain literal bit 3 assumptions.

### Voice allocation and CPU

The current voice implementation gives each synth part eight voice-state slots but enforces a shared maximum of eight simultaneously sounding synth voices across all synth parts. The drum engine has its own voice pool. Preserve that model unless measurements show it needs redesign; do not describe the target as 40-voice polyphony.

Five tracks increase the per-track state and the number of sequencer/effects units visited per audio block. The shared voice ceiling limits active synth rendering but does not eliminate the extra track-loop, modulation, mixer and insert costs. Measure worst-case CPU with all five tracks configured, including FM6-heavy material, before accepting the target.

### Engine identity and lifecycle

Only Analog and FM6 are selectable in the target UI. Keep their persistent engine IDs unchanged: Analog remains 0 and FM6 remains 9. Keep all historical engine IDs reserved; never compact or renumber the engine table.

Introduce an explicit legacy-engine mapper at project/preset load. IDs 0 and 9 load as Analog and FM6. Other historical IDs load as a safe Analog default while retaining common parameter values, pattern data, track level/pan/mute and locks that remain valid. Engine-specific values are clamped or defaulted against Analog descriptors. Do not use modulo arithmetic to map unsupported IDs.

Remove unsupported engines from the selectable engine list. Source modules and arena members should only be removed after dependency analysis and target memory/flash measurements; disabled IDs must still be recognised by migration code. The engine arena union should ultimately contain only state required by enabled engines, but its final size must be compiler-measured.

## 3. Project format and migration

### Format 7

Preserve the existing format-6 reader as a read-only migration input. Define a new format-7 project structure with:

- Six track records.
- Existing 64 × 10-byte step representation.
- A 16-byte fill-condition array per track (two bits per step).
- No micro-timing array.
- 12 parameter locks per track.
- Existing global/project fields and checksum, with an explicit format/version marker.

Source-derived size calculation:

- Current format-6 project: 3,816 bytes, four tracks.
- Current track record: (3,816 − 76-byte header − 4-byte checksum) / 4 = 934 bytes.
- Current per-step micro/fill field: 64 bytes. New fill-only field: 16 bytes.
- Lock reduction: 24 to 12 entries × 4 bytes = 48 bytes saved.
- New track record: 934 − 64 + 16 − 48 = **838 bytes**.
- New project: 76 + (6 × 838) + 4 = **5,108 bytes**.

Add compile-time sizeof assertions. Host tests must verify exact layout, checksum, fill round-trip, lock truncation, invalid values and legacy migration. Do not rely on this arithmetic as a substitute for compiler output.

### Format-6 migration

Map old synth tracks 0–2 to new synth tracks 0–2. Initialise new synth tracks 3–4 from the Analog default. Move the old drum track from index 3 to new index 5. Preserve globals, patterns, drum lanes, fill conditions and compatible common parameters. Discard micro-timing.

Old projects can contain up to 24 locks per track. Format 7 can retain only 12. Migration retains the 12 earliest-step valid locks. For locks on the same step, preserve original slot order as the tie-break. Drop later-step locks and expose a migration warning if any are lost. Host tests must cover empty, 12-lock, 13-lock and 24-lock cases.

### Section catalogue

Do not allocate eight independent A/B objects. Store all eight format-7 projects in one catalogue payload:

- Project records: 8 × 5,108 = 40,864 bytes.
- Catalogue header/directory allowance: approximately 80 bytes.
- Modelled payload: approximately 40,944 bytes.
- Eleven sectors per copy: 44,800 bytes available after the first-sector 256-byte header area.
- A/B catalogue: 22 sectors total.

The catalogue directory should include format version, section-presence bits and compact metadata needed by the arranger (for example, cached section bar lengths). Project records retain their own magic/checksum; the outer storage record has its own length and CRC. Section reads load one 5,108-byte record from the selected catalogue copy into a RAM cache buffer and validate its project checksum.

Saving one section is a whole-catalogue copy-on-write transaction: stream the current valid catalogue into the inactive copy using a 256-byte page buffer, overlay the changed section record and its directory metadata, calculate the complete payload CRC, verify the written payload, then program the commit header last. Do not stage the 40 KiB catalogue in RAM. The old copy remains untouched until the new one is committed and read back. This costs more writes per section save but gives a single atomic A/B snapshot for A–H.

## 4. Section RAM/cache strategy

Do not expand the current proj_slot[4] array to eight. It currently lives in .noinit, which is only 0x3D50 bytes and is nearly full.

Use two format-7 project buffers in .pool:

- Buffer 0: current/working section.
- Buffer 1: next arrangement section or staging section.
- Swap roles only at a controlled audio-block boundary.
- UI section selection while stopped loads the selected section into the working buffer.
- Arrangement playback preloads the next section in the main loop before its boundary.
- The audio ISR applies only a fully validated RAM-resident buffer. It never reads flash, erases flash or waits for storage.
- If prefetch or validation fails, stop/abort the arrangement safely and report the section error; never fall back to flash access in the ISR.

The existing song-restore snapshot and autosave buffer also become format 7. This is approximately 10.2 KiB for the two-section cache, plus 5.1 KiB each for song restore and autosave, before staging and metadata. Verify all placements against the actual linker map.

## 5. Multi-sector A/B storage layer

The current storage layer supports one 4 KiB sector per copy and a maximum 3,840-byte payload. It must not be enlarged by simply changing ST_PAYLOAD_MAX.

Add a separate multi-sector object API (name is provisional) with:

- A distinct storage magic and header version, separate from the legacy one-sector ST_MAGIC, so old firmware cannot mistake a new multi-sector record for a legacy object. Header fields include object type, copy, sequence, logical length, sector count, payload CRC and header CRC.

- A fixed extent list for each logical object and each A/B copy. An extent is a sector-aligned flash address; extents may be physically non-contiguous.
- A logical payload stream across the ordered sector list. The first sector reserves bytes 0–255 for header/commit metadata; subsequent sectors contribute their full payload area.
- Fixed expected sector counts and payload lengths per object/version.
- Streaming read, write and CRC routines; no need to stage the entire FM6 library in RAM.
- Inactive-copy erase, payload programming, complete readback/CRC verification and commit-header programming last.
- Boot-time validation of both copies and selection of the highest valid sequence; fall back to the older valid copy after torn writes.
- Bounds checking against the exact per-object extent list as well as the low-level flash allow-list.
- Power-loss fault-injection tests at every erase/program/commit stage.

Keep existing one-sector storage for small settings objects. Do not weaken current A/B guarantees.

### FM6 library object

Four banks contain 4 × 32 × 128 = 16,384 patch bytes. With a versioned directory and metadata, model the object at approximately 16.4 KiB. Five sectors per copy provide 20,224 payload bytes; ten sectors total for A/B.

The current FM6 code assumes one contiguous XIP object and stages a 3,472-byte bank in proj_tmp. Replace this with a logical segmented reader. Patch reads copy the selected 128-byte record through the extent reader. A patch update clones the current object into the inactive copy using a small page buffer, overlays the changed record, calculates the complete CRC and commits last. This avoids allocating a 16 KiB RAM bank buffer. Standard DX7 SysEx import/export operates on one 32-patch bank at a time.

Migrate the old 27-patch bank into Bank A slots 1–27. Keep the remaining slots empty. Preserve factory patches and the existing patch-selection behaviour where compatible.

### Autosave

Autosave remains a separate A/B project object. At 5,108 bytes it requires two sectors per copy (four sectors total). Use the same multi-sector transaction rules. This adds two sectors relative to the existing one-sector-per-copy object.

## 5A. Host-tested storage primitive prototype

A first isolated implementation now exists in firmware/src/storage_multi.h and firmware/src/storage_multi.c, with tests/storage_multi_test.c. It is not yet included by felucca.c and does not replace or modify storage.c.

The primitive accepts per-copy, possibly non-contiguous sector lists and a flash callback interface. It provides:

- A 32-byte versioned header with object identity, copy identity, sequence, payload length, sector count, payload CRC and header CRC.
- Logical payload addressing across the first sector's 3,840-byte payload area and subsequent full sectors.
- 256-byte page-bounded streaming writes using a caller-supplied source callback; a catalogue can therefore copy its old contents and overlay one section without a 41 KiB RAM staging buffer.
- Full payload readback/CRC before the header is programmed as the commit record.
- A/B selection with wrap-aware sequence comparison and CRC fallback.
- A degraded-read state: a valid copy may still be read if its peer cannot be inspected, but saves are refused while either copy is uncertain. This prevents a transient read error from being mistaken for an empty store and causing an erase of a potentially valid copy.
- Map validation for sector alignment, duplicate extents and payload capacity. The board flash callbacks must still enforce the independent hardware allow-list and protected-region guard.

The host test uses a simulated NOR device that enforces erase-to-FF, 1-to-0 programming and 256-byte page boundaries. It also instantiates the proposed SMK-37 catalogue/FM6/autosave extent tables and checks all 36 sectors are unique, 24 are newly allocated and all addresses fall within the candidate ranges. It checks non-contiguous reads/writes, streamed overlays, all erase/page/header failure points, partial page/header programming, read uncertainty, CRC corruption and fallback to the older committed copy.

The standalone test was compiled with the host C compiler using -std=c11 -Wall -Wextra -Werror -O2 and passed. The same test is added to tests/run_tests.sh. This is host evidence for the generic primitive only; it is not a target build, not a hardware flash test, and not yet proof of the proposed SMK-37 extent map.

## 6. Proposed SMK-37 sector extent map

This is a proposed allocation for validation, not permission to write these addresses before migration and guard tests pass.

| Logical object copy | Physical sectors | Count |
|---|---|---:|
| Section catalogue A | 0xD6000–0xE0FFF | 11 |
| Section catalogue B | 0x9D000–0xA4FFF and 0xB7000–0xB9FFF | 11 |
| FM6 library A | 0xE1000–0xE5FFF | 5 |
| FM6 library B | 0xAB000, 0xAC000, 0xE6000, 0xE7000, 0xBA000 | 5 |
| Autosave A | 0xE8000–0xE9FFF | 2 |
| Autosave B | 0xA9000–0xAAFFF | 2 |

The allocation uses 24 new sectors: all 20 sectors currently occupied by retired USR3/USR4 sample slots and the four existing SLOOP spare sectors at 0xB7000–0xBAFFF. It reuses the current project, FM6 and autosave sectors for the second copies.

Keep these 10 candidate sectors unused: 0xEB000–0xF3FFF (nine sectors) and 0xFE000 (one sector). Do not touch 0xEA000 (stock marker), 0xF4000–0xFDFFF (stock resources), 0xFF000 (key_mac), BTIF, USRTRIM, OTA staging, bootloader, stock VM or recovery regions.

### Flash guard changes required

The current SMK-37 sample allow-list spans 0xC2000–0xE9FFF. Split it into:

- USR1/USR2 sample region: 0xC2000–0xD5FFF (two × 40 KiB).
- Extended storage region: 0xD6000–0xE9FFF.

Add an explicit extended-storage allow-list window and compile-time protected-range assertions. Keep the 0xEA000 stock-marker sector as its existing special whole-sector erase operation only. Update the independent RAM-driver guard and host flash-map tests. The unlisted EB–F3 and FE sectors must remain disallowed.

The stock-marker release hook must run before the first erase anywhere in the repurposed USRFLASH area (0xC2000–0xE9FFF), not merely before writes to the new two-slot sample range. Keep legacy stock-mapping detection aware of the old four-slot layout while old data can still be present.

The SMK store owner check must account for the new catalogue/FM6/autosave extents and the newly used B7000–BA000 sectors when clearing a foreign firmware-family store. It must continue to preserve USR1/USR2 sample data and all protected stock regions.

## 7. Migration and first-boot transaction

Before any retired sample sector is erased, scan for valid USR3/USR4 headers. If either contains data, expose a legacy-sample export/confirmation path in the editor. Do not silently overwrite those samples. New backups contain USR1/USR2 only; legacy four-slot backup restore imports the first two and ignores retired slots.

Migrate persistent objects in a failure-safe order:

1. Read and validate legacy project slots A–D, legacy FM6 bank and autosave using the old sector map.
2. Build and commit section catalogue copy A in the retired-sample range. The old project slots remain untouched.
3. Clone and commit catalogue copy B into the old project sectors plus the assigned B7000–B9FFF sectors. Copy A is already valid before the old project sectors are erased.
4. Build and commit FM6 copy A in E1000–E5FFF from the old 27-slot bank.
5. Clone and commit FM6 copy B using the old AB000/AC000 sectors plus its assigned new extents. Copy A is valid before the old bank sectors are reused.
6. Build autosave copy A in E8000–E9FFF, then clone and commit copy B using the old A9000/AA000 sectors.
7. Record/verify the new layout version and run a complete boot-time integrity scan before enabling normal writes.

Each migration step must be idempotent. On power loss, the next boot must identify the newest valid copy and resume safely. Never erase the only valid old or new copy. Keep an explicit legacy-sector map available until migration is complete.

## 8. MIDI, mixer, editor and drum integration

- Extend per-synth MIDI channel settings from three to five without changing the meaning of the existing first three settings. Keep drum and FX channel handling separate.
- Update all MIDI input/output, CC, recording, DAW mode, user mapping and track-select paths.
- Four physical faders use two layers. Proposed first layer: synth tracks 1–4. Proposed second layer: synth track 5 and drums on two faders, with the remaining two assigned to master/FX or user-selectable controls. Confirm the actual panel button/layer interaction against the SMK-37 hardware map before coding.
- Keep the dedicated drum engine. Retain the generated synth kit library, 808/909, editable SYN1–SYN4 kits and sampled drum playback.
- With two sample slots, expose only USR1 and USR2 as user sample drum kits. Remove USR3, USR4 and USR3+4 from new UI paths while preserving legacy backup parsing.

Drum-kit identifiers are persistent project values too. Do not let DRUM_SYN become a different numeric value merely because SMP_USER_SLOTS changes. Keep the old USR3, USR4 and USR3+4 identifiers reserved, and keep SYN1–SYN4 at their current numeric IDs. Hide the retired values from new selectors. When a legacy project references a retired user kit whose sample data is no longer available, map it explicitly to the default GM/sample kit (or another documented safe default); never reinterpret it as SYN1 or another different kit. Add tests for every old kit ID.
- Update the browser editor's NTRK/TRK_DRUM constants, six-track controls, A–H scene logic, 12-lock UI, fader layers, FM6 bank A–D selector, two sample slots, backup IDs and protocol version. Do not use broad unreviewed regex replacements in the large generated editor HTML.

## 9. Validation gates before hardware build/install

### Host-level
- Compile-time assertions for format-7 track/project/catalogue/bank sizes.
- Project migration tests for formats 1–6, including old drum-track relocation, default new synth tracks, fill preservation and lock truncation.
- Catalogue and multi-sector A/B tests with simulated NOR; inject failure after every sector erase, page program, verification and header commit.
- Test all proposed extent boundaries, sector uniqueness, alignment, payload capacity and guard rejection for every protected/unlisted region.
- FM6 tests for 4 × 32 patch slots, 128-byte DX7 SysEx import/export, patch update and migration of 27 legacy patches.
- Drum/sample tests for USR1/USR2, legacy four-slot backups, SYN kits, 808/909 and the absence of retired slots from new selectors.
- Editor protocol tests for six tracks, eight sections, MIDI channels and backup/restore.
- Audio regression and target CPU-budget tests with five synth parts.

### Target build and hardware
- Build the exact SMK-37 target with the supported JieLi toolchain and SDK.
- Inspect linker map for .pool, .bss, .data, XIP, RAMTEXT and stack headroom.
- Measure flash erase/program time and worst-case catalogue save time while stopped.
- Test arrangement prefetch and section changes under load; prove no flash operation occurs in the audio ISR.
- Verify stock/V16 recovery independently before any installation.
- Install only a recoverable test build after all host and target gates pass.

## 10. Open implementation gates

The functional scope is agreed, but these items require source/test evidence before coding or hardware deployment:

1. Confirm compiler sizes and actual target memory headroom.
2. Implement and fault-test the segmented multi-sector A/B layer.
3. Validate the extent map, ownership clearing and stock-marker lifecycle.
4. Define lock truncation warning behaviour and legacy engine mapping in tests.
5. Confirm fader-layer button mapping on the physical SMK-37.
6. Complete legacy USR3/USR4 export and first-boot migration UX.
7. Establish the target audio CPU budget with five synth parts.

Until those gates pass, the allocation is **capacity-feasible on paper, not yet proven safe or hardware-feasible**.


## 5B. System 37 Project Format 7 prototype

An isolated FUN6-to-FUN7 schema and conversion module has been added:

- `firmware/src/project_v7.h` defines the read-only FUN6 wire layout and the new six-track FUN7 layout.
- `firmware/src/project_v7.c` validates the FUN6 checksum, constructs FUN7, calculates the new checksum and reports a per-track lock-loss bitmask.
- `tests/project_v7_test.c` covers the compiler-sized wire layouts and migration behaviour; it is wired into `tests/run_tests.sh`.

The FUN6 mirror fixes its historical four-track and 24-lock counts explicitly. It must not depend on future runtime `NTRK` or `NLOCK` values, or a later expansion could corrupt the legacy reader's layout.

The converter preserves globals, parameters, engine/preset bytes and all 64 steps. It packs each old step's two-bit fill condition into a 16-byte array and discards the six-bit micro-timing value. The old synth tracks 0–2 remain at 0–2. The two new synth tracks occupy 3–4, and the new dedicated drum track occupies 5. The legacy drum track at index 3 is deliberately not migrated; tracks 3–5 start from defaults. A saved selection of old track 3 selects the new default drum track at index 5. New synth records start empty, with Analog selected, common and Analog-specific parameters populated from the supplied descriptors, and the default-preset sentinel. The new drum track uses drum-kit default 0.

Lock migration uses the same lockable-parameter rules as the current sequencer. It retains the globally earliest 12 valid locks, sorts them by step, and preserves original source-slot order for equal-step ties. A per-track warning mask is returned when more than 12 valid locks existed; the eventual UI must present that warning to the user.

The module is deliberately isolated. It is not included by `felucca.c`, does not alter `project.c` or `proj_import`, and does not change runtime track constants, section RAM, autosave or flash storage. The existing FUN1–FUN6 import path therefore remains untouched. A local strict-warning host harness and its ASan/UBSan run passed. The complete repository host suite and the JieLi target build have not yet been run in this working session.

The existing project host test now invokes the FUN6-to-FUN7 converter after importing FUN1, FUN2, FUN3, FUN4, FUN5 and a current FUN6 image. It checks the new checksum, preservation of the three legacy synth tracks, deliberate omission of old drum data, and defaulted new synth/drum tracks. The isolated format-7 test separately covers lock-loss reporting, stable tie ordering, 12/13/24-lock boundaries, empty lock arrays and malformed input. These test sources are wired into the repository runner, but the complete runner has not yet been executed in this session. The next gate is to compile and run those tests in the repository build environment before connecting the converter to project persistence or changing runtime track counts.


## 5C. System 37 A–H catalogue prototype

The isolated catalogue implementation is in `firmware/src/section_catalog.h` and `firmware/src/section_catalog.c`. Version 1 is an 80-byte directory followed by eight fixed 5,108-byte FUN7 project records: 40,944 bytes total. The directory carries the present-section mask, default section, and one 8-byte entry per section (project checksum, cached bar count, flags and reserved byte). The outer multi-sector object header remains responsible for A/B copy identity, sequence, payload CRC and commit semantics.

The module validates directory consistency and can validate every present project using one caller-supplied project-sized scratch buffer. It can load one section, create the first catalogue from a streaming blank source, or prepare a copy-on-write section update. The update source streams the committed catalogue and overlays only the 80-byte directory and the changed 5,108-byte record. The generic storage writer handles the 256-byte page writes, full-payload verification and commit-last sequence; no full-catalogue RAM buffer is introduced.

The eleven-sector A/B extents offer 44,800 bytes per copy, leaving 3,856 bytes of headroom after the v1 payload. The directory's cached bar count is supplied by the caller; it is not inferred from project step data at this layer.

`tests/section_catalog_test.c` covers catalogue creation, all eight sections, individual section loading, replacement of one section without changing the other seven, metadata updates and invalid inputs. It is wired into `tests/run_tests.sh`, but has not yet been compiled or run in this session. No runtime project/arranger integration, physical flash map change, board flash-guard change or RAM placement change has been made.


## Runtime arranger audit and RAM gate (6 November 2026)

The source audit confirms that the existing 16-entry song chain can be retained without changing its entry count or timing model. The scene namespace is currently four entries and is hard-coded in `arranger.h`, `arranger_scene.c`, `ui_song.c`, `project.c` and live/quick-chain paths in `seq.c`.

The existing song arranger calls `arrangement_apply()` from the audio sequencer path at section boundaries. That function releases track state and calls `proj_apply()` against an already resident `proj_slot[]` record. No flash I/O or full-catalogue validation belongs in that callback.

The current four FUN6 section records occupy `proj_slot[4]` in `.noinit`. Song-mode preservation uses another full `project_t` in `.pool`; autosave uses a separate full project buffer, and project import has its own staging union. The documented `.noinit` budget is 0x3D50 bytes and is nearly consumed by the four current project records. This rules out simply changing the array to eight FUN7 projects. The actual linker map and measured pool/stack headroom must be obtained before choosing runtime buffers or moving objects between memory regions.

The implementation direction to evaluate is a bounded prefetch pipeline: read and validate the next catalogue record in the main loop, publish a ready staging record, and let the audio boundary apply only a prepared record. After the boundary, the main loop can prepare the following entry. This must also cover the live-section and quick-chain mechanisms in `seq.c`, and must define what happens if prefetch is late (never apply a partially loaded project). The number and placement of buffers remain open pending linker evidence and a host-tested concurrency/state-machine design.

### Legacy drum migration decision

The user has agreed that the FUN6-to-FUN7 migration may omit the old drum track's pattern and parameter data. This does **not** reduce the fixed FUN7 project size or the six-track runtime footprint: the new dedicated drum track still exists. It does simplify migration and avoids carrying old drum-channel state into the new drum architecture. The three legacy synth tracks remain mapped to synth tracks 0–2; new synth tracks 3–4 and the new drum track 5 are initialised from defaults. The old project remains recoverable through the untouched legacy firmware/backup route. The isolated converter and tests must be updated to reflect this decision before integration.

No runtime, flash map, linker placement or audio behaviour has been changed by this audit.


## Detailed RAM budget and single-buffer prefetch candidate (6 November 2026)

The checked linker script defines `.noinit` as 0x3D50 (15,696 bytes). The four existing format-6 project slots consume 15,264 bytes, leaving only 432 bytes. Eight FUN7 section projects cannot be kept resident there (40,864 bytes). The checked RAM audit reports 333,900 of 344,064 bytes used in `.pool` (10,164 bytes free) and 58,220 of 98,304 bytes used in the ordinary RAM region (40,084 bytes free). These are baseline figures from the RAM audit, not measurements of a System 37 target build.

A naïve pair of 5,108-byte section buffers would require 10,216 bytes, 52 bytes more than the reported free pool before accounting for the format growth of the song backup and autosave buffers. Each of those grows by 1,292 bytes, or 2,584 bytes combined. The ordinary RAM region has more reported headroom, but its import staging, track expansion and stack high-water marks still need measurement.

The more promising candidate is one prefetch/staging project: the currently playing section is already materialised into live `trk[]` state by `proj_apply()`, so it need not remain as a second project object. Main-loop code can load and validate the next catalogue record into staging; the audio callback applies it at the boundary and signals that staging is reusable. The main loop then preloads the following arrangement entry. A strict ready/consumed handshake is required, and a late preload must never expose a partially written project to the audio callback.

A second optimisation worth validating is sharing the full-project song-mode backup and autosave buffer. In the current code autosave exits while `song.playing`, and the reset/section flush path also avoids autosaving while the arrangement clock runs. That suggests their lifetimes do not overlap, but all editor import/export, reset and error paths must be checked before making them a union.

No buffer has been allocated and no linker placement has changed. Before implementation, obtain a fresh SMK-37 target build/map and record `.bss`, `.pool`, `.noinit`, main-stack and supervisor-stack high-water marks. Then host-test the single-buffer state machine, including underrun, stale request, failed read, stop, loop, live jump and quick-chain cases.


## Section prefetch hand-off prototype — tested, not integrated (6 November 2026)

The new `section_prefetch` module is an isolated state machine for the single staging-buffer design. It has five states: IDLE, REQUESTED, LOADING, READY and FAILED. Main/UI requests carry a generation ticket; the main loop loads and validates the record; READY is published only after validation; the audio-boundary consumer takes a matching section exactly once. Superseded tickets, cancellation and failed loads cannot publish stale/partial records. The module performs no I/O and does not own the staging buffer.

The host test is wired into `tests/run_tests.sh`. Isolated strict C11 compilation (`-Wall -Wextra -Werror`) and execution passed; a UBSan build and execution passed. ASan execution was blocked by the local environment's ASan runtime ordering. The full repository test runner and SMK-37 target build remain unrun.

The state machine is deliberately not yet connected to `seq.c` or `arranger.c`. Integration still needs a pre-boundary prefetch policy for the 16-entry arrangement, quick chain and live section jumps, plus first-section readiness before song playback starts. It must define deterministic underrun behaviour; a late or invalid load must never be applied. No runtime buffer, flash map or audio callback has been changed.

## Progress checkpoint — legacy drum migration removed (6 November 2026)

The isolated FUN6-to-FUN7 converter now migrates only legacy synth tracks 0–2. The old drum track at index 3 is intentionally ignored: its pattern, kit selection, per-lane sound values and parameter locks are not copied into FUN7. New synth tracks 3–4 and the dedicated drum track 5 are initialised from defaults. The new drum track's E0 value is explicitly set to the default drum kit (kit 0), rather than inheriting the Analog engine's E0 default. A saved selection of legacy track 3 still selects the new dedicated drum track at index 5.

The migration test now expects lock-loss bits only for the three migrated synth tracks (mask 0x07), checks the three legacy synth tracks, and checks that all three new tracks are empty with default parameters and free locks. The old FUN6 source record is still validated as a complete 3,816-byte image before conversion; no legacy persistence or recovery path is changed.

The source and test changes are committed and wired into the repository host runner. **The updated test has not yet been compiled or executed in this session.** The full host suite and SMK-37 target build remain unrun. This completes the migration-policy change only; it does not integrate FUN7 into live project persistence or the sequencer.

## Runtime conversion gate — six-track live model

The FUN7 migration policy is now reflected in both the isolated converter test and the legacy project regression helper. Before changing runtime constants, the live-project conversion needs a separate implementation plan because the existing project.c project_t is still the persisted FUN6 layout and its track count/lock count are tied to the current NTRK=4 and NLOCK=24 definitions.

The runtime transition must freeze the FUN6 wire layout independently of future runtime constants, then make FUN7 the live project representation. That entails:
- Keep the FUN1–FUN6 readers byte-for-byte compatible, with explicit legacy constants (four tracks, three synth parts, 24 locks).
- Convert every imported/loaded legacy project into FUN7 before it becomes the live section or working project.
- Replace proj_capture() and proj_apply() as a matched pair for six live tracks, five synth parts, the dedicated drum track at index 5, packed fill conditions and 12 locks.
- Audit every NTRK, NPART, NLOCK and TRK_DRUM dependent array, loop, MIDI route, recorder, UI, voice allocator and test before changing core.h.
- Preserve the old storage readers and recovery route; do not write FUN7 to legacy project sectors as if it were FUN6.

The current branch has not yet made any runtime-constant or live-project changes. No full host suite or target build has been run for the migration change. The next implementation unit is a host-testable FUN7 live-project adapter and its regression tests, followed by a coordinated runtime model change only when all legacy wire-format dependencies are isolated.

## Progress checkpoint — catalogue-backed prefetch service and RAM gate (6 November 2026)

A small main-loop service now joins the existing catalogue reader to the tested prefetch state machine:

- New files: firmware/src/section_prefetch_io.h and firmware/src/section_prefetch_io.c.
- s37_pf_service() claims at most one request, scans the A/B multi-sector object, loads the requested A–H record through s37_cat_load_section(), and publishes READY only after the catalogue and FUN7 checks succeed.
- A failed scan, absent section, invalid directory or invalid project checksum publishes FAILED. If a newer request supersedes the synchronous load, its completion ticket is rejected and the stale buffer is not made READY.
- The service owns no staging memory. The caller supplies one project_v7_t buffer. It is intended for the main loop only; it must never be called from the audio callback.
- tests/section_catalog_test.c now exercises a missing-section failure and a successful load/one-shot consume. tests/run_tests.sh links the service and prefetch modules into the catalogue test.

This connects the persistence prototype to the prefetch primitive at the host-module boundary, but does not allocate a runtime buffer or connect seq.c/arranger.c. The updated catalogue test has not yet been compiled or run in this session.

### RAM decision remains a gate

The existing audit is still only a baseline: .noinit is 0x3D50 bytes, with 15,264 bytes already occupied by four FUN6 slots; the documented .pool baseline has 10,164 bytes free. A single 5,108-byte staging project is the only section-buffer design currently worth pursuing. The current song backup and autosave are each separate 3,816-byte objects; keeping them separate and growing both to 5,108 bytes, plus one staging object, adds 7,692 bytes net to the old .pool baseline, leaving only about 2,472 bytes before any other changes. Sharing the song backup/autosave storage could reduce that footprint, but their non-overlapping lifetimes have not yet been proven across every editor, import, reset and error path.

The larger unknown is the live audio model: five synth track_t objects instead of three add two complete synth tracks, including their voice state. The actual target compiler/linker map, stack high-water marks, and track_t/voice_t sizes must be measured before changing NPART/NTRK or allocating the staging record. The previous RAM audit is not a substitute for a System 37 target build.

No flash map, flash guard, linker placement, runtime track constants, live project application or audio callback has changed. The next safe runtime task is to isolate the legacy FUN6 wire reader from the new FUN7 live project type and build host tests around that boundary. Sequencer/arranger integration remains blocked until that runtime apply path and the memory budget are proven.

## Progress checkpoint — FUN6 wire format frozen in the legacy project layer (6 November 2026)

The legacy project.c structure definitions have been replaced with aliases to the explicit wire types in project_v7.h: project_t is now an alias of project_v6_t and proj_trk_t an alias of proj_trk_v6_t. The historical four-track, 24-lock FUN6 layout is therefore defined in one place and guarded by its existing compile-time size assertions (3,816 bytes total; 934 bytes per track). The legacy project code retains its existing field names, so this is intended as a no-behaviour-change refactor while removing its structural dependence on future NTRK/NLOCK changes.

This is not yet the full live-model split. The existing capture/apply routines still operate on the current four-track runtime and the legacy project representation. They must be replaced as a matched FUN7 capture/apply path before changing runtime constants. In particular, a future six-track runtime must never use the legacy FUN6 capture/apply functions as though they were its native project format.

The catalogue-backed prefetch service is now wired into the catalogue host test. That test checks both a missing-section failure (which must not publish READY) and a successful section load followed by a matching one-shot consume. The test runner compile line includes section_prefetch.c and section_prefetch_io.c. Neither this updated test nor the full host runner has yet been executed in this session; no target build or hardware test has been run.

## Progress checkpoint — six-track FUN7 live-project adapter (6 November 2026)

New files `firmware/src/project_v7_live.h` and `.c` define a host-testable boundary between the FUN7 wire record and a future six-track live sequencer model. The adapter does not depend on the current `NTRK`, `NPART` or `NLOCK` runtime array sizes. It uses explicit `PROJ_V7_TRACKS == 6` iteration and transfers each persistent `proj_trk_v7_t` record through caller-supplied callbacks.

Capture calls the global callback and each of the six track callbacks, then calculates and validates the FUN7 checksum. Apply validates the checksum, preflights globals and all six tracks before calling any mutating callback, then applies globals and all six tracks. The callback contract requires apply operations to be infallible after preflight; the board caller must exclude the audio ISR for the entire apply transaction. `all` preserves the existing distinction between full project loads and arrangement-section global handling; all six track records are applied in either mode.

`tests/project_v7_live_test.c` covers six-track capture, globals and track round-trip, full-project apply, section-mode apply, validation failure on track index 5 with zero mutations, unsupported engine rejection, bad checksum rejection, and a failed capture callback. It is wired into `tests/run_tests.sh`.

**Scope boundary:** this is the live-project adapter contract and host test, not yet the board-specific `track_t` callback implementation. The current runtime still has four `track_t` entries and 24 runtime locks. No `NTRK`, `NPART`, `NLOCK`, audio, UI, voice, linker, flash-map or live project apply changes have been made. The adapter test and full host runner have not yet been executed in this session, and no target build has been run. The next gate is to implement the board callbacks against the six-track runtime model, after the target memory audit and coordinated runtime-array migration. Do not connect `seq.c`/`arranger.c` until that board apply path exists.


## 9. Source-level runtime migration audit (6 November 2026)

The detailed dependency inventory is in `EXTENDED-FIRMWARE-RUNTIME-AUDIT.md`. It supersedes any implication that the FUN7 board callbacks can be implemented against the current runtime immediately.

The audit confirms that `NPART` controls synth-only voice allocation, engine arenas, modulation and audio render units, while `NTRK` controls sequenced-track state, MIDI bookkeeping, editor protocol and track UI. The dedicated drum render unit remains separate. The target is five synth parts plus one drum track; the two counts must never be collapsed into one loop bound.

A specific compatibility task remains before changing runtime constants: legacy `project.c` loops still refer to live `NTRK`, `NPART`, `NLOCK` and `TRK_DRUM` even though the FUN6 structures are now explicitly typed. Replace those dependencies with fixed FUN6 constants, including legacy drum index 3 and 24 locks. This prevents a future runtime count change from corrupting old project decoding.

A candidate RAM improvement is to move synth voice slots out of `track_t` into a dedicated synth-part pool. The current drum track contains an unused inline synth voice array while its actual voices live in `drums.v[]`. Keep this as a candidate until it is refactored with unchanged three-part behaviour and regression tests. The engine arena union, two additional synth parts, FUN7 staging and backup buffers must all be measured against the exact target linker map before runtime expansion.

No runtime constants, track layout, voice ownership, lock count, microtiming, audio path, linker placement or flash map have changed. The next implementation gate is FUN6 constant isolation and regression coverage, followed by target memory measurement.
