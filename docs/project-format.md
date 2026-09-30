# Project format (`.uproj`)

A `.uproj` is UTF-8 JSON. The format is versioned by the integer `formatVersion` (current: **9**, `Project::kFormatVersion`).

```jsonc
{
  "format": "ultimatepost.project",
  "formatVersion": 6,
  "project": {
    "id": "…", "name": "…", "createdAt": "2026-09-29T17:40:00Z", "modifiedAt": "…",
    "settings": { "frameRate": "25/1", "width": 1920, "height": 1080, "sampleRate": 48000 },
    "activeTimelineId": "…"
  },
  "bins":  [ { "id": "…", "name": "Master", "parentId": "" } ],
  "media": [ {
      "id": "…", "name": "shot01.mov",
      "path": "/abs/path/shot01.mov",           // last known absolute location
      "relativePath": "../media/shot01.mov",     // relative to the .uproj folder
      "binId": "…", "rating": 0, "keywords": [], "comment": "", "importedAt": "…",
      "info": { "container": "mov,mp4,…", "durationSeconds": 12.5, "fileSize": 123,
                "hasVideo": true, "videoCodec": "h264", "width": 3840, "height": 2160,
                "frameRate": "24000/1001", "pixelFormat": "yuv420p", "isStill": false,
                "hasAudio": true, "audioCodec": "aac", "sampleRate": 48000, "channels": 2,
                "timecode": "" },
      "markIn": 1.5, "markOut": 3.25,             // source marks in seconds, or null
      "colorSpace": null                           // override, e.g. "awg3/logc3"; null = detected from tags
  } ],
  "timelines": [ {
      "id": "…", "name": "Timeline 1", "frameRate": "25/1", "width": 1920, "height": 1080, "sampleRate": 48000,
      "markIn": 12, "markOut": null,                    // record marks in frames, or null
      "targets": { "video": "<track id>", "audio": "" },  // source patching; "" = stream disabled
      "markers": [ { "id": "…", "frame": 25, "duration": 0, "name": "Scene 2",
                     "comment": "", "color": "yellow" } ],  // timeline frames, sorted
      "outputLut": null,                                // or { "path": …, "relativePath": … }
      "gradesBypassed": false,
      "colorSpace": "rec709/bt1886",                    // timeline colour space: "<primaries>/<transfer>"
      "outputColorSpace": null,                         // null = same as the timeline
      "tracks": [ {
          "id": "…", "kind": "video" | "audio", "name": "V1",
          "enabled": true, "locked": false, "muted": false, "solo": false, "gainDb": 0,
          "pan": 0,                                      // -1 left .. 1 right (audio tracks)
          "effects": [ { "id": "…", "type": "eq3", "enabled": true,
                         "params": { "lowGain": 3, "lowFreq": 120 } } ],   // insert chain, in order
          "clips": [ { "id": "…", "mediaId": "…", "name": "…",
                       "start": 0, "duration": 50, "sourceIn": 0, "sourceLength": 50,
                       "linkId": "…", "enabled": true, "gainDb": 0,
                       "markers": [ … ],                      // clip markers: source frames
                       "transform": {                          // only non-default parameters
                         "scale": { "value": 50, "keys": [] },
                         "opacity": { "value": 100, "keys": [
                           { "frame": 0, "value": 0, "interpolation": "ease" },
                           { "frame": 40, "value": 100, "interpolation": "linear" } ] } },
                       "grade": {                              // video clips; only non-default parameters
                         "saturation": { "value": 0.5, "keys": [] },
                         "gainR": { "value": 1.2, "keys": [ { "frame": 10, "value": 1.2, "interpolation": "linear" } ] },
                         "curves": { "master": [[0, 0.05], [1, 0.95]],       // only non-empty curves; [x, y] 0..1
                                     "hueVsSat": [[0.35, 0.2]] },
                         "lut": { "path": "/looks/film.cube", "relativePath": "../looks/film.cube" } },
                       "gradeBypass": false,
                       "gradeVersion": "B",                    // name of the active grade above
                       "gradeVersions": [ { "name": "A", "grade": { … } } ],   // stored, inactive versions
                       "transitionIn": { "kind": "dissolve", "duration": 25, "alignment": "center" },
                       "transitionOut": null } ]
      } ]
  } ]
}
```

## Rules

- Rates are exact rationals written as `"num/den"`.
- Clip positions are integer frames at the owning timeline's rate (see [timeline.md](timeline.md)).
- Media is referenced, never embedded. On load, each media item is resolved by absolute path first, then by `relativePath` against the project folder, so a moved project folder keeps working. Items that are not found are marked offline and can be relinked.
- Output is deterministic: saving an unchanged project produces the same bytes (apart from `modifiedAt`).
- Timelines are validated on load. A structurally invalid timeline (overlaps, negative durations, source out of bounds) is rejected with a readable error rather than loaded half-broken.

## Versioning and migrations

`ProjectMigrator::standard()` holds one step per version (N → N+1). A step edits the JSON document in place before it is parsed into the model.
- Files **newer** than the running build are refused (`UNSUPPORTED_VERSION`) and never rewritten.
- To change the format: bump `Project::kFormatVersion`, register a step for the previous version, add a test with a document in the old shape, and update this file.

## Version history

| Version | Change | Migration |
|---|---|---|
| 1 | First format | – |
| 2 | Timeline `markIn`/`markOut` and `targets`; media `markIn`/`markOut` | Targets become the first video and first audio track (how v1 placed media); all marks are null. Covered by `ProjectFormat.MigratesVersion1Documents`. |
| 3 | Timeline and clip `markers` | Empty marker lists everywhere. Covered by `ProjectFormat.MigratesVersion2Documents` (and v1 files migrate through both steps). |
| 4 | Clip `transform` (animatable position, scale, rotation, opacity, crop) | Empty transform (identity) on every clip. Covered by `ProjectFormat.MigratesVersion3Documents`. |
| 5 | Clip `transitionIn` / `transitionOut` | Both null on every clip. Covered by `ProjectFormat.RoundTripsTransitionsAndMigratesV4`. |
| 6 | Track `pan` and `effects`; clip `transform` may hold `volume` (dB) and `pan` | Pan 0 and an empty effect chain on every track. Covered by `ProjectFormat.RoundTripsTrackAudioAndMigratesV5`. Invalid effect parameters in a file are rejected on load. |
| 7 | Clip `grade` (primary colour correction: lift/gamma/gain/offset master and RGB, contrast, pivot, saturation, exposure, temperature, tint; all animatable) | An empty grade object (identity) on every clip. Covered by `ProjectFormat.MigratesV6ToGrades`. Parameter ids are listed in `timeline/Grade.cpp`. Unknown ids are ignored, non-finite values are rejected on load, and out-of-range values are clamped when rendering. |
| 8 | Grade `curves` and `lut` (inside `grade`); clip `gradeBypass`, `gradeVersion`, `gradeVersions`; timeline `outputLut`, `gradesBypassed` | Bypass off, one active version named "A" with no stored versions, no output LUT (curves and LUT are simply absent). Covered by `ProjectFormat.RoundTripsCurvesLutsVersionsAndMigratesV7`. Curves must be valid (points in 0..1, distinct x, sorted, tone curves ≥ 2 points) and version names unique per clip, or the file is rejected. LUT paths resolve like media paths: absolute first, then relative to the project file. |
| 9 | Timeline `colorSpace` and `outputColorSpace`; media `colorSpace`; media `info` gains `colorPrimaries`, `colorTransfer`, `colorMatrix`, `colorRange` (FFmpeg tag names) | Timelines become Rec.709 gamma 2.4 with the output in the same space; media overrides are null. Old media info has no tags, so video is detected as Rec.709 gamma 2.4 (pixels pass through unchanged, as before) and stills as sRGB (converted, so they render slightly differently than before). Covered by `ProjectFormat.RoundTripsColorSpacesAndMigratesV8`. Ids are `<primaries>/<transfer>` from `timeline/ColorSpace.cpp`; an unknown id falls back to Rec.709 gamma 2.4 with a logged warning. |

## Safety

- Saves are atomic (temp + fsync + rename) and keep the previous file as `.uproj.bak`.
- Autosaves (`.autosave.uproj`) use the same schema.
