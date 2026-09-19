# soundscape-native-linux — design notes

The native client is a **full client** of the Soundscape api (`:3021`): it lists stations, starts/stops the radio,
pulls songs, plays them with a crossfade, analyses the mix itself and renders the visualizer with **raw Vulkan**.
The web UI is not needed while it runs. It is the "hella cool end state" the plan reserved for later
(`soundscape/docs/plan.md` §10.3) and consumes the same inputs the web visualizer publishes
(`docs/visual-feed.md`), so scenes port 1:1.

## Modules (src/)

| File | Role | Ported from |
|---|---|---|
| `api.*` | blocking libcurl + nlohmann/json client, models (Song, Station, RadioStatus, Playlist) | `web/lib/api.ts` |
| `audio.*` | miniaudio playback device, two decks decoding FLAC from memory, linear gain ramps, master volume, mono tap | WebAudio graph in `web/lib/player.ts` |
| `player.*` | RadioPlayer: 250 ms tick, `nextStartAt`, crossfade, stuck watchdog, fetch that always clears | `web/lib/player.ts` |
| `analysis.*` | AnalyserNode emulation (Blackman, 1/N, 0.8 smoothing, -100..-30 dB → bytes), `bandEnergies`, `logSpectrum`, `BeatClock`, `paletteFor`, `makeFrame`, `meter` | `web/lib/visual.ts` + Chromium's RealtimeAnalyser |
| `abc.*` | ABC dialect parser → note events → section cues | `web/lib/abc.ts` |
| `vk_context.*` | instance/device/swapchain (sRGB, FIFO), 2 frames in flight, buffers, one-time commands | — |
| `renderer.*` | the `pulse` scene: static triangle soup + 3 instanced quad draws into an RGBA16F offscreen image, then the CRT post pass into the swapchain | `web/lib/scenes.ts` `pulse` + `globals.css` `.crt-*` |
| `ui.cpp` | Dear ImGui side panel (stations / radio / seeds / playlist / profile) + the red telemetry HUD | `RadioPanel.tsx`, `Visualizer.tsx` |
| `app.*` | state, actions, api polling (status every 2 s, stations every 5 s, health every 15 s), main loop | `RadioPanel.tsx` |
| `async.hpp` | detached worker threads + a main-thread queue | — |

Threading: everything touches state on the main thread. Blocking api calls run on detached threads via
`async_call` and post their result back through `MainQueue`. The one callback that runs off-thread is the player's
`onNeedNext` (it must block until the api answers); it reads copies of the station id / playlist guarded by `App::wmx`.
The audio callback holds a short mutex while decoding into the mix; deck loads happen on the main thread with the
already-downloaded bytes, so the lock is never held across I/O.

## Rendering

- Camera: the web uses a 60° perspective camera at z = 3.2 looking at z = 0. Everything sits on z = 0, so the
  visible half-height is tan(30°)·3.2 = 1.8475 world units; the vertex shaders scale by `1/(1.8475·aspect), 1/1.8475`.
- Color: hex colors are converted sRGB → linear on the CPU (three.js does the same with ColorManagement on), math is
  linear, the offscreen target is RGBA16F, the swapchain is `B8G8R8A8_SRGB`. The post pass converts to sRGB, applies
  the CSS-equivalent overlays (scanlines multiply 0.58 on two of three rows, a 22 %-tall red band rolling every 7 s
  with screen blending, an elliptical vignette, an inset edge shadow) and converts back.
- Blending: `MeshBasicMaterial` opaque draws use alpha blending with α = 1; the bloom bars and wheel glow use
  additive (`SrcAlpha, One`) like three's `AdditiveBlending`.
- Draw order follows the web `renderOrder`: grid → bloom → segments → ticks → disc → bezel/lines → wheel glow → wheel.
- Per frame the CPU rewrites 3328 segment + 128 bloom + 128 tick instances (40 bytes each) into a persistently
  mapped host-visible buffer (one per frame in flight).

## Not ported yet

- Scenes `radial`, `nebula`, `rings` (the web keeps them; `pulse` is the user-validated look).
- Seed **upload** from a local file (the api takes multipart; the client has no file dialog yet). Links work.
- Library view / playlist export / station deletion.
- A WebSocket bridge to publish `VisualFrame`s to other consumers.
