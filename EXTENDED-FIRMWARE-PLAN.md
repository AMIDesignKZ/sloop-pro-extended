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
- Before changing sample storage, choose between two 40 KiB slots (which would release two 40 KiB regions) and two 80 KiB slots (which would preserve total sample capacity but release no flash). No flash remapping has been made.

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
