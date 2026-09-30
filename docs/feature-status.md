# Feature status

Status labels follow the master prompt: **IMPLEMENTED** · **PARTIAL** · **INTERFACE ONLY** · **PLACEHOLDER** · **NOT IMPLEMENTED**.
A feature is only IMPLEMENTED when it has a model, engine code, UI and/or CLI, error handling, persistence where needed, undo where applicable, tests and docs.

_Last updated: session 9._

## First vertical slice (§90)

| Step | Status | Where / how verified |
|---|---|---|
| Create project | IMPLEMENTED | File ▸ New, `ultimatepost new`; SessionTest |
| Import video | IMPLEMENTED | File ▸ Import, Media Pool button, `ultimatepost import`; SessionTest |
| Show media | IMPLEMENTED | Media Pool (name, duration, format, online state, search); UI test |
| Drag video to timeline | IMPLEMENTED | Drag from Media Pool; overwrite by default, Ctrl = insert; UI test |
| Play video | IMPLEMENTED | `PlaybackEngine`: audio through the system output (Qt Multimedia) as the master clock, video rendered ahead on a worker thread and synced to it, dropped-frame and underrun counters, silent wall-clock fallback without a device. Tests: PlaybackTest (played audio equals the offline mix; clock, prefetch, frame dropping, fallback), UI test (viewer and playhead follow the audio clock). **The real `QAudioSink` path has not been heard on hardware:** the development container has no audio device. |
| Cut clip | IMPLEMENTED | Razor at playhead (Ctrl+K), `ultimatepost razor`; ops, session and UI tests |
| Trim clip | IMPLEMENTED | Drag clip edges (Shift = ripple), `[`/`]` keys, `ultimatepost trim`; tests |
| Add second clip | IMPLEMENTED | Drag/append; tests |
| Add audio | IMPLEMENTED | Audio-only media goes on audio tracks; mixed on export; VerticalSliceTest |
| Save project | IMPLEMENTED | Atomic save with backup; tests |
| Reopen project | IMPLEMENTED | Round-trip is byte-identical; offline detection; tests |
| Export video | IMPLEMENTED | File ▸ Export (background, progress, cancel), `ultimatepost export`; output decoded and verified in tests |

## Capability matrix (§1)

| # | Capability | Status | Notes |
|---|---|---|---|
| 1 | Media management | PARTIAL | Import, probe metadata, search, offline detection, relink/replace. **Thumbnails** in the media pool (small/large toggle) and on video clips, **waveforms** on audio clips, generated in the background and cached. Bins exist in the model (one "Master" bin); there is no bin UI, ratings/keywords UI, filmstrip view or hover scrub. |
| 2 | Professional video editing | PARTIAL | Multitrack V/A timeline, linked clips, full trim toolset, cross dissolve / dip to black transitions and fades, keyframed transforms. No effects, wipes or nesting. |
| 3 | Fast cutting | PARTIAL | Razor, lift, ripple delete, keyboard trims, **source monitor with three-point insert/overwrite** (I/O marks, `,` and `.`), source patching. No Cut workspace, no match frame, no J/K/L. |
| 4 | Multicam | NOT IMPLEMENTED | |
| 5 | Timeline editing | IMPLEMENTED | See [timeline.md](timeline.md). Includes in/out marks, track targeting, timeline and clip markers (dialog, navigation) and copy/cut/paste/duplicate. Also multi-clip selection (Ctrl/Shift-click, box selection, select all/forward) with group move and delete, and track add/remove/rename/reorder. Missing: a markers list panel, paste attributes, nesting/compound clips. |
| 6 | Text-based editing | NOT IMPLEMENTED | |
| 7 | Motion graphics | PARTIAL | Keyframed position, scale, rotation, opacity and crop per clip with linear/hold/ease interpolation, an Inspector panel and keyframe navigation. No text, shapes, graph editor or expressions. |
| 8 | 2D compositing | PARTIAL | All video tracks blend bottom-to-top with transforms and opacity (straight-alpha over). No blend modes, masks or node graph. |
| 9–15 | 3D compositing, VFX, keying, roto, tracking, camera tracking, particles | NOT IMPLEMENTED | |
| 16 | Color grading | PARTIAL | Colour management: media, timeline and output colour spaces (Rec.709, sRGB, P3, Rec.2020, PQ, HLG, linear, ARRI LogC3, Sony S-Log3) with a float working pipeline; clipping and false-colour viewer overlays; correct YUV matrices on decode and encode. No OCIO/ACES, tone mapping or gamut mapping. |
| 16a | Color grading tools | PARTIAL | Per-clip primary correction: lift/gamma/gain/offset (master and RGB), contrast with pivot, saturation, exposure, temperature and tint, all keyframable. Custom curves (master, R, G, B, hue vs hue, hue vs sat, lum vs sat) with a curve editor. `.cube` 3D/1D LUTs per clip and as a timeline output LUT, relinkable, with missing LUTs flagged and blocking export. Bypass per clip and for the whole timeline. Named grade versions per clip. Color panel with four balance wheels and numeric rows; copy/paste/reset grade (all undoable). Scopes panel: waveform, RGB parade, vectorscope and histogram of the program monitor. CLI `grade`, `output-lut`, `bypass-grades`, `relink-lut`, `lut-info`, `scopes`. Not yet: qualifiers, windows, tracking, node graph, OCIO/ACES, keyframed curves, and live preview while dragging a wheel or curve point (they commit on release). |
| 17 | HDR | PARTIAL | Colour management with PQ and HLG (Rec.2100) as timeline or output spaces, float working pipeline, 16-bit decode of deep sources, HEVC Main10 / ProRes 10-bit exports with correct tags and HDR10 static metadata (mastering display, user-entered MaxCLL/MaxFALL). Not yet: tone mapping, an HDR display path, measured MaxCLL/MaxFALL, dynamic metadata (HDR10+/Dolby Vision). |
| 18 | RAW | NOT IMPLEMENTED | |
| 19–20 | Audio editing, DAW mixing | PARTIAL | Track gain and pan, mute/solo, clip gain, keyframable clip volume and pan, constant-power crossfades and fades, per-track insert effects (gain, 3-band EQ, compressor), mixer panel with meters, stereo mix for playback and export. No buses/sends, track automation lanes, surround or plugin hosting. |
| 21–22 | ADR, Foley | NOT IMPLEMENTED | |
| 23 | Captions/subtitles | NOT IMPLEMENTED | |
| 24–30 | AI search, masking, tracking, enhancement, audio AI, generative video/audio | NOT IMPLEMENTED | No AI provider abstraction yet (planned for Phase 10). |
| 31 | Photo/RAW editing | NOT IMPLEMENTED | Still images import as unbounded clips, but have not been tested. |
| 32–33 | VR/360, stereoscopic | NOT IMPLEMENTED | |
| 34 | Professional export | PARTIAL | Presets: H.264 (web/high), HEVC 10-bit, ProRes 422 HQ and 4444, FFV1 lossless 10-bit, 24-bit WAV, 16-bit PNG sequences; user presets as JSON; an Export dialog (preset, output, range, HDR metadata) feeding a background **render queue** (progress, logs, cancel, failures isolated). 10/16-bit output from the float pipeline. Not yet: DNxHR, ProRes XQ, burned-in timecode/watermarks, loudness normalisation, captions, multiple audio tracks/stems, smart rendering. |
| 35–38 | Broadcast, cinema, DCP, IMF | NOT IMPLEMENTED | |
| 39–41 | Cloud collaboration, review/approval, version control | NOT IMPLEMENTED | |
| 42 | Plugin support | NOT IMPLEMENTED | |
| 43 | Python/Lua/JS automation | NOT IMPLEMENTED | The `EditorSession` service layer is the planned binding surface. |
| 44 | CLI automation | PARTIAL | `ultimatepost` covers the whole slice, plus three-point editing, markers, duplicate, transforms, transitions, tracks, audio effects, colour grades, curves, LUTs, colour spaces, scopes, export presets (`presets`, `export --preset`) and batch rendering (`render-queue`), `analyze`, `cache-info` and `cache-clear`. No `transcode/proxy/transcribe/archive` yet. |
| 45–46 | GPU / hardware acceleration | NOT IMPLEMENTED | CPU reference pipeline only; FFmpeg decoder frame threading is enabled. |
| 47 | Proxy workflows | NOT IMPLEMENTED | |
| 48 | Render caching | PARTIAL | The cache engine (§57) exists and holds the thumbnail and waveform caches. There is no playback, effect or render cache yet. |
| 49 | Professional QC | NOT IMPLEMENTED | Offline media is detected and reported (export warns, renders an offline colour). |
| 50 | Project archiving | NOT IMPLEMENTED | |

## Cross-cutting requirements

| Requirement | Status | Notes |
|---|---|---|
| Undo/redo (§16) | IMPLEMENTED | Command stack with groups/transactions, configurable limit, clean-state tracking. Timeline edits use exact snapshots. |
| Autosave / crash recovery (§74) | IMPLEMENTED | Periodic autosave (setting `autosave/intervalSeconds`, default 120), recovery prompts on open and at startup, transactional writes. No crash-report capture yet. |
| Versioned format + migrations (§8) | IMPLEMENTED | Format v7 with a migration step for every version since v1, each tested against a document in the old shape (v1 verbatim). |
| Human-readable errors (§76) | IMPLEMENTED | Error id, message, suggestion, technical details; copyable in UI dialogs. |
| Logging (§77) | IMPLEMENTED | Per-subsystem levels; log file in the app-data folder. |
| Themes / design tokens (§69) | IMPLEMENTED | Dark, light, high contrast. |
| Dockable/floating panels (§68) | PARTIAL | Qt docks can float onto other monitors. Workspace layouts are not saved yet. |
| Accessibility (§70) | PARTIAL | Keyboard shortcuts for all edit/transport actions, accessible names, font-scaled metrics. Shortcut remapping is not available. |
| Background jobs | IMPLEMENTED | `JobQueue` with priorities, cancellation, status history and completion listeners, used for media analysis. Exports run in the `RenderQueue` (one job at a time, cancellable, logged). |
| Audio meters | IMPLEMENTED | Per-track and master peak meters synchronised to what is heard, with ballistics and peak hold. |
| Performance monitor (§75) | PARTIAL | The viewer shows the clock source and dropped frames during playback. The engine also counts audio underruns. There is no dedicated panel. |
| Security (§71) | NOT IMPLEMENTED | Nothing network-facing exists yet. |
