# Project format (`.uproj`)

A `.uproj` is UTF-8 JSON. The format is versioned by the integer `formatVersion` (current: **3**, `Project::kFormatVersion`).

```jsonc
{
  "format": "ultimatepost.project",
  "formatVersion": 3,
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
      "markIn": 1.5, "markOut": 3.25              // source marks in seconds, or null
  } ],
  "timelines": [ {
      "id": "…", "name": "Timeline 1", "frameRate": "25/1", "width": 1920, "height": 1080, "sampleRate": 48000,
      "markIn": 12, "markOut": null,                    // record marks in frames, or null
      "targets": { "video": "<track id>", "audio": "" },  // source patching; "" = stream disabled
      "markers": [ { "id": "…", "frame": 25, "duration": 0, "name": "Scene 2",
                     "comment": "", "color": "yellow" } ],  // timeline frames, sorted
      "tracks": [ {
          "id": "…", "kind": "video" | "audio", "name": "V1",
          "enabled": true, "locked": false, "muted": false, "solo": false, "gainDb": 0,
          "clips": [ { "id": "…", "mediaId": "…", "name": "…",
                       "start": 0, "duration": 50, "sourceIn": 0, "sourceLength": 50,
                       "linkId": "…", "enabled": true, "gainDb": 0,
                       "markers": [ … ] } ]                   // clip markers: source frames
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

## Safety

- Saves are atomic (temp + fsync + rename) and keep the previous file as `.uproj.bak`.
- Autosaves (`.autosave.uproj`) use the same schema.
