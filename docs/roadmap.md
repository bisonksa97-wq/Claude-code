# Roadmap

Phases follow §80 of the master prompt. "Done" means the Definition of Done in §87, not just code present.

## Phase 0: Foundation ✅ (session 1)
Build system, module layout, logging, errors, command system and undo/redo, project creation, versioned project file, atomic IO, autosave/recovery, application shell.

## Phase 1: Media ◐
- ✅ Import, probe metadata, media pool list, search, offline detection, relink/replace source
- ✅ Thumbnails and audio waveforms, background jobs, regeneratable disk cache (session 3)
- ☐ Bins/sub-bins UI, ratings, keywords, markers, smart bins
- ☐ Source viewer with in/out marks → three-point editing

## Phase 2: Timeline ◐
- ✅ Tracks, clips, playhead, selection, overwrite/insert/append, razor, lift/ripple delete, trim/ripple/roll/slip/slide/move, linked A/V, snapping
- ☐ In/out points, markers, track targeting, add/remove/rename tracks in the UI
- ☐ Copy/paste, duplicate
- ☐ Nested timelines / compound clips

## Phase 3: Video engine ◐
- ✅ CPU decode/compose/encode reference pipeline, aspect-correct fit
- ✅ Background frame prefetch during playback, synced to the master clock (session 2)
- ☐ Playback frame cache and a render cache that survive edits (§57)
- ☐ Transform/crop/opacity with blending (tracks composited, not just top-most)
- ☐ Keyframes (§30) as a shared parameter system
- ☐ GPU abstraction (§56) with the CPU path kept as the golden reference

## Phase 4: Audio ◐
- ✅ Decode/resample, multi-track mix with gain/mute/solo, AAC export
- ✅ Audio playback in the viewer, audio as the master clock (session 2)
- ☐ Device selection, output latency compensation, and J/K/L shuttle and variable-speed playback
- ☐ Pan, fades, clip volume keyframes, mixer panel, EQ/compressor processors

## Phases 5–15
Colour (OCIO), multicam and text, VFX node graph, motion, advanced audio, AI (provider abstraction first), advanced VFX, delivery presets/DCP/IMF, collaboration, plugins/scripting, professionalization. These are unchanged from the master prompt. Each starts with its data model and interfaces, tests and an honest status entry in [feature-status.md](feature-status.md).

## Next recommended task
**Source viewer and three-point editing (Phase 1/2):**
1. Source monitor that loads a media item (double-click in the media pool), with its own transport and in/out marks stored on the media item (undoable).
2. Timeline in/out points and track targeting (source patching) in the model and UI.
3. Insert (`,`) and overwrite (`.`) edits using source in/out and the playhead or timeline in/out. Reuses `placeMedia` with a source range.
4. Tests: three-point edit math (every combination of marks), undo, persistence of marks in `.uproj` (a format-version bump with a migration test).
