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
