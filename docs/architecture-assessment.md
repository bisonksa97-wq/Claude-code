# Architecture Assessment (session 1)

This is the assessment the master build prompt asks for before any implementation.

## 1. What existed

| Question | Finding |
|---|---|
| Existing code | One file: `index.html`, a static "digital business card" page. It has nothing to do with post-production and was left untouched. |
| Language / framework | None for this product. Greenfield. |
| Build system | None. |
| Target OS | Not specified in the repo. The prompt targets Windows 11, macOS and Linux. |
| Maturity | Nothing to assess. This session starts the project. |

## 2. Technology decisions

| Concern | Choice | Why |
|---|---|---|
| Core language | **C++20** | Mature media ecosystem (FFmpeg, OCIO, OFX, VST3 are C/C++ APIs), deterministic performance, and a native core with no browser runtime. C++20 keeps us on MSVC, Apple Clang and GCC without relying on C++23 `std::expected`. |
| Build | **CMake ≥ 3.21 + Ninja** | Cross-platform standard; works with vcpkg/Homebrew/apt. |
| Codecs | **FFmpeg (libavformat/avcodec/swscale/swresample)** via pkg-config | Broadest codec coverage. It is hidden behind the `codec` module, so nothing else includes FFmpeg headers. |
| Desktop UI | **Qt 6 Widgets** | Mature native cross-platform toolkit with dockable, floating panels (multi-monitor) and accessibility hooks. The UI is optional in the build, so the engine and CLI build without Qt. |
| Project format | **Versioned JSON (`.uproj`)** through nlohmann-json | Human-diffable, easy to migrate, and the diffs are reviewable. SQLite stays a candidate for large media databases/indices later (see roadmap). |
| Tests | **GoogleTest + CTest**, Qt offscreen platform for UI tests | Standard, discoverable, and runs headless in CI. |
| GPU | Deferred | A GPU abstraction is only worth designing once there is a CPU reference pipeline to compare against. The CPU compositor is that reference (see roadmap Phase 3). |

Considered and rejected for now:
- **Rust core.** It is attractive for safety, but the FFmpeg, Qt, OFX and VST3 bindings would add friction on day one. It can still be introduced later for isolated services (e.g. a plugin host process).
- **Electron/web UI.** The prompt forbids the core depending on a browser runtime, and a native UI avoids a second runtime.

## 3. Resulting layering

```
ui (Qt)   cli
   \      /
     app            EditorSession: the only mutation entry point (commands, undo)
   /  |   \
media render project
  |   /  \   |
 codec  timeline
     \   /
      core           Result/Error, logging, time, commands, atomic IO
```

- The UI never touches decoders or media buffers. It asks `render::FrameCompositor` for frames and `EditorSession` for edits.
- The CLI uses the same `EditorSession`, which shows the engine works without the UI (automation, remote and cloud rendering can follow).

## 4. First vertical slice

The target is the §90 workflow: create project → import → show media → drag to timeline → play → cut → trim → add second clip → add audio → save → reopen → export.
It is implemented and exercised by `tests/integration/VerticalSliceTest.cpp` (engine) and `tests/ui/UiSmokeTest.cpp` (widgets).
See [feature-status.md](feature-status.md) for exactly what works.
