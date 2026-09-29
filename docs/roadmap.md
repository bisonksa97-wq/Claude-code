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
- ☐ Background frame scheduling and prefetch for smooth playback (decode off the UI thread)
- ☐ Transform/crop/opacity with blending (tracks composited, not just top-most)
- ☐ Keyframes (§30) as a shared parameter system
- ☐ GPU abstraction (§56) with the CPU path kept as the golden reference

## Phase 4: Audio ◐
- ✅ Decode/resample, multi-track mix with gain/mute/solo, AAC export
- ☐ **Audio playback in the viewer (A/V sync, audio as the master clock)**: the most visible gap in the slice
- ☐ Pan, fades, clip volume keyframes, mixer panel, EQ/compressor processors

## Phases 5–15
Colour (OCIO), multicam and text, VFX node graph, motion, advanced audio, AI (provider abstraction first), advanced VFX, delivery presets/DCP/IMF, collaboration, plugins/scripting, professionalization. These are unchanged from the master prompt. Each starts with its data model and interfaces, tests and an honest status entry in [feature-status.md](feature-status.md).

## Next recommended task
**Audio playback with A/V sync, plus off-thread frame prefetch in the viewer.** Together they finish "PLAY VIDEO" in the vertical slice:
1. Add an `AudioOutput` interface in a new `playback` module with a Qt Multimedia implementation (`QAudioSink`) behind it.
2. Drive playback from the audio clock. The video frame scheduler renders ahead on a worker thread into a small ring buffer.
3. Tests: a deterministic clock and scheduler unit test, and an integration test that the mixer feed stays sample-continuous across seeks.
