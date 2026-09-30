# Roadmap

Phases follow §80 of the master prompt. "Done" means the Definition of Done in §87, not just code present.

## Phase 0: Foundation ✅ (session 1)
Build system, module layout, logging, errors, command system and undo/redo, project creation, versioned project file, atomic IO, autosave/recovery, application shell.

## Phase 1: Media ◐
- ✅ Import, probe metadata, media pool list, search, offline detection, relink/replace source
- ✅ Thumbnails and audio waveforms, background jobs, regeneratable disk cache (session 3)
- ☐ Bins/sub-bins UI, ratings, keywords, markers, smart bins
- ✅ Source monitor with in/out marks and three-point editing (session 4)

## Phase 2: Timeline ◐
- ✅ Tracks, clips, playhead, selection, overwrite/insert/append, razor, lift/ripple delete, trim/ripple/roll/slip/slide/move, linked A/V, snapping
- ✅ Timeline in/out points and track targeting (session 4)
- ✅ Timeline and clip markers with navigation (session 5)
- ☐ Multi-clip selection (marquee, Ctrl-click), markers list panel, add/remove/rename tracks in the UI
- ✅ Copy/cut/paste (overwrite and insert), duplicate (session 5)
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
**Multi-clip selection and track management (last Phase 2 gaps):**
1. A selection model (set of clip ids) in the timeline: Ctrl/Shift-click, marquee drag, select-all-forward. Move, delete, copy and duplicate then act on the whole selection through the existing list-based session APIs.
2. Add, remove, rename and reorder tracks through `EditorSession` (undoable; targets fixed up; a track with clips needs confirmation).
3. Tests: selection semantics, multi-clip move/delete and their undo, track operations and target repair.
