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

Old projects can contain up to 24 locks per track. Format 7 can retain only 12. Before implementation, choose and test a deterministic policy (recommended: retain the earliest-step valid locks, with stable parameter/order tie-breaking) and expose a migration warning if any locks are dropped. Do not silently claim lossless conversion.

### Section catalogue

Do not allocate eight independent A/B objects. Store all eight format-7 projects in one catalogue payload:

- Project records: 8 × 5,108 = 40,864 bytes.
- Catalogue header/directory allowance: approximately 80 bytes.
- Modelled payload: approximately 40,944 bytes.
- Eleven sectors per copy: 44,800 bytes available after the first-sector 256-byte header area.
- A/B catalogue: 22 sectors total.

The catalogue directory should include format version, section-presence bits and compact metadata needed by the arranger (for example, cached section bar lengths). Project records retain their own magic/checksum; the outer storage record has its own length and CRC.

Saving one section is a whole-catalogue copy-on-write transaction: read the current valid catalogue, replace the selected section in the inactive copy, write all payload bytes, verify the complete payload CRC, then program the commit header last. The old copy remains untouched until the new one is committed and read back. This costs more writes per section save but gives a single atomic A/B snapshot for A–H.

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
