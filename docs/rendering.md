# Rendering, codecs and audio

## Pipeline (current)

```
Timeline ─► FrameCompositor ─► VideoFrame (RGBA8) ─┐
        └─► AudioMixer ─────► float stereo ─────────┴─► MediaWriter (FFmpeg) ─► file
```

- **VideoDecoder**: frame-accurate random access. Returns the last frame whose PTS ≤ t. Decodes forward for small jumps (< 2 s), otherwise seeks to the previous keyframe. Holds the last frame past EOF. Scales and converts with swscale.
- **AudioDecoder**: converts any input to interleaved float at the requested rate/channels (swresample). It streams sequential reads, and seeks with a 100 ms pre-roll so transform codecs (AAC) have their overlap frame. Reads outside the media return silence.
- **FrameCompositor**: the top-most enabled video track with a clip at the frame wins. It fits the source aspect-correct onto a black canvas. Offline media renders in the offline colour (`kOfflineColor`) so problems are visible. It samples 1/8 frame into the display interval to avoid rounding onto the previous frame.
- **AudioMixer**: sums audio tracks with track and clip gain (dB), honouring enable, mute and solo. Clip ranges are mapped to exact sample positions.
- **DecoderPool**: LRU of decoders keyed by clip, so two clips from one file don't thrash a single decoder.
- **MediaWriter**: H.264 (libx264, or a platform encoder) or MPEG-4 fallback, YUV 4:2:0, CRF or bitrate. AAC audio is fed in encoder-sized frames; the final partial frame is padded. The container is chosen by file extension.
- **ExportJob**: renders frame by frame with exact per-frame sample counts. It can be cancelled, writes `<name>.partial.<ext>` and renames on success, and runs on a worker thread against a project snapshot.

## Playback

```
                ┌─ audio worker: AudioMixer ─► SampleFifo ─► AudioOutput (device pulls) ─┐
PlaybackEngine ─┤                                                                          ├─ master clock
                └─ video worker: FrameCompositor ─► frames ahead ─► viewer polls ◄────────┘
```

- **Master clock:** the device's played-sample count (`AudioOutput::playedFrames`). Without a device (or if it fails to start), a monotonic wall clock is used and playback is silent. The viewer shows "Audio" or "No audio output".
- **Video follows the clock:** the worker skips frames that are already late instead of stalling, and the viewer always shows the newest frame at or before the clock. Skipped and never-shown frames are counted as *dropped* and displayed in the viewer.
- **Audio underruns** (the device pulling from an empty buffer before the end) are counted. The start of playback is primed with 150 ms of audio.
- `QtAudioOutput` (UI layer) implements `AudioOutput` with `QAudioSink` in pull mode: float32, falling back to int16, with an ~80 ms device buffer. Played position = frames pulled − device buffer size, which is an estimate. Hardware latency beyond the Qt buffer is not compensated yet.

## Determinism

For identical inputs and settings, compositing and mixing are deterministic. Encoders may vary across FFmpeg versions, so golden tests compare **decoded content with tolerances** (average colour per edit, audio RMS per region), not bytes.

## Known limitations

- 8-bit RGBA, no colour management (Rec.709 assumed by swscale defaults).
- No blending, transforms, transitions or effects.
- While **stopped**, the frame at the playhead is rendered on the UI thread. During playback, rendering happens on the engine's worker thread.
- Playback is CPU-only: one video worker, no GPU and no frame cache, so heavy timelines drop frames rather than stutter.
- Still images: supported by the model (unbounded clips) but not yet covered by tests.
