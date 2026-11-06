# SLOOP Pro Extended — functional specification

Target: M-VAVE SMK-37 Pro. This document defines the agreed product target. It is not a claim that the firmware has been implemented or hardware-validated.

## 1. Track model

- Six independently sequenced tracks total.
- Tracks 1–5 are synth tracks. Each can independently select Analog or FM6.
- Track 6 is the dedicated drum track. It is not assignable to a synth engine.
- Each synth part retains its own eight voice slots, but the existing global synth voice allocator limits the number of simultaneously sounding synth voices across all five parts to eight. Drum voices are separate.
- Five synth tracks must be supported by the mixer, effects, modulation, sequencing, MIDI, project format, device UI and browser editor.
- Four physical faders remain. A two-layer fader assignment is the intended control model; exact button mapping is to be finalised after auditing the SMK-37 control map.

## 2. Synth engines

- The only selectable synth engines are Analog and FM6.
- Any of the five synth tracks may independently use either engine.
- Analog is the primary subtractive engine for bass, leads and pads.
- FM6 is a six-operator DX7-style FM engine with 32 algorithms.
- Persistent engine IDs must never be renumbered. Legacy projects and presets using engines no longer selectable must load through an explicit compatibility mapper, defaulting unsupported sounds to Analog while retaining sequence, mix and common parameters.
- The incoming external FM-1 is a separate future MIDI instrument and is not part of the SMK-37 internal engine or RAM budget.

## 3. FM6 patch library

- Four complete user banks, named A–D.
- 32 patches per bank; 128 user patches total.
- Each patch uses the standard 128-byte DX7 voice representation.
- Import and export one standard 32-voice DX7 SysEx bank at a time into/from a selected user bank.
- Existing 27 stored patches migrate to Bank A positions 1–27. Bank A positions 28–32 and Banks B–D start empty.
- Factory patches remain available.
- Bank storage must be versioned and A/B protected. The current 27-slot single-sector object is not the target format.

## 4. Drums and samples

- One dedicated drum track.
- Retain the existing synthesised drum engine and kit library, including 808, 909, 606, 80s, vintage, techno and other electronic kits.
- Retain the four editable SYN1–SYN4 user synth kits.
- Retain sampled drum playback and one user drum kit per retained user sample slot.
- Keep persistent drum-kit IDs stable. Reserve the old USR3, USR4 and USR3+4 IDs; keep SYN1–SYN4 at their existing IDs. Legacy references to retired user kits must map to a safe available kit rather than being reinterpreted as a different kit.
- Two user sample slots, USR1 and USR2, each 40 KiB.
- Retire USR3, USR4 and the USR3+4 paired kit from the new user interface.
- Preserve the current sample encoding and upload semantics unless a measured issue requires a separately approved change.
- Before reclaiming the old USR3/USR4 sectors, detect legacy data and provide an export/confirmation path. Do not silently overwrite user samples.

## 5. Sequencer and song structure

- 64 steps per track.
- Eight editable sections A–H. Each section stores all six tracks and the project-level musical state.
- Preserve the existing 16-entry arrangement chain. Each entry may reference any section A–H and retain its bar duration and loop behaviour.
- 12 parameter locks per track.
- Remove per-step micro-timing/nudge.
- Retain fill conditions, step notes/velocity, drum lanes, ratchets, swing and the other existing sequencer functions unless a specific compatibility issue is found.
- Format migration must preserve fill conditions while discarding old micro-timing. If an old track has more than 12 valid locks, migration must apply a deterministic documented truncation policy and report the loss where the UI permits.

## 6. Mixing and MIDI

- Four physical faders remain and all six tracks must have accessible level control.
- Use a deliberate fader-layer model rather than making two tracks inaccessible. The precise layer button/assignment is a control-map design gate.
- Extend per-synth-track MIDI channel configuration from three to five channels, preserving the meaning of existing settings. Drum channel and global FX channel behaviour remain separate.
- Update MIDI input/output routing, DAW mode, CC mapping, recording, track selection, solo/mute and editor controls for six tracks.

## 7. Persistence and compatibility

- New project format stores six tracks, 12 locks and fill conditions without micro-timing.
- Existing format-6 projects migrate: old synth tracks 1–3 map to new synth tracks 1–3; new synth tracks 4–5 receive defaults; the old drum track maps to new track 6.
- Eight sections are stored in a versioned A/B section catalogue.
- Autosave remains supported and receives a two-sector-per-copy allocation.
- FM6 receives four-bank versioned A/B storage.
- Legacy four-slot sample backups remain readable: restore USR1 and USR2 only; ignore retired slots. New backups contain only USR1 and USR2.
- Keep existing V16 recovery route and protected stock/bootloader/calibration regions untouched.

## 8. Non-goals

- No internal FM-1 engine.
- No increase to 64-step patterns or 16 arrangement entries.
- No eight-bank FM6 library.
- No per-step micro-timing.
- No weakening of A/B power-loss protection to save flash.
- No firmware build, installation or hardware change is authorised by this specification alone.
