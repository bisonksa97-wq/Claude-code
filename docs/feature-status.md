# Feature status

Status labels follow the master prompt: **IMPLEMENTED** · **PARTIAL** · **INTERFACE ONLY** · **PLACEHOLDER** · **NOT IMPLEMENTED**.
A feature is only IMPLEMENTED when it has a model, engine code, UI and/or CLI, error handling, persistence where needed, undo where applicable, tests and docs.

_Last updated: session 4._

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
| 2 | Professional video editing | PARTIAL | Multitrack V/A timeline, linked clips, full trim toolset. No effects, transitions or nesting. |
| 3 | Fast cutting | PARTIAL | Razor, lift, ripple delete, keyboard trims, **source monitor with three-point insert/overwrite** (I/O marks, `,` and `.`), source patching. No Cut workspace, no match frame, no J/K/L. |
| 4 | Multicam | NOT IMPLEMENTED | |
| 5 | Timeline editing | IMPLEMENTED | See [timeline.md](timeline.md). Includes in/out marks and track targeting. Missing: markers, adding/removing tracks in the UI, copy/paste. |
| 6 | Text-based editing | NOT IMPLEMENTED | |
| 7–15 | Motion graphics, 2D/3D compositing, VFX, keying, roto, tracking, camera tracking, particles | NOT IMPLEMENTED | |
| 16–18 | Color grading, HDR, RAW | NOT IMPLEMENTED | 8-bit RGBA CPU pipeline only; no colour management yet. |
| 19–20 | Audio editing, DAW mixing | PARTIAL | Track gain, clip gain, mute, solo, enable; stereo mix for playback and export. No pan, EQ, dynamics, automation or buses. |
| 21–22 | ADR, Foley | NOT IMPLEMENTED | |
| 23 | Captions/subtitles | NOT IMPLEMENTED | |
| 24–30 | AI search, masking, tracking, enhancement, audio AI, generative video/audio | NOT IMPLEMENTED | No AI provider abstraction yet (planned for Phase 10). |
| 31 | Photo/RAW editing | NOT IMPLEMENTED | Still images import as unbounded clips, but have not been tested. |
| 32–33 | VR/360, stereoscopic | NOT IMPLEMENTED | |
| 34 | Professional export | PARTIAL | H.264/AAC (MP4/MOV/MKV) with CRF; encoder choosable via CLI `--codec`; fallback to MPEG-4 Part 2. No presets, ProRes/DNx profiles or export dialog settings. |
| 35–38 | Broadcast, cinema, DCP, IMF | NOT IMPLEMENTED | |
| 39–41 | Cloud collaboration, review/approval, version control | NOT IMPLEMENTED | |
| 42 | Plugin support | NOT IMPLEMENTED | |
| 43 | Python/Lua/JS automation | NOT IMPLEMENTED | The `EditorSession` service layer is the planned binding surface. |
| 44 | CLI automation | PARTIAL | `ultimatepost` covers the whole slice, plus `analyze`, `cache-info` and `cache-clear`. No `transcode/proxy/transcribe/archive` yet. |
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
| Versioned format + migrations (§8) | IMPLEMENTED | Format v2 with a real v1 → v2 migration, tested against a verbatim v1 document. |
| Human-readable errors (§76) | IMPLEMENTED | Error id, message, suggestion, technical details; copyable in UI dialogs. |
| Logging (§77) | IMPLEMENTED | Per-subsystem levels; log file in the app-data folder. |
| Themes / design tokens (§69) | IMPLEMENTED | Dark, light, high contrast. |
| Dockable/floating panels (§68) | PARTIAL | Qt docks can float onto other monitors. Workspace layouts are not saved yet. |
| Accessibility (§70) | PARTIAL | Keyboard shortcuts for all edit/transport actions, accessible names, font-scaled metrics. Shortcut remapping is not available. |
| Background jobs | IMPLEMENTED | `JobQueue` with priorities, cancellation, status history and completion listeners. Used for media analysis; export still uses its own thread. |
| Performance monitor (§75) | PARTIAL | The viewer shows the clock source and dropped frames during playback. The engine also counts audio underruns. There is no dedicated panel. |
| Security (§71) | NOT IMPLEMENTED | Nothing network-facing exists yet. |
