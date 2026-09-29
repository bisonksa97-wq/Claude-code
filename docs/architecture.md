# Architecture

## Modules

Each module is a static library under `src/<module>` with its own `CMakeLists.txt`.
Dependencies only point downward.

| Module | Target | Responsibility | Depends on |
|---|---|---|---|
| core | `up_core` | `Result`/`Error`, logging, `Rational`/timecode, `Command`/`CommandStack`, atomic file IO, ids, `MediaInfo` | – |
| timeline | `up_timeline` | Timeline/track/clip data model and deterministic edit operations | core |
| project | `up_project` | Project model (media pool, bins, timelines), `.uproj` serializer and migrations | core, timeline |
| codec | `up_codec` | FFmpeg wrappers: probe, `VideoDecoder`, `AudioDecoder`, `MediaWriter` | core, FFmpeg |
| media | `up_media` | Import, offline detection, relink validation, search, synthetic test media | core, project, codec |
| render | `up_render` | `FrameCompositor`, `AudioMixer`, `DecoderPool`, `ExportJob` | core, timeline, project, codec |
| app | `up_app` | `EditorSession` application services and undoable project commands | all of the above |
| cli | `ultimatepost` | Command-line front end | app |
| ui | `up_ui`, `ultimatepost-studio` | Qt Widgets front end | app, Qt 6 |

The conceptual engines from the master prompt map onto modules as they are built.
Today: Project, Media, Timeline, Codec, Video (CPU compositor), Audio (mixer), Render and UI exist.
The rest are listed in [roadmap.md](roadmap.md).

## Key design rules

- **Single mutation path.** Every change to a project goes through `EditorSession`, which wraps it in a `Command` on the `CommandStack`. The UI and CLI never mutate the model directly.
- **All-or-nothing edits.** `timeline/EditOperations` run on a working copy and commit only if the result passes `Timeline::validate()`. Session-level edits that touch several clips (linked A/V, insert across tracks) run inside one `TimelineEditCommand`, which snapshots the timeline before and after. Undo and redo are therefore exact and deterministic.
- **Linked clips.** Video and audio from one file share a `linkId`. Session-level operations apply to linked partners on unlocked tracks. The low-level ops never do.
- **Non-destructive.** Clips reference media by id with a source range. Media is never copied into the project.
- **Human-readable errors.** Failures return `Error{code, subsystem, message, suggestion, details}`. `Error::id()` gives a stable id (`UP-CODEC-DECODE_ERROR`), and `toString()` is the "copy diagnostics" text shown in the UI's detail pane.
- **Subsystem logging.** `UP_LOG_*` macros carry a subsystem tag (`app`, `media`, `codec`, `timeline`, `render`, `audio`, `ui`...) with a configurable level per subsystem. The desktop app also writes a log file under the platform's app-data directory.

## Threading

- The model (`Project`, `Timeline`) is owned by the UI thread and is not thread-safe.
- `ExportJob` receives a **copy** of the project, so export runs on a worker thread while editing continues.
- Decoders are single-threaded objects. Each `FrameCompositor`/`AudioMixer` owns its own `DecoderPool`. The viewer and export never share decoders.
- FFmpeg's internal frame threading is enabled in the video decoder.

## Crash safety

- Project writes are transactional: temp file → fsync → rename, with the previous version kept as `.uproj.bak`.
- Autosave writes `<name>.autosave.uproj` next to saved projects, or to `<tmp>/UltimatePost/Recovery/<id>.uproj` for unsaved ones. Opening a project with a newer autosave offers recovery. A successful save removes the autosave.
- Export writes to `<name>.partial.<ext>` and renames only on success. Cancelled or failed exports leave nothing behind.
