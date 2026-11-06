# System 37 — functional specification

Target: M-VAVE SMK-37 Pro. System 37 is an alternative firmware based on SLOOP, not a backwards-compatible SLOOP firmware. This document defines the intended product; it is not a claim that the firmware is implemented or hardware-validated.

## 1. Track model

- Six independently sequenced tracks total.
- Tracks 1–5 are synth tracks. Each can independently select Analog or FM6.
- Track 6 is the dedicated drum track and is not assignable to a synth engine.
- Each synth part has eight voice-state slots, but the global synth allocator limits the number of simultaneously sounding synth voices across all five parts to eight. Drum voices are separate.
- Five synth tracks must be supported by the mixer, effects, modulation, sequencing, MIDI, project format, device UI and browser editor.
- Four physical faders remain. A two-layer track-mixer assignment is the intended control model; exact button mapping remains a control-map design task.

## 2. Synth engines

- The only selectable internal synth engines are Analog and FM6.
- Any of the five synth tracks may independently use either engine.
- System 37 uses its own compact engine IDs: Analog = 0, FM6 = 1. These IDs belong to the new System 37 project format; they do not preserve SLOOP's historical engine numbering.
- Analog is the subtractive engine for bass, leads and pads.
- FM6 is a six-operator DX7-style FM engine with 32 algorithms.
- Other SLOOP synth engines are out of scope and should not be included in the System 37 selectable engine table or per-part engine-state allocation.
- The incoming external FM-1 is a separate future MIDI instrument and is not part of the SMK-37 internal engine or RAM budget.

## 3. FM6 patch library

- Four complete user banks, named A–D.
- 32 patches per bank; 128 user patches total.
- Each patch uses the standard 128-byte DX7 voice representation.
- Import and export one standard 32-voice DX7 SysEx bank at a time into/from a selected user bank.
- Existing FM6 patch-bank data may be retained where its format is directly compatible; this does not imply compatibility with SLOOP project files.
- Factory patches remain available.
- Bank storage must be versioned and A/B protected. The current 27-slot single-sector object is not the target format.

## 4. Drums and samples

- One dedicated drum track.
- Retain the existing synthesised drum engine and kit library, explicitly including the 808 and 909 kits, as well as 606, 80s, vintage, techno and other electronic kits.
- Retain the four editable SYN1–SYN4 user synth kits.
- Retain sampled drum playback and one user drum kit per retained user sample slot. Drum sample playback is independent of the selectable synth-engine list.
- Two user sample slots, USR1 and USR2, each 40 KiB.
- Retire USR3, USR4 and the USR3+4 paired kit from the new user interface.
- Preserve the current sample encoding and upload semantics unless a measured issue requires a separately approved change.
- Before reclaiming the old USR3/USR4 sectors, inspect existing user data and provide an export/confirmation path. Do not silently overwrite user samples.

## 5. Sequencer and song structure

- 64 steps per track.
- Eight editable sections A–H. Each section stores all six tracks and project-level musical state.
- Preserve the existing 16-entry arrangement chain. Each entry may reference any section A–H and retain its bar duration and loop behaviour.
- 12 parameter locks per track.
- Remove per-step micro-timing/nudge.
- Retain fill conditions, step notes/velocity, drum lanes, ratchets, swing and other existing sequencer functions unless a specific design issue is found.

## 6. Mixing and MIDI

- Four physical faders remain and all six tracks must have accessible level control.
- Use a deliberate fader-layer model rather than making two tracks inaccessible. The precise layer/button assignment remains a design gate.
- Extend per-synth-track MIDI channel configuration to five channels. Drum channel and global FX channel behaviour remain separate.
- Update MIDI input/output routing, DAW mode, CC mapping, recording, track selection, solo/mute and editor controls for six tracks.

## 7. Persistence

- System 37 has its own versioned project format storing six tracks, 12 locks and fill conditions without micro-timing.
- SLOOP FUN1–FUN6 project files are not a supported import format. No legacy engine mapper or old-project compatibility layer is required.
- Eight sections are stored in a versioned A/B section catalogue.
- Autosave remains supported and receives a two-sector-per-copy allocation.
- FM6 receives four-bank versioned A/B storage.
- New sample backups contain only USR1 and USR2. Whether old four-slot sample backups need a limited USR1/USR2 extraction path is a separate data-retention decision, not project compatibility.
- Keep existing V16 recovery route and protected stock/bootloader/calibration regions untouched.

## 8. Non-goals

- No internal FM-1 engine.
- No backwards compatibility with SLOOP project files or historical synth-engine IDs.
- No increase beyond 64-step patterns or 16 arrangement entries.
- No eight-bank FM6 library.
- No per-step micro-timing.
- No weakening of A/B power-loss protection to save flash.
- No firmware installation or hardware change is authorised by this specification alone.
