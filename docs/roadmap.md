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

## Phase 5: Colour ◐
- ✅ Primary correction per clip (lift/gamma/gain/offset, contrast/pivot, saturation, exposure, white balance), keyframable, format v7 (session 10)
- ✅ Color panel with balance wheels, copy/paste/reset grade; scopes (waveform, parade, vectorscope, histogram); CLI `grade`/`scopes` (session 10)
- ☐ Curves (custom RGB/luma, hue-vs-hue/sat/lum), 3D LUT import (.cube), grade versions and bypass
- ☐ Colour management: OCIO/ACES input/working/output transforms, float working space, HDR
- ☐ Qualifiers, power windows, tracking, node graph; scopes on a worker thread / GPU

## Phases 6–15
Multicam and text, VFX node graph, motion, advanced audio, AI (provider abstraction first), advanced VFX, delivery presets/DCP/IMF, collaboration, plugins/scripting, professionalization. These are unchanged from the master prompt. Each starts with its data model and interfaces, tests and an honest status entry in [feature-status.md](feature-status.md).

## Next recommended task
**Phase 5: colour, part 2: curves, LUTs and a float grading path.**
1. Move grading to a float (32-bit linear) intermediate per layer, so heavy grades no longer band, keeping 8-bit output; golden tests before/after.
2. Custom curves per clip (master/R/G/B splines, hue-vs-sat, hue-vs-hue, lum-vs-sat), keyframable as a whole, stored in the grade (format v8 with a migration).
3. 3D LUT support: parse `.cube` files (validation, readable errors), trilinear/tetrahedral interpolation, a per-clip LUT slot and a timeline output LUT; LUT files referenced like media (relinkable).
4. Grade bypass per clip and for the whole timeline, and A/B grade versions.
5. UI: a curves editor in the Color panel and a LUT picker; CLI `grade curve` / `grade lut`.
6. Tests: curve evaluation and monotonicity, `.cube` parsing (good and malformed files), LUT identity and known transforms, bypass, persistence and migration.
