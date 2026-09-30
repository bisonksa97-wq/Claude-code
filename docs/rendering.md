# Rendering, codecs and audio

## Pipeline (current)

```
Timeline ─► FrameCompositor ─► VideoFrame (RGBA8) ─┐
        └─► AudioMixer ─────► float stereo ─────────┴─► MediaWriter (FFmpeg) ─► file
```

- **VideoDecoder**: frame-accurate random access. Returns the last frame whose PTS ≤ t. Decodes forward for small jumps (< 2 s), otherwise seeks to the previous keyframe. Holds the last frame past EOF. Scales and converts with swscale.
- **AudioDecoder**: converts any input to interleaved float at the requested rate/channels (swresample). It streams sequential reads, and seeks with a 100 ms pre-roll so transform codecs (AAC) have their overlap frame. Reads outside the media return silence.
- **FrameCompositor**: composites every enabled video track bottom (V1) to top over black. Each clip is fitted to the timeline frame (aspect preserved), then its transform, evaluated at the frame's *source* position, applies scale, rotation (clockwise about the centre), position (timeline pixels from the centre), crop (percent per edge) and opacity. Blending is straight-alpha "over". Layers below a fully opaque, frame-covering layer are skipped. Sources are decoded no larger than needed and never above native size; upscaling happens in compositing. Offline media is drawn as a layer of the offline colour (`kOfflineColor`) with the same transform. Decoding samples 1/8 frame into the display interval to avoid rounding onto the previous frame.
- **Colour grade** (`render/ColorGrading.h`): applied to each decoded video source *before* fit and transform, evaluated at the clip's source frame (so grade keyframes move with the media like transform keyframes), and skipped entirely when the grade is the identity. Per channel:
  1. decode to linear light (sRGB curve), multiply by 2^exposure, apply white balance (temperature: R×(1+0.3t), B×(1−0.3t); tint: G×(1−0.3m), with t and m the slider value / 100), re-encode;
  2. on encoded values: offset (master + channel), lift `v + l·(1−v)`, gain (master × channel), gamma `v^(1/γ)`, contrast `(v−pivot)·c + pivot`.
  Those per-channel steps are exact 256-entry lookup tables built once per frame. Saturation then scales each pixel's distance from its Rec.709 luma, and the result is clamped to 0..1. Alpha is never changed. Parameter values are clamped to their ranges when evaluated. Offline-media placeholders are not graded.
- **Scopes** (`render/Scopes.h`): histogram (R, G, B and Rec.709 luma), luma waveform, RGB parade (both binned per image column), vectorscope (Rec.709 Cb/Cr, 256×256, Cr up) and per-channel min/max and mean luma. They are computed from 8-bit display values. The Scopes panel analyses the program monitor's current preview image, reduced to at most 480 px wide, on the UI thread, at most 8 times per second, and only while it is visible. The CLI's `scopes` command analyses a full-resolution render.
- **Transitions**: during a transition or fade a track contributes `withOutgoing·a + below·b + withIncoming·c` (weights from `transitions::videoWeights`). Each "with" canvas is a copy of the canvas below with that clip composited over it, so dissolves stay correct over lower tracks at the cost of two canvas copies per transitioning track.
- **compositeOver** (`render/Compositing.h`): the pure blending function. Unrotated 1:1 layers use a row-copy fast path (position rounded to whole pixels); everything else is inverse-mapped with bilinear sampling. It is tested on synthetic images for offset, opacity, scale, crop, rotation and clipping.
- **AudioMixer**: per track, clips are summed into a **track bus** with clip gain, keyframable clip **Volume** (dB) and **Pan**, and the transition envelope, all evaluated per sample when animated. The bus runs through the track's **insert effects**, then track gain and pan, into the master. Enable, mute and solo are honoured. Effect processors keep their state between consecutive calls: splitting a mix into chunks is bit-identical to one call, which is tested. State resets when a call does not continue where the last ended (a seek), and processors are rebuilt when the chain changes. Each call can return **meters** (peak L/R and RMS per track and for the master).
- **Pan law**: balance with unity at centre. The far side follows a constant-power curve and the near side never exceeds unity, so centred material is untouched.
- **Effects** (`src/audio`): `gain`, `eq3` (low shelf, peaking mid, high shelf; RBJ biquads in transposed direct form II) and `compressor` (feed-forward, stereo-linked peak detection, hard knee, attack/release smoothing of the gain reduction, make-up gain). Parameters have validated ranges. Responses are unit-tested at known frequencies and levels.
- **Metering in playback**: the engine's audio worker stores each chunk's meters with its timeline position, and `PlaybackEngine::meters()` returns the entry for the sample being heard, so meters line up with the sound rather than with the mix-ahead buffer. The mixer panel applies ballistics (instant rise, 24 dB/s fall, 1.2 s peak hold).
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

- 8-bit RGBA, no colour management (Rec.709 assumed by swscale defaults). Grading decodes with the sRGB curve regardless of the source's actual transfer function, so HDR or log footage is not handled correctly yet.
- Grading quantises back to 8 bits after each clip, so heavy grades can band. A float pipeline is part of the GPU/colour-management work.
- CPU compositing only. There are no blend modes besides "over", no anchor point or motion blur, and no video effects besides the primary grade.
- **swscale buffers**: its SIMD paths read and write past the end of rows that are not a multiple of 64 bytes. All conversions therefore go through padded, 64-byte-aligned scratch memory (`ffmpeg::alignedStride`). This fixed heap corruption at preview widths such as 120 px; the regression test `CodecTest.ConvertsToAndFromAwkwardSizes` is clean under valgrind.
- While **stopped**, the frame at the playhead is rendered on the UI thread. During playback, rendering happens on the engine's worker thread.
- Playback is CPU-only: one video worker, no GPU and no frame cache, so heavy timelines drop frames rather than stutter.
- Still images: supported by the model (unbounded clips) but not yet covered by tests.
