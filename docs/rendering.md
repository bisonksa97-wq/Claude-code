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

## Determinism

For identical inputs and settings, compositing and mixing are deterministic. Encoders may vary across FFmpeg versions, so golden tests compare **decoded content with tolerances** (average colour per edit, audio RMS per region), not bytes.

## Known limitations

- 8-bit RGBA, no colour management (Rec.709 assumed by swscale defaults).
- No blending, transforms, transitions or effects.
- Viewer rendering happens on the UI thread (fine at preview resolution for simple timelines; see roadmap).
- Still images: supported by the model (unbounded clips) but not yet covered by tests.
