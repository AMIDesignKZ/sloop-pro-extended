# System 37: six tracks and eight song sections

## Target

This document defines the next System 37 expansion for the SMK-37 Pro build.

- **Six total tracks:** five independently sequenced synth tracks (tracks 1–5) and one dedicated drum track (track 6).
- **Two selectable synth engines:** Analog and FM6 remain available on each synth track. The drum track remains a dedicated drum instrument, not a sixth synth.
- **Eight independent sections:** A–H. Each section stores a complete six-track project snapshot, including patterns, sounds, track parameters and the project values already included in section snapshots.
- **Thirty-two arrangement positions:** a song can place any of A–H in each position, including repeated sections, with an individual bar length per position.
- **Track selection:** all six tracks must be addressable from the SMK-37 UI and the web editor. The existing four physical faders remain; they are banked controls, not a four-track limit. The selected track is the target for track-specific editing. The mixer exposes all six track levels and mute/solo states.

The expansion must work in the firmware, the SMK-37 display/control surface, the SysEx editor protocol, the web editor and the full-system emulator package.

## Existing implementation state

The extended-firmware branch already contains isolated groundwork:

- firmware/src/project_v7.h and project_v7.c: a FUN7 project image with six track records.
- firmware/src/project_v7_live.h and project_v7_live.c: host-testable capture/apply callbacks for six tracks.
- firmware/src/section_catalog.h and section_catalog.c: an eight-section catalogue and streaming overlay format.
- firmware/src/storage_multi.h and storage_multi.c: multi-sector A/B NOR storage primitives.

These components are not yet the active project/section implementation. The running firmware still has NPART == 3, NTRK == 4, four project slots, four arranger scenes, and a 16-position arrangement. The web editor and live protocol also still describe four tracks and sections A–D.

## Data model and compatibility

1. Make the six-track runtime explicit: synth tracks 0–4, drum track 5. Audit every fixed-size array, track loop, voice/engine arena, MIDI channel map, mute/solo/record mask and UI colour/index calculation that currently assumes three synths or four total tracks.
2. Keep the existing FUN6 layout frozen as a legacy import format. New project writes use the six-track FUN7 layout only after the runtime adapter and migration tests are complete.
3. FUN6 migration must preserve the user's three existing synth tracks and the existing drum track. The two additional synth tracks start empty with safe defaults. Do not silently discard legacy drum patterns, kit selection, drum parameters or other recoverable data.
4. Preserve parameter-lock data during migration where possible. Any unavoidable reduction must be explicit, tested and reported; it must not silently truncate valid locks.
5. Keep old projects readable. Validate and checksum every image before applying it. Preflight the entire project before mutating live audio state.
6. The eight-section catalogue is the authoritative A–H store. Existing four-slot project objects must be migrated or imported safely; do not erase or overwrite old data until the new catalogue has been validated and committed.
7. Audit the SMK-37 flash map before assigning physical sectors to the multi-sector A/B catalogue. The catalogue is about 43 KB with 24 locks per track in each FUN7 record and needs multiple sectors per copy. Do not assume the four currently spare sectors are sufficient, and do not touch stock firmware, update, calibration, BT, key-mac or sample regions.

## Song arrangement

- Raise the arrangement capacity from 16 to 32 entries.
- Each entry references section 0–7 (A–H) and has a bar count of 1–64.
- Preserve loop mode, song mode, start/stop behaviour, bar-accurate transitions and repeated sections.
- Update validation, defaults, erase/drop behaviour, persistence and all bit masks for eight sections.
- The device song page must let the user select and edit all eight sections and navigate a 32-entry arrangement. A compact scrolling window is acceptable; sections E–H must not be inaccessible.
- The web editor must show all eight section slots and all 32 arrangement positions, with section selection, duplication/store/load and repeated arrangement entries.

## Controls and editor

- Track selection must cover indices 0–5. Track-specific edit operations must always target the selected track; mixer operations must be able to address any track without changing selection.
- Keep the four hardware faders and their existing bank concept. Do not imply that four faders mean only four tracks.
- Update SAVE/GLO layer actions so A–H can be selected and stored without colliding with song mode, song record, chain or song-page actions. The final key/pad mapping must be documented and tested.
- Extend the editor protocol compatibly: retain existing command meanings, advertise the actual track/section/arrangement capacities, and add commands or object access for sections E–H and the 32-entry order. Update the web editor's parser, simulator, live view, backup/restore and help text together.
- Update emulator-visible firmware only through the normal SMK-37 build; do not special-case the emulator.

## Performance and acceptance tests

- Measure RAM, pool, flash use and target CPU cost with five synth parts. Keep the shared voice budget bounded; do not multiply the current voice budget by five without profiling.
- Test simultaneous notes, FM6 arena isolation, mixer routing, MIDI channel uniqueness, mute/solo/record masks, project capture/apply, autosave and warm-reset recovery.
- Test A–H save/load/erase, catalogue recovery from interrupted writes, 32-position playback, loops, repeats, invalid/empty sections and bar-boundary transitions.
- Test old FUN1–FUN6 project imports and old four-section settings/flash data.
- Run the full host test suite, target budget checks and SMK-37 firmware package build before treating the feature as ready for emulator or hardware testing.

## Implementation sequence

1. **Runtime and project model:** integrate the six-track runtime, FUN7 capture/apply and lossless legacy migration; add host tests.
2. **Audio and controls:** extend voice/engine allocation, mixing, MIDI, track selection, mute/solo/record and physical controls; profile target limits.
3. **Section storage:** integrate the eight-section catalogue with a verified SMK-37 flash map, legacy import and power-fail-safe migration.
4. **Arranger and device UI:** add 32 arrangement positions and A–H selection/store/load on the device.
5. **Editor protocol and web UI:** expose all six tracks, eight sections and 32 arrangement entries; update backup/restore and tests.
6. **Release validation:** run full tests, build the SMK-37 .fwsc, then test in the SMK-37 emulator before flashing hardware.
