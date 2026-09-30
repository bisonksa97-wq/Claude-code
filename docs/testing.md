# Testing

```bash
ctest --test-dir build --output-on-failure
```

| Suite | Binary | Covers |
|---|---|---|
| Unit | `unit_tests` | Colour grading (identity at defaults, lift/gamma/gain/offset/contrast golden values, exposure and white balance in linear light, luma-preserving saturation and clamping), scopes (histogram, parade and vectorscope bins for known colours, waveform of a ramp), grade migration v6 → v7, pan law, gain, EQ band responses at known frequencies, compressor gain reduction and make-up, processor reset, effect validation, transition regions (alignment, handle and length clamping, fades, precedence), weights, constant-power gains, audio envelopes, razor behaviour, keyframe interpolation (linear, hold, ease, replace, remove), parameter metadata, compositing pixels (offset, opacity, scale, crop, rotation, clipping, invisible layers), clip markers across move/slip/razor, marker validation, three-point edit resolution (all 16 mark combinations, stills, impossible edits), disk cache (read/write, sharding, LRU eviction, safe clear), job queue (priority order, failure, cancellation of queued and running jobs, shutdown, waitIdle), rational/timecode (incl. drop-frame), errors, logging, atomic IO, command stack, every timeline operation, randomized timeline stress test, project format round-trip (incl. marks and targets), validation, migrations (verbatim v1 and v2 documents, v3 without transforms), marker and transform round trips |
| Integration | `integration_tests` | Grades rendered through the decoder, keyframed grades, persistence, copy/paste onto linked selections and undo, track pan/gain and meters, clip volume/pan automation, track effects (processing, enable/disable, chunked mixing equals one call, chain editing and undo), dissolve, dip and fades rendered through the decoder, constant-power audio crossfade into handles, transition validation and linked audio, an exported dissolve, swscale conversions at awkward sizes (valgrind-clean), multi-track compositing with scale/position/keyframed opacity through the real decoder, transform and keyframe session edits, an exported opacity fade verified frame by frame, multi-clip move/lift/ripple delete, track add/remove/rename/reorder with target repair, session markers (add, edit, move, delete, navigate, undo), clipboard (copy with links, paste on targets, disabled targets, paste insert, cut, duplicate, impossible destinations), session three-point edits (overwrite, backtimed insert, targets, errors, one undo step, mark persistence), thumbnail/waveform generation and formats, fingerprints, `MediaAssets` (async generation, memory and disk reuse, invalidation on file change, clear/regenerate, offline and undecodable media, damaged cache entries), playback engine (audio continuity against the offline mix, audio clock, video prefetch/drop, wall-clock fallback, snapshot isolation), sample FIFO, probe, frame-accurate sequential and random video decode, audio decode accuracy and seek alignment, encoder validation and fallback, session-level linked/sync editing, relink/offline, moved-project resolution, autosave/recovery, **end-to-end vertical slice with decoded-output verification**, mute/offline rendering, export cancel, aspect fit |
| UI | `ui_tests` | Drives the real `MainWindow` offscreen: media pool and background thumbnails, drop to timeline with snapping, viewer frame, razor action, mouse trim of linked clips, undo, save, export; playback through the viewer with an injected audio device; source monitor → I/O marks → target patching by clicking track headers → overwrite; copy/paste/duplicate/cut and markers through the menu actions; Ctrl-click selection, group drag, box selection, select all and multi-delete; Inspector scale and keyframed opacity with the program monitor checked; Ctrl+T dissolve on video and linked audio, removal and undo; Color panel (audio clips refused, saturation via spin box checked on the program monitor and scopes, a gain-wheel move as one luma-neutral undo step, keyframe toggle, copy/paste grade buttons, Reset Grade menu with undo) and all four scope modes; mixer fader/pan with undo, strips following tracks, meter ballistics, live meters during playback, effects dialog; the Qt audio adapter's no-device error; theme tokens |

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
