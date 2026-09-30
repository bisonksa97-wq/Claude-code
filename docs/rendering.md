# Rendering, codecs and audio

## Pipeline (current)

```
Timeline ─► FrameCompositor ─► VideoFrame (RGBA8) ─┐
        └─► AudioMixer ─────► float stereo ─────────┴─► MediaWriter (FFmpeg) ─► file

Inside FrameCompositor, per frame (float RGBA, one quantisation at the end):
  decode (8-bit RGBA) ─► media space → timeline space ─► grade ─► transform + composite
  ─► transitions ─► timeline space → output space ─► output LUT ─► quantise to 8 bits
```

- **VideoDecoder**: frame-accurate random access. Returns the last frame whose PTS ≤ t. Decodes forward for small jumps (< 2 s), otherwise seeks to the previous keyframe. Holds the last frame past EOF. Scales and converts with swscale, using the stream's own YUV matrix (BT.709, BT.2020, BT.601…; untagged: BT.709 from 720 lines up, else BT.601) and range (limited or full). Before this, swscale's default BT.601 matrix was used for everything.
- **MediaWriter** colour: the RGB→YUV conversion uses the matrix it tags (default BT.709, limited range), and the stream carries primaries, transfer and matrix tags taken from the timeline's output colour space. `CodecColor.YuvMatrixAndTagsFollowTheSettings` checks the written Y/Cb/Cr values directly through libav (pure red is Y 63 / Cb 102 / Cr 240 in BT.709, Y 81 / Cb 90 in BT.601).
- **AudioDecoder**: converts any input to interleaved float at the requested rate/channels (swresample). It streams sequential reads, and seeks with a 100 ms pre-roll so transform codecs (AAC) have their overlap frame. Reads outside the media return silence.
- **FrameCompositor**: composites every enabled video track bottom (V1) to top over black. Each clip is fitted to the timeline frame (aspect preserved), then its transform, evaluated at the frame's *source* position, applies scale, rotation (clockwise about the centre), position (timeline pixels from the centre), crop (percent per edge) and opacity. Blending is straight-alpha "over". Layers below a fully opaque, frame-covering layer are skipped. Sources are decoded no larger than needed and never above native size; upscaling happens in compositing. Offline media is drawn as a layer of the offline colour (`kOfflineColor`) with the same transform. Decoding samples 1/8 frame into the display interval to avoid rounding onto the previous frame.
- **Float working pipeline** (`render/FloatFrame.h`): layers, the canvas, transitions, the output transform and the output LUT all work on float RGBA without clamping, so values beyond 0..1 (HDR, log, wide-gamut conversions) survive until the one quantisation at the end. `ColorManagement.FloatGradingAvoidsIntermediateBanding` shows the difference: gain 0.25 then a ×4 output LUT keeps all 256 levels, where quantising in between keeps 65. Pixel loops run on all cores (`render/Parallel.h`, row chunks; tiny frames stay on one thread). 1080p, 50 frames, 4 cores, Release, including decode and libx264 encode: 2.9 s ungraded (3.6 s with the previous 8-bit pipeline), 4.1 s graded (6.7 s before), 6.5 s graded on a linear timeline with an output transform.
- **Colour management** (`render/ColorManagement.h`, `timeline/ColorSpace.h`): a colour space is primaries (Rec.709, Rec.2020, P3-D65, ARRI Wide Gamut 3, S-Gamut3.Cine; all D65) plus a transfer function (linear, sRGB, BT.1886 gamma 2.4, gamma 2.2, PQ, HLG, ARRI LogC3 EI 800, Sony S-Log3). Linear 1.0 is reference white: SDR white, 203 cd/m² for PQ and the 75 % signal for HLG (BT.2408); camera logs decode to scene-linear with 18 % grey at 0.18. HLG is treated as scene-referred (no OOTF / system gamma).
  - Each media item has a space: detected from its tags (untagged video = Rec.709 gamma 2.4, untagged stills = sRGB) or set by hand. Each timeline has a **timeline colour space** (default Rec.709 gamma 2.4), where grading and compositing happen, and an **output colour space** (default: the same), used for viewing and export and written into exported files' tags.
  - A conversion decodes the source transfer, converts primaries in linear light with a 3×3 matrix derived from the chromaticities (checked against BT.2087's published Rec.709→Rec.2020 matrix), and encodes the destination transfer. There is **no tone mapping or gamut mapping**: out-of-range values are kept in float and clip at the final quantisation. Equal spaces skip the conversion entirely, so default projects pass pixels straight through.
  - Grading's exposure and white balance use the *timeline's* transfer to reach linear light. Dissolves and dips mix in the timeline space; dips fade towards the space's black (encoded linear 0, which is not code 0 for log encodings).
- **Viewer overlays** (`render/ViewerOverlay.h`): *Clipping* (any channel ≥ 254 red, all channels ≤ 1 blue) and *False color* (luma bands: < 2.5 % purple, 2.5–4 % blue, 38–42 % green, 52–56 % pink, 97–99 % yellow, > 99 % red, otherwise grey). They are drawn on the displayed 8-bit output image only; scopes and exports never see them.
- **Colour grade** (`render/ColorGrading.h`): applied to each decoded video source *before* fit and transform, evaluated at the clip's source frame (so grade keyframes move with the media like transform keyframes). It is skipped entirely when the grade is the identity, when the clip is bypassed, or when the timeline's *Bypass All Grades* is on. Per channel:
  1. decode to linear light (sRGB curve), multiply by 2^exposure, apply white balance (temperature: R×(1+0.3t), B×(1−0.3t); tint: G×(1−0.3m), with t and m the slider value / 100), re-encode;
  2. on encoded values: offset (master + channel), lift `v + l·(1−v)`, gain (master × channel), gamma `v^(1/γ)`, contrast `(v−pivot)·c + pivot`.
  3. the master tone curve, then the red/green/blue curve (on values clamped to 0..1; skipped when all are empty).
  Those per-channel steps are exact 256-entry lookup tables built once per frame. Then, per pixel in Rec.709 Y/Cb/Cr (luma is kept): saturation, lum-vs-sat (×2y at the pixel's luma), hue-vs-sat (×2y at its hue) and hue-vs-hue (rotation by y−0.5 turns). Hue is the vectorscope angle with red at 0. Finally the clip's LUT, if any. Everything after the table lookup runs in float, and the result is clamped and quantised to 8 bits once. Alpha is never changed. Parameter values are clamped to their ranges when evaluated. Offline-media placeholders are not graded.
- **Curves** (`render/ColorCurves.h`): monotone cubic Hermite splines with Fritsch–Carlson tangents. They pass through every point, are monotone wherever the points are, and never overshoot (tested on sharp steps). Tone curves are flat outside their first and last points. Hue curves wrap around.
- **LUTs** (`render/Lut.h`): `.cube` files (Adobe/Resolve format). 3D tables (2–128 per axis) use tetrahedral interpolation, and 1D tables (2–65536) use linear interpolation per channel. `DOMAIN_MIN/MAX` and `LUT_3D/1D_INPUT_RANGE` are honoured; input outside the domain is clamped. Other vendor keywords are ignored. Malformed files are rejected with the reason and the line number. LUT files are referenced by path like media: they are saved with a path relative to the project and resolved again when the project folder moves, and `relinkLut` repoints every use. Each renderer keeps a `LutCache` that reloads a file when its size or modification time change.
- **Output LUT**: a timeline can have one LUT applied to the finished composite, after all clip grades. *Bypass All Grades* does not affect it.
- **Missing or broken LUTs**: viewing renders without that LUT and logs one warning. Export checks the LUTs that would render first (`checkTimelineLuts`) and refuses with the file's name, because delivering silently wrong colours is worse than failing.
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

- **Decoding and encoding are 8-bit.** Sources are converted to RGBA8 by swscale, so 10/12-bit and HDR/log footage loses precision before the float pipeline starts. Exports are 8-bit 4:2:0: a PQ/HLG export is correctly tagged and converted, but at 8 bits it bands and is not a deliverable HDR master.
- **No display transform**: the viewer shows output-space values as they are. On an SDR monitor, PQ, HLG and log outputs look dim or flat. There is no tone mapping or gamut mapping anywhere.
- **OpenColorIO / ACES**: not integrated. OpenColorIO is not installed in the build environment, and no untested integration was added.
- Scopes analyse the displayed 8-bit output image, not the float working space.
- Projects from before format v9 treat untagged still images as sRGB now, so stills in old projects render slightly differently (converted into the Rec.709 gamma 2.4 timeline space).
- Grade curves are not animated (only the numeric grade parameters are keyframable).
- CPU compositing only. There are no blend modes besides "over", no anchor point or motion blur, and no video effects besides the primary grade.
- **swscale buffers**: its SIMD paths read and write past the end of rows that are not a multiple of 64 bytes. All conversions therefore go through padded, 64-byte-aligned scratch memory (`ffmpeg::alignedStride`). This fixed heap corruption at preview widths such as 120 px; the regression test `CodecTest.ConvertsToAndFromAwkwardSizes` is clean under valgrind.
- **Channel layouts** (fixed in session 12): `AudioDecoder::open` copied a channel layout into an uninitialised `AVChannelLayout`, and FFmpeg frees the destination first, so stack garbage was occasionally freed. It crashed `Mixing.TrackPanGainAndMeters` intermittently in Release builds (4 of 60 repeats). The layouts are now zero-initialised; 60/60 repeats pass. There is no deterministic regression test, because the failure depends on stack contents.
- While **stopped**, the frame at the playhead is rendered on the UI thread. During playback, rendering happens on the engine's worker thread.
- Playback is CPU-only: one video worker, no GPU and no frame cache, so heavy timelines drop frames rather than stutter.
- Still images: supported by the model (unbounded clips) but not yet covered by tests.
