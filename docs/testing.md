# Testing

```bash
ctest --test-dir build --output-on-failure
```

| Suite | Binary | Covers |
|---|---|---|
| Unit | `unit_tests` | Disk cache (read/write, sharding, LRU eviction, safe clear), job queue (priority order, failure, cancellation of queued and running jobs, shutdown, waitIdle), rational/timecode (incl. drop-frame), errors, logging, atomic IO, command stack, every timeline operation, randomized timeline stress test, project format round-trip, validation, migrations |
| Integration | `integration_tests` | Thumbnail/waveform generation and formats, fingerprints, `MediaAssets` (async generation, memory and disk reuse, invalidation on file change, clear/regenerate, offline and undecodable media, damaged cache entries), playback engine (audio continuity against the offline mix, audio clock, video prefetch/drop, wall-clock fallback, snapshot isolation), sample FIFO, probe, frame-accurate sequential and random video decode, audio decode accuracy and seek alignment, encoder validation and fallback, session-level linked/sync editing, relink/offline, moved-project resolution, autosave/recovery, **end-to-end vertical slice with decoded-output verification**, mute/offline rendering, export cancel, aspect fit |
| UI | `ui_tests` | Drives the real `MainWindow` offscreen: media pool and background thumbnails, drop to timeline with snapping, viewer frame, razor action, mouse trim of linked clips, undo, save, export; playback through the viewer with an injected audio device; the Qt audio adapter's no-device error; theme tokens |

## Deterministic test media

No binary media is checked in. Tests generate it with `media::generateSyntheticMedia` (also available as `ultimatepost gen-test-media`):
- **Solid** colours per clip, used to verify which clip appears in the output.
- **FrameRamp**: the grey level encodes the frame number, so every decoded frame identifies itself (frame-accuracy tests).
- **Bars** with a moving marker for visual checks.
- Sine tones at chosen frequency and level, for audio RMS checks.

UI tests default to Qt's `offscreen` platform. Set `UP_UI_SCREENSHOT=/path/shot.png` to save a screenshot of the test window.

## Threading checks
Build with `-DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread` and run the playback and UI tests. Expect reports that originate inside FFmpeg or Qt thread pools; they are uninstrumented and not our races. Any report with Ultimate Post frames on both sides is a bug.

Playback tests use a fake `AudioOutput` pumped by the test and a `ManualClock`, so they are timing-independent. They passed 50 consecutive repeats.

## Not yet covered
Performance and stress benchmarks, 4K/8K/HDR/RAW golden media, Windows and macOS runs, and long-duration tests (see the §73 golden dataset in the roadmap).
