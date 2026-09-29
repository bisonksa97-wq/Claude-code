# Testing

```bash
ctest --test-dir build --output-on-failure
```

| Suite | Binary | Covers |
|---|---|---|
| Unit | `unit_tests` | Rational/timecode (incl. drop-frame), errors, logging, atomic IO, command stack, every timeline operation, randomized timeline stress test, project format round-trip, validation, migrations |
| Integration | `integration_tests` | Probe, frame-accurate sequential and random video decode, audio decode accuracy and seek alignment, encoder validation and fallback, session-level linked/sync editing, relink/offline, moved-project resolution, autosave/recovery, **end-to-end vertical slice with decoded-output verification**, mute/offline rendering, export cancel, aspect fit |
| UI | `ui_tests` | Drives the real `MainWindow` offscreen: media pool, drop to timeline with snapping, viewer frame, razor action, mouse trim of linked clips, undo, save, export; theme tokens |

## Deterministic test media

No binary media is checked in. Tests generate it with `media::generateSyntheticMedia` (also available as `ultimatepost gen-test-media`):
- **Solid** colours per clip, used to verify which clip appears in the output.
- **FrameRamp**: the grey level encodes the frame number, so every decoded frame identifies itself (frame-accuracy tests).
- **Bars** with a moving marker for visual checks.
- Sine tones at chosen frequency and level, for audio RMS checks.

UI tests default to Qt's `offscreen` platform. Set `UP_UI_SCREENSHOT=/path/shot.png` to save a screenshot of the test window.

## Not yet covered
Performance and stress benchmarks, 4K/8K/HDR/RAW golden media, Windows and macOS runs, and long-duration tests (see the §73 golden dataset in the roadmap).
