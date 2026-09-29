# Timeline engine

## Time model

- A timeline has an exact frame rate (`Rational`, e.g. `30000/1001`), a resolution and an audio sample rate.
- All timeline positions and lengths are **integer frames at the timeline rate** (`FrameIndex`). Edits are frame-accurate by construction, with no floating-point drift.
- A clip maps timeline range `[start, start+duration)` to source range `[sourceIn, sourceIn+duration)`, both in timeline frames. Source frame `n` is displayed at `n / timelineRate` seconds into the media. Sources with a different rate show the frame on screen at that instant (nearest-previous), which is standard NLE conform behaviour.
- `sourceLength` caches the usable media length in timeline frames and bounds trims and slips. `0` means unbounded (stills).
- Audio sample positions are derived exactly: `frameToSample(f) = f·den·sampleRate / num`. Per-frame sample counts never drift (e.g. 1601/1602 samples at 29.97 fps).
- Timecode formatting and parsing supports drop-frame for 29.97/59.94.

## Invariants (`Timeline::validate`)

On every track, clips are sorted, never overlap, have duration ≥ 1, start ≥ 0 and stay within source bounds. All ids are unique.

## Operations (`timeline/EditOperations.h`)

Every operation is **all-or-nothing**: it runs on a copy, validates, and commits only on success. Locked tracks reject edits (`LOCKED`).

| Operation | Behaviour |
|---|---|
| `placeClip(..., Overwrite)` | Clears the destination range (splitting straddling clips) and places the clip |
| `placeClip(..., Insert)` | Splits at the insert point and shifts later clips right by the clip length |
| `appendClip` | Places after the last clip on the track |
| `razor` | Splits a clip strictly inside it; the right piece gets a new id (and a shared new link id when provided) |
| `lift` | Removes a clip, leaving a gap |
| `rippleDelete` | Removes a clip and shifts later clips left |
| `trim(Normal)` | Moves one edge; cannot overlap neighbours or exceed media |
| `trim(Ripple)` | Moves one edge and shifts later clips to follow (for the in-edge the clip keeps its position and its head is trimmed) |
| `roll` | Moves the edit point between two adjacent clips |
| `slip` | Changes the source range only |
| `slide` | Moves a clip while trimming its adjacent neighbours so the overall length is unchanged |
| `moveClip` | Moves to a position/track of the same kind with overwrite semantics |
| `insertGap`, `clearRange` | Building blocks for sync-locked edits |

## Session-level behaviour (`EditorSession`)

- `placeMedia` creates linked video and audio clips. **Insert** opens a gap on every unlocked track first, so all tracks stay in sync.
- `razorAt` cuts every clip under the playhead on unlocked tracks. Linked right-hand pieces stay linked to each other.
- Lift, ripple delete, trim, slip, slide and move also apply to linked partners on unlocked tracks. A trim applies to partners whose edge lines up with the edited edge. A roll applies to partners that also have an adjacent clip.
- Every call is one undo step.

## Tests

`tests/unit/TimelineOpsTest.cpp` has hand-checked expectations for each operation plus a seeded random stress test (3,000 random operations). It asserts that the timeline stays valid and that failed operations leave it byte-for-byte unchanged. `tests/integration/SessionTest.cpp` covers linked, sync and undo behaviour.
