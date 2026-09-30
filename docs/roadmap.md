# Roadmap

Phases follow §80 of the master prompt. "Done" means the Definition of Done in §87, not just code present.

## Phase 0: Foundation ✅ (session 1)
Build system, module layout, logging, errors, command system and undo/redo, project creation, versioned project file, atomic IO, autosave/recovery, application shell.

## Phase 1: Media ◐
- ✅ Import, probe metadata, media pool list, search, offline detection, relink/replace source
- ✅ Thumbnails and audio waveforms, background jobs, regeneratable disk cache (session 3)
- ☐ Bins/sub-bins UI, ratings, keywords, markers, smart bins
- ✅ Source monitor with in/out marks and three-point editing (session 4)

## Phase 2: Timeline ◐ (core editing complete; nesting outstanding)
- ✅ Tracks, clips, playhead, selection, overwrite/insert/append, razor, lift/ripple delete, trim/ripple/roll/slip/slide/move, linked A/V, snapping
- ✅ Timeline in/out points and track targeting (session 4)
- ✅ Timeline and clip markers with navigation (session 5)
- ✅ Multi-clip selection and group edits; track add/remove/rename/reorder (session 6)
- ☐ Markers list panel
- ✅ Copy/cut/paste (overwrite and insert), duplicate (session 5)
- ☐ Nested timelines / compound clips

## Phase 3: Video engine ◐
- ✅ CPU decode/compose/encode reference pipeline, aspect-correct fit
- ✅ Background frame prefetch during playback, synced to the master clock (session 2)
- ☐ Playback frame cache and a render cache that survive edits (§57)
- ✅ Transform/crop/opacity with multi-track blending (session 7)
- ✅ Keyframes (§30): linear/hold/ease, stored in source frames (session 7)
- ☐ Graph/curve editor, custom Bézier curves, expressions
- ☐ GPU abstraction (§56) with the CPU path kept as the golden reference

## Phase 4: Audio ◐
- ✅ Decode/resample, multi-track mix with gain/mute/solo, AAC export
- ✅ Audio playback in the viewer, audio as the master clock (session 2)
- ☐ Device selection, output latency compensation, and J/K/L shuttle and variable-speed playback
- ✅ Fades and crossfades (session 8); pan, clip volume/pan keyframes, mixer panel with meters, EQ/compressor/gain inserts (session 9)
- ☐ Buses, sends and returns; track automation lanes; limiter, gate, de-esser; loudness (LUFS) metering

## Phases 5–15
Colour (OCIO), multicam and text, VFX node graph, motion, advanced audio, AI (provider abstraction first), advanced VFX, delivery presets/DCP/IMF, collaboration, plugins/scripting, professionalization. These are unchanged from the master prompt. Each starts with its data model and interfaces, tests and an honest status entry in [feature-status.md](feature-status.md).

## Next recommended task
**Phase 5: colour, part 1: primary correction and scopes.**
1. A colour-correction model per clip: lift/gamma/gain/offset (RGB and master), contrast with pivot, saturation, temperature/tint. It is animatable like the transform and stored as a clip "grade" (format v7 with a migration).
2. A CPU grading pass in the compositor after decode and before transform, working in linear-light float with sRGB/Rec.709 transfer handling. This is the reference implementation for a later GPU path.
3. Scopes on the program monitor: waveform, RGB parade, vectorscope and histogram, computed from the rendered frame on a worker thread.
4. A Color workspace panel with wheels and sliders for the selected clip, plus copy/paste of grades.
5. Tests: golden values for each operation on synthetic ramps and colour patches, identity at defaults, scope histograms and vectorscope positions for known colours, persistence and migration.
