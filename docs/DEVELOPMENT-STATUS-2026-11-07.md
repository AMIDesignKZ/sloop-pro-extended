# System 37 development status — 7 November 2026

Branch reviewed: `extended-firmware`  
Reviewed HEAD: `0723b59b1e344379fbcadbfa29dbd13b1424d7d3`  
Rebrand baseline: `bdfd8a7ccee04cafa0258cf04f0a775b4b5b2bb2`  
The branch is 72 commits ahead of that baseline and not behind it.

This note consolidates the work from the 6 November SMK-37 session and the subsequent System 37 expansion. The earlier conversation title “SMK37 USB Detection Issues” is outdated: the USB detection issue was resolved and is not an open task.

## 1. Work completed earlier in the session

- Resolved the SMK-37 USB detection issue.
- Established a successful SMK-37 build on the pre-expansion baseline using `BOARD=smk37 bash build.sh`.
- That successful build predates the six-track/eight-section changes. It is only a baseline result, not a build verification of the current branch.
- The host test work was not a clean full pass: portability/sanitizer issues were encountered and one ASan/UBSan stress stage stopped before the full suite completed. Do not report the complete host suite as passing.
- System 37 rebrand work updated build/package naming, installer/launcher entry points, README/build/test documentation and Windows build scripts. The SMK hardware identity `SMK-37 Pro_900` and upstream Felucca identity/attribution were retained.
- The current repository is `AMIDesignKZ/system37`; the active development branch is `extended-firmware`.

## 2. System 37 target agreed

- Six total tracks: five independently sequenced synth tracks (each Analog or FM6) plus dedicated drum track 6.
- Eight independently stored sections A–H. Each is a complete six-track snapshot.
- 32 arrangement positions, each selecting A–H and holding 1–64 bars.
- Keep the four physical faders and four-column display layout; bank/scroll the visible controls so all six tracks remain accessible.
- Expose the full system in firmware, device UI, SysEx/editor protocol, web editor, backup/restore and emulator package.
- Preserve FUN1–FUN6 imports and existing drum patterns/kit/tune/decay data.

The detailed target remains in `docs/SYSTEM37-EXTENDED.md`.

## 3. Source work now present on the branch

### Project format and migration

- FUN7 format is defined for six tracks, with five synth parts and drum track 6.
- FUN6 remains a frozen legacy import format.
- Legacy drum track 4 (index 3) is moved to track 6 (index 5); new synth tracks 4 and 5 (indices 3 and 4) receive safe empty defaults.
- FUN7 retains 24 parameter locks per track.
- Six-bit signed microtiming is packed into 48 bytes per track and two-bit fill conditions are retained.
- FUN7 track record is asserted at 881 bytes; full project at 5,366 bytes.
- Host-testable FUN7 capture/apply adapter exists in `project_v7_live.c/.h`.
- Legacy drum tune/decay data is carried through the FUN7 drum record.

### Section catalogue and flash

- A/B multi-sector storage primitives exist in `storage_multi.c/.h`.
- A–H catalogue includes eight section records plus a working-project/autosave record.
- Catalogue payload is 48,378 bytes. Each 12-sector copy has 48,896 bytes of payload capacity, leaving 518 bytes.
- The map uses reserved free sectors plus the old project/autosave sectors for the second copy. The existing design preserves the legacy copy until the first new catalogue copy is committed and verified; later A/B rotation can reclaim the old sectors.
- The sample slots, FM6 bank, user preset banks, DAW settings, calibration/settings objects and stock-protected ranges are intended to remain untouched.
- Boot, legacy import, section load/store/erase, working autosave, restore and flush paths have been connected in `project.c`.
- PER3 four-section settings migration to PER4 32-position settings has been added.

### Runtime, controls and MIDI

- SMK-37 runtime constants now select `NPART=5`, `NTRK=6`, `TRK_DRUM=5`; FM-1 remains at three synth parts plus drums.
- Safe defaults were added for synth tracks 4 and 5.
- Track palette and drum colour indexing were extended.
- Mixer and studio track views use four visible rows with scrolling/banking.
- SMK-37 layer keys were extended for six mute/six solo states and A–H section play/store.
- Arranger capacity is 32 positions and eight sections on SMK-37; FM-1 remains 16 positions/four sections.
- MIDI routing now has separate channel IDs for five synth tracks. CH4/CH5 are persisted in the SMK-37 settings object (`OBJ_SMKSET`), not in the legacy MIDI settings byte block.

### Firmware editor protocol and tests

- Firmware track masks, solo/record masks and live-state fields were expanded for six tracks/eight sections.
- Backup object IDs 12–15 are used for sections E–H.
- FUN7 migration, live capture/apply, section catalogue and multi-sector storage host tests have been added or updated.
- The latest commit keeps the FM-1 song controls at their original four-section behaviour while retaining the System 37 A–H target.

## 4. Audit findings — current blockers

These are source-level findings from HEAD `0723b59`. No current-branch build or complete test run has verified them yet.

1. **Section-store call has the wrong argument order.**  
   In `firmware/src/project.c`, `s37_catalog_store_section()` calls `s37_cat_prepare_overlay()` with the section index where the replacement-project pointer belongs, and the project pointer in the final `default_section` position. The declaration in `section_catalog.h` expects `scratch, replacement, section, bars, default_section`. This will make the section-save path invalid and produces incompatible argument diagnostics.

2. **Arranger readiness checks the legacy format.**  
   `firmware/src/arranger_scene.c` uses `proj_ok()` in both `arrangement_ready()` and `section_bars()`. On System 37, `project_t` is FUN7, while `proj_ok()` validates a FUN6 image. Consequently FUN7 sections are not marked ready and the arranger will reject otherwise valid arrangements. These checks need the format-aware `project_valid()`.

3. **FUN1–FUN5 import can overwrite its own source buffer.**  
   In the SMK-37 paths in `project.c`, `proj_fetch()` and `autosave_resume()` call `proj_import(&proj_tmp.v6, &proj_tmp, n)`. The input and output overlap in the same union. FUN6's same-layout copy is harmless, but older conversion routines clear the destination before reading the source, corrupting FUN1–FUN5 input. Use a separate legacy-import staging object.

4. **Backup settings validation still assumes A–D.**  
   `settings_restore()` calls `arr_valid(&p.arrangement, 15u)`. This rejects valid System 37 arrangements that reference E–H. The old PER3 migration checks using mask 15 are intentional; the current PER4 backup-restore validation is not.

5. **Web LIVE parser still consumes four track records.**  
   Firmware `editor_live.c` emits one three-byte state record for every `NTRK` track (six on System 37). `web/editor.html`'s `CMD.LIVE` parser still loops exactly four times. It will misalign the rest of the LIVE reply on System 37.

6. **Web backup/restore does not accept the new E–H objects or FUN7.**  
   Firmware lists backup objects 12–15 for E–H. The web editor's `BK.RESTORE` list and `backupObjects()` whitelist still omit IDs 12–15. Its project-format detection also stops at FUN6 and its device-format check treats FUN7 as newer than the current device. A System 37 backup therefore cannot be reliably restored by the current web editor.

7. **Web Song page and protocol documentation remain old.**  
   `web/editor.html` and `web/EDITOR_PROTOCOL.md` still describe A–D and the 16-entry PER3 song order. The LIVE section UI is still four sections. The web editor has not yet been brought into line with the 8-section/32-position firmware target.

8. **Hardware MIDI channel controls for synth tracks 4 and 5 are not on a GLO page.**  
   `G_CH4` and `G_CH5` descriptors and persistence exist, but the `MIDI`, `MIDI 2` and `MIDI 3` page definitions do not expose them. They are available to protocol-level code but not through the current hardware parameter pages.

9. **Performance and final validation are outstanding.**  
   The shared voice/CPU/RAM budget has not been profiled for five synth parts. The host test suite, target budget check, SMK-37 package build, emulator and hardware have not been validated against this six-track/eight-section HEAD. The host test labels and target budget baseline still include old four-track assumptions.

## 5. Current status and safe next sequence

The branch contains substantial implementation work, but it is not release-ready and should not be flashed or treated as emulator-ready.

Recommended order:
1. Fix the section-catalogue call signature, FUN7 arranger validity checks, legacy-import staging alias and PER4 backup validation.
2. Audit all fixed track/section assumptions in firmware and test code; expose CH4/CH5 on the hardware UI.
3. Finish the web editor: six-track LIVE parsing/mixer, A–H controls, 32-entry song order, FUN7/E–H backup/restore and protocol documentation/tests.
4. Run the full host suite and resolve sanitizer/portability failures.
5. Build the SMK-37 package, check RAM/flash/CPU budgets, then test in the emulator before hardware.

Do not confuse the earlier successful pre-expansion SMK-37 build with validation of this branch.
