# Rendering, codecs and audio

## Pipeline (current)

```
Timeline ─► FrameCompositor ─► VideoFrame (RGBA8) ─┐
        └─► AudioMixer ─────► float stereo ─────────┴─► MediaWriter (FFmpeg) ─► file
```

- **VideoDecoder**: frame-accurate random access. Returns the last frame whose PTS ≤ t. Decodes forward for small jumps (< 2 s), otherwise seeks to the previous keyframe. Holds the last frame past EOF. Scales and converts with swscale.
- **AudioDecoder**: converts any input to interleaved float at the requested rate/channels (swresample). It streams sequential reads, and seeks with a 100 ms pre-roll so transform codecs (AAC) have their overlap frame. Reads outside the media return silence.
- **FrameCompositor**: composites every enabled video track bottom (V1) to top over black. Each clip is fitted to the timeline frame (aspect preserved), then its transform, evaluated at the frame's *source* position, applies scale, rotation (clockwise about the centre), position (timeline pixels from the centre), crop (percent per edge) and opacity. Blending is straight-alpha "over". Layers below a fully opaque, frame-covering layer are skipped. Sources are decoded no larger than needed and never above native size; upscaling happens in compositing. Offline media is drawn as a layer of the offline colour (`kOfflineColor`) with the same transform. Decoding samples 1/8 frame into the display interval to avoid rounding onto the previous frame.
- **Transitions**: during a transition or fade a track contributes `withOutgoing·a + below·b + withIncoming·c` (weights from `transitions::videoWeights`). Each "with" canvas is a copy of the canvas below with that clip composited over it, so dissolves stay correct over lower tracks at the cost of two canvas copies per transitioning track.
- **compositeOver** (`render/Compositing.h`): the pure blending function. Unrotated 1:1 layers use a row-copy fast path (position rounded to whole pixels); everything else is inverse-mapped with bilinear sampling. It is tested on synthetic images for offset, opacity, scale, crop, rotation and clipping.
- **AudioMixer**: sums audio tracks with track and clip gain (dB), honouring enable, mute and solo. Clip ranges are mapped to exact sample positions. Transitions and fades apply a per-sample constant-power envelope, and clips play into their handles during edit-point crossfades.
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

## Derived media: thumbnails and waveforms

- **Thumbnail:** a poster frame at 10% of the duration (max 5 s; frame 0 for stills), fitted inside 192×108 and stored as binary PPM.
- **Waveform:** a min/max envelope of all channels, with 480 samples per peak at 48 kHz (100 peaks/s). It covers the probed duration, so silent passages are kept, and is stored as `UPWF` v1 binary (header + int16 pairs). The timeline draws one line per pixel column from the peaks under that column.
- **Cache keys:** kind + format version + file fingerprint (absolute path, size, mtime) + parameters, hashed with FNV-1a into `<cache>/<2 hex>/<16 hex><ext>.upc`. A changed or relinked file therefore never reuses stale assets. Unchanged files are never regenerated across app restarts.
- **Safety:** the cache holds only regeneratable data. Writes are atomic. Damaged entries are detected on decode and regenerated. `clear()` only removes `*.upc` files. The default limit is 2 GB with LRU eviction.
- **Location:** the per-user cache folder (`$XDG_CACHE_HOME/UltimatePost`, `~/Library/Caches/UltimatePost`, or `%LOCALAPPDATA%\UltimatePost\Cache`). The desktop app honours the `cache/directory` setting, and the CLI takes `--cache-dir`.

## Determinism

For identical inputs and settings, compositing and mixing are deterministic. Encoders may vary across FFmpeg versions, so golden tests compare **decoded content with tolerances** (average colour per edit, audio RMS per region), not bytes.

## Known limitations

- 8-bit RGBA, no colour management (Rec.709 assumed by swscale defaults).
- CPU compositing only. There are no blend modes besides "over", no anchor point or motion blur, and no transitions or effects yet.
- **swscale buffers**: its SIMD paths read and write past the end of rows that are not a multiple of 64 bytes. All conversions therefore go through padded, 64-byte-aligned scratch memory (`ffmpeg::alignedStride`). This fixed heap corruption at preview widths such as 120 px; the regression test `CodecTest.ConvertsToAndFromAwkwardSizes` is clean under valgrind.
- While **stopped**, the frame at the playhead is rendered on the UI thread. During playback, rendering happens on the engine's worker thread.
- Playback is CPU-only: one video worker, no GPU and no frame cache, so heavy timelines drop frames rather than stutter.
- Still images: supported by the model (unbounded clips) but not yet covered by tests.
