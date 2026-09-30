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
- ✅ High-bit-depth decode/encode (16-bit decode of deep sources; 10/16-bit presets) and HDR10 static metadata (session 13)
- ☐ OCIO/ACES configs, tone and gamut mapping, an HDR display path
- ☐ Qualifiers, power windows, tracking, node graph; scopes on a worker thread / GPU

## Delivery (Phase 12, started early) ◐
- ✅ Export presets (built-in and user JSON), Export dialog, background render queue, ProRes/HEVC 10-bit/FFV1/WAV/PNG sequences, CLI `presets`, `export --preset`, `render-queue` (session 13)
- ☐ DNxHR and ProRes XQ, loudness (EBU R128 / ATSC A/85) measurement and normalisation, captions/subtitles, audio stems, burn-ins, DCP/IMF

## Phases 6–15
Multicam and text, VFX node graph, motion, advanced audio, AI (provider abstraction first), advanced VFX, delivery presets/DCP/IMF, collaboration, plugins/scripting, professionalization. These are unchanged from the master prompt. Each starts with its data model and interfaces, tests and an honest status entry in [feature-status.md](feature-status.md).

## Next recommended task
**Performance: proxies and a playback frame cache (Phase 1/3, capabilities 47–48).**
1. Proxy generation as background jobs: per-media proxies (e.g. 1/2 or 1/4 resolution, ProRes 422 Proxy or H.264 intra), stored next to the cache and keyed like thumbnails. Progress appears in the media pool.
2. A proxy switch (per project, and per viewer) that the playback engine and viewer honour. Export always uses the originals, which a test verifies.
3. A RAM frame cache for the viewer (LRU by timeline, frame and edit revision) so scrubbing over rendered frames is instant; invalidated by edits.
4. Measurements: dropped frames and render time per frame for 1080p and 4K timelines with and without proxies, recorded in the docs.
5. Tests: proxy creation and relinking, originals used for export, cache hits and invalidation after edits, dropped-frame counts with a fake clock.
