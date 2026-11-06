# SLOOP Pro Extended — implementation plan

Target hardware: M-VAVE SMK-37.

## Requested result

- Five independently sequenced synth parts plus the existing dedicated drum part (six tracks total).
- Eight independently editable song sections, A–H.
- Preserve the existing 16-entry arrangement chain; each entry can reference any of the eight sections.
- Retain the four physical faders and provide track selection/mixing for all six tracks.
- Keep the stock firmware and the existing V16 recovery route untouched.

## Baseline audit

- The current core defines `NPART=3`, `NTRK=4`, and `TRK_DRUM=3`. The synth-part count is used throughout the audio engine, sequencing, effects, modulation, and UI; this is not a local constant change.
- The arranger has 16 chain entries but only four scenes (`ARR_STEPS=16`, `ARR_SCENES=4`).
- Project sections are currently four slots (A–D), with hard-coded four-slot masks and bounds in project persistence, restore, section storage, and the song UI.
- The current project payload is effectively at its 3,840-byte limit. Adding two tracks directly to the existing project structure is not safe without a storage-format redesign.
- Flash object mapping allocates four project objects. The SMK-37 flash layout also contains user presets, autosave, FM bank, settings, owner metadata, and reserved/stock regions. These must be mapped explicitly before allocating additional project storage.
- The browser editor contains independent assumptions for four tracks and four sections. Firmware and editor formats must remain aligned.


## Accepted design preferences (6 November 2026)

- Retain **two** user sample slots rather than four. The SMK-37 implementation currently allocates four 40 KiB slots in the separate USRFLASH range; reducing this to two is an accepted trade-off, subject to confirming which slots are retained and remapping the released sectors safely.
- Do not assume the released sample flash automatically solves project storage. It is a separate flash window; any reassignment must update the flash allow-list, storage map, upload/editor protocol, and recovery/stock-firmware safeguards together.
- First engine candidate to make optional/remove if measured RAM pressure requires it: **PHYS** (physical modelling), because its per-part engine arena can be substantial. Do not remove it pre-emptively; measure the compiled RAM/CPU impact of five synth parts first.
- DRAWBAR is a secondary candidate only if firmware code/flash size becomes a constraint and the user accepts losing tonewheel organ sounds. Preserve engine indices as reserved values if any engine is disabled or removed; never renumber stored engine IDs.
- Retain ANALOG, DIGITAL, PHASE, LOFI, SAMPLE, FORMANT, TRIO, GRAIN, FM6, and NOISE by default.
- Sequencer direction: 12 parameter locks per track; remove per-step micro-timing/nudge; retain fill conditions; keep 64-step patterns and the existing 16-entry arrangement chain. Eight sections A–H remain the target.

These are design decisions and constraints, not implemented firmware changes.


## Accepted instrument architecture (6 November 2026)

- The five synth parts are **freely assignable**: each part can select either FM6 or Analog independently. Do not reserve fixed tracks for either engine.
- The sixth track remains the dedicated drum track, independent of the synth engine selection.
- Prioritise FM6 as a DX7-style six-operator engine, including full standard 32-voice SysEx bank import/export and a bank library design that can retain complete 32-patch banks. Current implementation supports standard 32-voice bank parsing but only 27 persistent user slots; expansion is a design requirement, not an existing capability.
- Prioritise Analog as the main subtractive engine for bass, leads and pads.
- Retain sample playback support for sampled drum kits and two user sample slots. Removing the general SAMPLE synth engine must not remove drum sample playback or user drum-kit functionality.
- Drum direction: retain the dedicated synthesised drum engine and organise/refine kits around 808, 909, hybrid and experimental/electronic characters, after auditing existing SYN1–SYN4 kits. Retain sampled kits.
- Proposed engine simplification: remove or make optional DIGITAL, PHASE, LOFI, FORMANT/VOICE, TRIO, DRAWBAR, GRAIN, PHYS and NOISE, subject to dependency and memory/flash measurement. FM6, ANALOG, drum synthesis and sample playback remain.
- Preserve old engine numeric IDs as reserved/disabled values; never renumber IDs stored in existing projects or presets.
- The incoming external FM-1 is outside this SMK-37 firmware's internal engine budget; consider it later as a separate MIDI instrument.

These are accepted product-direction decisions and proposed scope. No firmware source has been changed to implement them.

## Implementation phases

1. **Complete dependency and storage audit.** Trace every use of `NPART`, `NTRK`, drum indices, section masks, project slot counts, project format versions, backup/restore paths, MIDI channel settings, mixer/fader routing, and editor serialization.
2. **Design the data model and flash layout.** Decide how six-track projects and eight sections are represented without exceeding the existing payload limit or overwriting any reserved SMK-37 flash region. Define migration and compatibility behaviour before changing structures.
3. **Expand the audio and sequencing engine.** Add two synth parts across voice allocation, engine state, modulation, effects, mixing, MIDI routing, and real-time scheduling. Check CPU and RAM budgets on the SMK-37.
4. **Expand project and arrangement handling.** Implement A–H storage, validation, save/load, autosave, backup/restore, and all 16 arrangement references. Add explicit format/version handling.
5. **Update the instrument UI and browser editor.** Add selection and level control for six tracks while retaining four physical faders; expose all eight sections and keep editor import/export compatible.
6. **Build and validate.** Compile for the exact SMK-37 target, run static checks, inspect generated firmware size, and test project migration and recovery. Hardware testing must start with a recoverable test build and a verified stock/V16 recovery path.

## Safety constraints

- Do not alter stock firmware images, bootloader, calibration, protected flash areas, or the user's V16 recovery files.
- Do not assume spare flash sectors are safe until the complete linker, partition, and storage map has been checked against the target board.
- Do not increase track or section constants globally until all dependent arrays, masks, UI bounds, MIDI settings, and persistence code have been updated.
- Preserve a known-good baseline and make changes on the `extended-firmware` branch only.
- A build is not considered hardware-safe merely because it compiles; verify memory use, flash writes, project round-trips, and recovery first.

## Current status

This is an implementation plan and audit checkpoint, not a completed firmware modification. No firmware source has been changed by this plan.


## Verified source audit checkpoint (6 November 2026)

The following details have now been checked against the current source, rather than inferred from the earlier plan:

### Drum architecture and kits

- The dedicated drum track already has a substantial synthesis engine in `drum_synth.c`. It synthesises tones, pitch drops, transient clicks, white/metal/cymbal/chip noise, clap bursts, resonant filtering, drive and sample-rate/bit-depth crushing. It is not dependent on the general SAMPLE synth engine.
- The generated drum-kit source currently defines **32 synthesised kits**, including named `808`, `909`, `606`, `80S`, `VINTAGE`, `TECHNO`, `ELECTRO`, `SYNTHWV`, `CHIP`, `INDUSTR` and others. This already provides the requested 808 and 909 starting points, plus broad electronic coverage.
- The drum UI also exposes four additional editable `SYN1`–`SYN4` kits through a separate `dsu_kit` path. The host test confirms their initial contents are copies of 808, 909, TRAP and TECHNO respectively; they can be edited in the web editor and are retained as user kits.
- Sampled drum kits and user-sample drum kits are handled separately in `drums.c`. Any removal of the selectable SAMPLE synth engine must leave this drum sample playback path intact.

### FM6 bank capacity

- FM6 is already a six-operator engine with 32 algorithms and standard DX7 128-byte voice packing/unpacking.
- The standard 32-voice SysEx bank parser is not the limiting factor. The current persistent bank is limited to **27 user patches** because `FM6_BANK_N=27`, and its A/B storage object is 3,472 bytes.
- One complete 32-patch bank requires 4,096 bytes of patch records alone, before metadata. It cannot fit in the current 3,840-byte storage-object payload.
- The current editor labels patch slots B1–B27. The bank format, editor protocol, storage object allocation and backup/restore object numbering must all change together for multi-bank storage.
- The requested number of complete banks is not yet fixed. Eight banks (256 patches) remains a possible target, not a confirmed flash allocation.

### Track, project and memory constraints

- The current source confirms `NPART=3`, `NTRK=4`, `TRK_DRUM=3`, `NLOCK=24`, and 64 steps. The project still stores a 6-bit micro-timing value plus a 2-bit fill condition for every step.
- This differs from the accepted extended design of 12 locks and no micro-timing. Those reductions have not yet been implemented.
- The current project structure is near the 3,840-byte object limit, and project sections are held in a four-element `.noinit` array. The linker reserves only 0x3D50 bytes for `.noinit`; simply doubling the section array is not viable. Eight sections need a deliberate RAM placement redesign as well as flash storage changes.
- The arranger is already 16 entries, but `ARR_SCENES=4`; scene validation and UI are still hard-coded to A–D.
- The four synth engines/parts and drum track are referenced across audio, FX, MIDI, project migration, UI and the browser editor. No global track-count change is safe until all of these references are audited.

### Sample-slot map discrepancy to resolve before flash work

- `eng_sample.c` currently defines four 40 KiB user slots at 0xC2000, 0xCC000, 0xD6000 and 0xE0000 (160 KiB total).
- The SMK-37 flash-map comments describe the same 0xC2000–0xE9FFF region as two 80 KiB slots. The implementation and documentation therefore describe different logical layouts over the same physical range.
- The user has since confirmed two 40 KiB slots. The intended retained positions are 0xC2000 and 0xCC000; 0xD6000 and 0xE0000 are candidates to retire. The 80 KiB released range is not yet allocated. No flash remapping has been made.

### Immediate implementation order

1. Finish the source-level dependency audit, including `SYN1`–`SYN4`, all engine IDs, RAM section placement, editor protocol and flash guard call sites.
2. Add host-side size/layout checks and tests for the proposed project and FM6 bank formats before changing persistent structures.
3. Design the six-track project representation and eight-section RAM/flash storage together. Keep the existing format readers for migration.
4. Implement FM6 multi-bank storage as a versioned format with a safe A/B commit strategy; do not simply enlarge the current object.
5. Only then begin changing the runtime track count and browser/device UI.

No firmware source, flash address, stock region, recovery path or installed device has been changed by this audit.


### User decision: sample capacity

On 6 November 2026 the user confirmed **two user sample slots of 40 KiB each**.

- Keep the first two existing slot positions as the intended user-facing slots: USR1 at 0xC2000 and USR2 at 0xCC000.
- Retire USR3 at 0xD6000 and USR4 at 0xE0000 from the user-sample interface.
- This potentially releases 80 KiB (0xD6000–0xE9FFF) for a future storage redesign. It is not yet reassigned to projects or FM6: the flash map, loader allow-list, stock marker handling, backup/restore and all editor references must be reconciled first.
- Keep the existing 40 KiB per-slot capacity and sample encoding. Do not silently convert either slot to 80 KiB.
- The current flash-map comments incorrectly describe two 80 KiB slots, while the implementation has four 40 KiB slots. Update those comments only as part of the coordinated flash-map change, not as an isolated documentation-only claim.

### Storage capacity model (6 November 2026)

This is a source-derived capacity model for the accepted six-track / eight-section design. It is a planning calculation, not a flash-layout implementation.

#### Six-track project payload

The current format-6 project is 3,816 bytes with four tracks. Its stored track structure contains:

- 64 steps × 10 bytes = 640 bytes.
- 64 bytes of per-step micro-timing/fill flags.
- 24 parameter locks × 4 bytes = 96 bytes.
- Existing parameter and engine fields.

The project has 76 bytes before its track array and a 4-byte checksum after it. The current per-track record is therefore 934 bytes (3,816 − 80, divided by four).

The accepted changes remove the 64-byte micro-timing/fill field and reduce locks from 24 to 12 (48 bytes). Retaining the existing step and parameter representation gives a modelled track record of 822 bytes. Six tracks therefore produce:

- 76-byte project header + (6 × 822-byte track records) + 4-byte checksum
- **5,012 bytes per section**, before any future fields or compiler/layout changes.

This is a direct structure-size model; it must be confirmed with a target/host compiler sizeof assertion before implementation. It is already larger than the current 3,840-byte one-sector payload.

A two-sector copy provides 8,192 − 256 = 7,936 payload bytes, so a 5,012-byte project can fit in two sectors per copy. With A/B safety, each section would occupy four sectors. Eight sections would occupy 32 sectors (128 KiB), versus the current four one-sector A/B projects occupying eight sectors (32 KiB): **24 additional sectors / 96 KiB**.

#### FM6 bank storage

A standard DX7 bank contains 32 × 128-byte patch records = 4,096 bytes. Eight complete banks require 32,768 bytes before directory, version, checksums or metadata.

The current FM6 object is 3,472 bytes and uses one sector per A/B copy. A versioned eight-bank object would need nine sectors per copy (36,864 bytes of sector space, 36,608 bytes available after the first-sector header offset). That is 18 sectors total, or **16 additional sectors / 64 KiB** over the current two-sector A/B allocation.

The eight-section project expansion plus eight FM6 banks therefore needs an estimated **40 additional sectors / 160 KiB**, before extra metadata or future growth.

#### Candidate free/released SMK-37 sectors

The current source map identifies these candidate areas, all of which still require coordinated allow-list, erase-guard, recovery and stock-firmware review:

- Retiring USR3 and USR4: 0xD6000–0xE9FFF = 20 sectors / 80 KiB.
- Existing SLOOP spare area: 0xB7000–0xBAFFF = 4 sectors / 16 KiB.
- Unlisted free area: 0xEB000–0xF3FFF = 9 sectors / 36 KiB.
- Unlisted free sector: 0xFE000–0xFEFFF = 1 sector / 4 KiB.

Total candidate capacity: **34 sectors / 136 KiB**. This is six sectors / 24 KiB short of the 40-sector estimate for eight sections plus eight FM6 banks. The 0xEA000 stock-marker sector and all protected stock/resource, OTA, BTIF, calibration, key_mac and recovery regions remain excluded.

The 34 candidate sectors leave 10 additional sectors for FM6 after the estimated 24-sector project expansion. The existing FM6 A/B pair already occupies two sectors, so five complete banks (20,480 bytes of patch records) would require six sectors per copy: 12 sectors total, or 10 additional sectors. Five banks are therefore the theoretical maximum under this allocation, with no spare sectors for growth. Four banks would leave two candidate sectors; six banks would exceed the allocation by four sectors. These are capacity ceilings before metadata and safety margin. The eight-bank target cannot currently be promised alongside eight sections using only the identified sectors. Do not weaken A/B power-loss protection to force a fit.

#### RAM consequence

The current four-project array occupies almost all of the 0x3D50-byte .noinit region. Eight 5,012-byte project objects would require about 40 KiB and cannot simply be placed in .noinit. The linker provides a separate 0x54000-byte .pool, but its actual remaining headroom has not yet been measured. The storage design must therefore include a RAM placement/cache strategy and a target linker-map check; simply changing proj_slot[4] to [8] is unsafe.

#### Next design gate

Before any source or flash-map change:

1. Add host compile-time size assertions for the proposed compact six-track project format and the multi-sector A/B object.
2. Measure .pool, .bss, .data, XIP and RAMTEXT headroom from an actual SMK-37 build/map.
3. Decide how many complete FM6 banks fit after the project allocation and required metadata.
4. Specify multi-sector A/B write/verify/rollback semantics and migration from current one-sector objects.
5. Reconcile the sample-slot count with the stock-mapping detector, sample upload, drum sample references, backup/restore, browser editor and flash guards.

No source code, flash address, sample slot or storage allocation has been changed by this capacity model.

### Two-slot sample dependency audit (6 November 2026)

The two-slot decision affects more than the sample-slot constant:

- eng_sample.c uses SMP_USER_SLOTS=4 for zone arrays, scan state, names and slot addressing. Its SMK-37 addresses are 0xC2000, 0xCC000, 0xD6000 and 0xE0000. The encoded slot decoder also assumes four slots.
- drums.c exposes four single-slot user drum kits plus a special USR3+4 paired kit that reads slot indexes 2 and 3. With two slots, retain one user drum kit per slot (USR1 and USR2) and retire the USR3/USR4 entries and paired-kit path. Sampled drum playback and user drum kits in the two retained slots remain supported.
- The browser editor hard-codes four sample records, backup object IDs 32–35, four sample selectors in SAMPLE/GRAIN engine descriptions, and the USR3+4 kit split/upload workflow. Its backup UI and user-facing text also describe four slots. These require a coordinated protocol and editor update.
- New backups should contain only USR1 and USR2. Restore must continue to read legacy four-slot backups, restore the first two slots, and ignore retired slots rather than writing into reassigned storage.
- Keep legacy stock-mapping detection aware of old sample headers while old data may exist. Preserve the stock marker release before the first erase/write in the USRFLASH region. The 0xEA000 marker sector remains protected.

No sample-slot constant, drum-kit index, backup object, browser-editor path or flash guard has been changed yet.
