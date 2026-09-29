# Roadmap

Phases follow §80 of the master prompt. "Done" means the Definition of Done in §87, not just code present.

## Phase 0: Foundation ✅ (session 1)
Build system, module layout, logging, errors, command system and undo/redo, project creation, versioned project file, atomic IO, autosave/recovery, application shell.

## Phase 1: Media ◐
- ✅ Import, probe metadata, media pool list, search, offline detection, relink/replace source
- ☐ **Thumbnails and audio waveforms** with a regeneratable cache directory (§57)
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
**Phase 1 media essentials: thumbnails and audio waveforms**, with a safely deletable, regeneratable cache (§57):
1. A `cache` module with a content-addressed disk cache (key = media id + file size/mtime + parameters) and size limits.
2. A background job runner (`jobs` module: queue, priority, cancel) that generates poster frames and waveform peak files with the existing decoders.
3. Show them in the media pool (thumbnail view) and draw waveforms on audio clips in the timeline.
4. Tests: deterministic peaks from synthetic tones, cache invalidation when a file changes or is relinked, and cancellation.
