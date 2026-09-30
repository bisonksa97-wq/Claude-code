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
- ✅ Curves (master/RGB, hue vs hue/sat, lum vs sat) with an editor, `.cube` LUTs per clip and on the output, relinking, bypass (clip and timeline) and grade versions, format v8 (session 11)
- ✅ Colour management: media/timeline/output colour spaces (incl. PQ, HLG, LogC3, S-Log3), float working pipeline, tagged exports, correct YUV matrices, viewer clipping/false-colour overlays, format v9 (session 12)
- ☐ OCIO/ACES configs, tone and gamut mapping, an HDR display path, high-bit-depth decode/encode
- ☐ Qualifiers, power windows, tracking, node graph; scopes on a worker thread / GPU

## Phases 6–15
Multicam and text, VFX node graph, motion, advanced audio, AI (provider abstraction first), advanced VFX, delivery presets/DCP/IMF, collaboration, plugins/scripting, professionalization. These are unchanged from the master prompt. Each starts with its data model and interfaces, tests and an honest status entry in [feature-status.md](feature-status.md).

## Next recommended task
**Delivery, part 1: high-bit-depth I/O, export presets and a render queue.**
1. High-bit-depth decode: swscale to 16-bit RGBA (or float) instead of RGBA8, so 10/12-bit and log/HDR sources reach the float pipeline intact; golden tests on 10-bit synthetic media.
2. 10-bit encode: HEVC Main10 (libx265) and ProRes 422 HQ/4444 (prores_ks) when the encoders are available, with honest fallbacks and readable errors when they are not; HDR metadata (mastering display, MaxCLL/MaxFALL) for PQ exports.
3. An export dialog with presets (H.264 web, HEVC 10-bit, ProRes master, audio-only WAV, image sequence) stored as JSON; a background render queue with progress, cancel and per-job logs.
4. CLI: `export --preset`, `presets list`, `queue` commands.
5. Tests: bit-depth round trips (10-bit ramp keeps > 256 levels), preset validation, encoder fallback, queue ordering and cancellation, metadata written and probed back.
