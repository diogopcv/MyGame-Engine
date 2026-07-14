# Phase 7 — Audio

> Status: Expanded 2026-07-14. Not started.
> Backend decision made at expansion time: **WASAPI from scratch** (not
> miniaudio) — see Tech decisions. Record it as an ADR (audio backend +
> real-time threading contract) when the first milestone lands.

## Goal
An audio engine: a fixed mixing graph (voices → SFX/music buses → master),
3D spatialization with distance attenuation, in-memory and streamed sounds,
and basic DSP (volume, pitch, one reverb send) — ending with positional
sound effects and streamed music playing in the sandbox scene, attenuated
by distance and routed through separate buses.

## Scope
- In scope: WASAPI shared-mode, event-driven output device with a dedicated
  audio render thread; a lock-free game→audio command queue; a voice pool
  with per-voice gain/pitch/pan; fixed buses (master, SFX, music) plus one
  reverb send/return; f32 internal mixing; WAV loading (hand-written RIFF
  parser) for in-memory sounds; OGG Vorbis streaming via vendored
  stb_vorbis with job-system chunk decoding; a listener + 3D spatialization
  (constant-power stereo panning from listener-relative azimuth, distance
  attenuation curves with min/max distance); per-voice pitch via playback-
  rate resampling; a simple Freeverb-style reverb on the send bus; a null
  backend so tests run on machines with no audio device; handle-based
  public API (`SoundId`, `VoiceId` via `core/handle.h`).
- Out of scope (deferred, not speculative): HRTF/binaural rendering;
  occlusion/obstruction (needs Phase 9-style raycast glue and a scene that
  wants it); Doppler (nothing in the sandbox moves fast enough to hear it);
  surround / >2 output channels; WASAPI exclusive mode; a data-driven mixer
  graph (fixed buses until content demands routing flexibility — same
  "plain C++ until churn demands more" reasoning as ADR-0009); audio
  compression formats beyond OGG; a master limiter beyond a hard clip
  guard (open question below).

## Module layout
New `engine/audio` module (`ge_audio`), **Layer 3 (Subsystems)** — same
layer as `ge_gfx`/`ge_physics`/`ge_anim`. Depends on `ge_core` only; never
sideways on `ge_world`/`ge_gfx`/`ge_physics` (golden rule 1). WASAPI lives
only in implementation files, mirroring the `rhi_dx12.cpp` precedent
(golden rule 2 — no `<windows.h>` in public headers):

- `include/audio/sound.h` + `src/sound.cpp` — `Sound`: immutable sample
  data (f32, interleaved) + metadata (channels, sample rate, frame count,
  streamed flag). `load_wav()` (hand-written RIFF/fmt/data parser — PCM16
  and f32) and `open_ogg_stream()` (stb_vorbis, vendored PRIVATE, same
  wrapping rule as cgltf/stb_image). Milestones 2 and 4.
- `include/audio/spatial.h` + `src/spatial.cpp` — pure math, fully
  unit-testable with no device: `Listener` (position + forward/up),
  `attenuate(curve, dist, min_d, max_d)` for Linear/Inverse/Exponential
  curves, `pan_stereo(azimuth) -> (gain_l, gain_r)` constant-power law
  (gain_l² + gain_r² == 1). Milestone 3.
- `include/audio/mixer.h` + `src/mixer.cpp` — `Mixer`: voice pool
  (fixed capacity, e.g. 64), bus array, `mix(f32* out, u32 frames)` — a
  pure function of mixer state, callable headless in tests. All state owned
  by the audio thread. Milestone 2.
- `include/audio/dsp.h` + `src/dsp.cpp` — gain application, linear
  resampler (pitch/rate conversion), Freeverb-style reverb (parallel combs
  + series allpasses, fixed tuning first). Milestones 2 and 5.
- `include/audio/audio_system.h` + `src/audio_system.cpp` — the facade the
  app talks to: `init/shutdown`, `load_sound`, `play(SoundId, params) ->
  VoiceId`, `stop`, `set_voice_params`, `set_bus_gain`, `set_listener`,
  `update()` (pumps stream decode requests to the job system). Game-thread
  calls translate to commands on the ring; nothing touches mixer state
  directly. Milestone 1.
- `src/device_wasapi.cpp` — WASAPI bring-up (IMMDeviceEnumerator →
  IAudioClient shared mode, event-driven, f32 mix format), the audio
  thread loop (wait event → drain command ring → `mix()` → fill
  IAudioRenderClient buffer). Milestone 1.
- `src/device_null.cpp` — null backend: a thread that consumes `mix()`
  output at real-time rate (or as fast as tests ask). Keeps every CTest
  runnable on headless/CI machines — the same reason the gfx tests render
  offscreen. Milestone 1.

**Sibling isolation.** Emitter positions live in the world (Layer 4) and
the listener follows the camera; `ge_audio` sees only plain `vec3`/floats.
The glue — a sandbox-side emitter struct + a system that pushes positions
into `AudioSystem` each tick — lives in the sandbox, exactly like the
physics and animation glue (ADR-0009 precedent). No new coupling.

## Tech decisions
- **WASAPI from scratch, not miniaudio** (decided 2026-07-14). The engine
  already does raw Win32 and raw DX12; the audio device is the same class
  of problem, and the interesting parts (mixing, spatialization, DSP) are
  hand-written either way. miniaudio would add a third-party layer in the
  real-time path for plumbing we're capable of owning. Cost accepted:
  device-loss handling and format negotiation are ours. Decoding is
  different — OGG is a *solved, non-core* problem, so stb_vorbis is
  vendored per `docs/third-party-libraries.md` (same category as
  stb_image); WAV is trivial enough to parse by hand.
- **Dedicated audio thread + SPSC command ring.** The WASAPI event
  callback has a hard real-time budget, so the audio thread never locks,
  never allocates, and never touches files. The game thread enqueues
  fixed-size commands (play/stop/set-param/listener) into a single-
  producer single-consumer ring; the audio thread drains it at the top of
  each device period. Sound sample data is immutable after load; frees go
  through a deferred-release queue drained by the audio thread. The thread
  itself is `std::thread`, same as the Phase 2 job system.
- **Internal format: f32 interleaved stereo at the device mix rate.**
  In-memory sounds are converted/resampled once at load time; streamed
  sounds are resampled at play via the linear resampler (which also
  implements pitch — pitch is just a rate ratio).
- **Streaming model.** Each streaming voice owns N (3) chunk buffers of
  ~4096 frames. The audio thread consumes filled chunks; `update()` on the
  game thread notices low buffers and kicks decode jobs (Phase 2 job
  system) that fill them and flip an atomic "ready" flag. Starvation
  plays silence and increments a counter — never blocks.
- **Fixed bus graph.** master ← {SFX, music, reverb-return}; every voice
  routes to one bus plus an optional reverb send level. Hand-rolled, not
  data-driven (ADR-0009 reasoning).
- **Spatialization = constant-power pan + distance attenuation.** Azimuth
  from the listener-space direction (left-handed, per ADR-0005); elevation
  ignored for stereo. Attenuation curves clamp at min/max distance.
- **Memory.** One `ge_audio` permanent arena for sounds/voices/ring
  storage, sized at init (ADR-0004). Zero allocation on the audio thread.
- **ADR.** The backend choice + real-time threading contract is
  cross-cutting enough to record: write **ADR-0011** when Milestone 1
  lands.

## Design
Game thread, per frame:

```
sandbox emitter system: world positions + camera -> set_listener / set_voice_params
AudioSystem::update(): check stream buffers -> kick decode jobs (job system)
play()/stop()/set_bus_gain() -> fixed-size commands -> SPSC ring
```

Audio thread, per device period (WASAPI event):

```
drain command ring -> apply to mixer state
for each active voice:
    fetch frames (in-memory cursor | next stream chunk; starved -> silence)
    resample (pitch) -> voice gain -> spatialize (pan x attenuation)
    accumulate into bus buffer (+ reverb send accumulate)
reverb: process send buffer -> return into master
master: sum buses -> bus/master gains -> hard clip guard -> IAudioRenderClient
```

## Milestones
1. **Device bring-up + command ring.** WASAPI init/teardown, audio thread,
   null backend, SPSC ring; a played 440 Hz generated sine is audible.
   Smoke test on the null backend: init → N periods → clean shutdown, zero
   starvation, zero ring overflows.
2. **Mixer core (headless).** `Sound` + WAV parser, voice pool, buses,
   gains, resampler; `mix()` as a pure function. Unit tests on known
   buffers (see test plan).
3. **3D spatialization.** Listener, attenuation curves, constant-power
   pan; sandbox glue system; a looping positional emitter on the
   DamagedHelmet prop audibly pans/attenuates as the player moves.
4. **Streaming.** stb_vorbis vendored; chunk decode on the job system;
   a music track streams on the music bus in the sandbox.
5. **DSP + sign-off.** Pitch as playback rate, reverb send/return, bus
   gains on CVars for live tuning; listening pass and phase DoD sign-off.

## Test plan
Per plan.md's Phase 7 critical-test line — mixing + attenuation math, bus
gain, buffer integrity, and a listening pass:
- **Unit (spatial math):** attenuation curve values at known distances,
  including clamping at min/max and the min==max degenerate case; pan law
  gains satisfy gain_l² + gain_r² == 1 across the azimuth sweep, with
  exact values at center/hard-left/hard-right.
- **Unit (mixer):** mixing two known constant buffers sums exactly; voice
  gain and bus gain each scale the output by the expected factor and
  compose multiplicatively; a stopped voice contributes exact silence; the
  clip guard bounds output to [-1, 1].
- **Unit (buffer integrity):** a generated linear-ramp waveform played
  through the in-memory path and through the chunked streaming path
  arrives with no gaps, repeats, or reordering (compare against the
  analytic ramp — this catches off-by-one chunk-boundary bugs); WAV
  parser round-trip on a generated PCM16 + f32 file; resampler output on
  a known ramp matches the analytic interpolation.
- **Unit (ring):** SPSC command ring — FIFO order preserved, full-ring
  rejects cleanly, and a producer/consumer stress pass (job-system
  threads) loses no commands.
- **Smoke:** null-device boot → N periods → clean shutdown, zero
  starvation/overflow counters (runs headless in CI); a WASAPI variant of
  the same, skipped gracefully when no endpoint exists.
- **Manual:** listening pass — positional pan/attenuation while circling
  an emitter, bus separation (mute SFX vs music independently), reverb
  send audible, no clicks on start/stop/seek. Recorded sign-off, per
  plan.md.

## Open questions
- Device loss / default-endpoint change: Milestone 1 handles it by
  reinitializing the device and continuing (voices keep state); full
  IMMNotificationClient hot-swap polish only if reinit proves audibly
  disruptive.
- Master limiter: ship with a hard clip guard; add a soft-knee limiter
  only if the listening pass finds audible clipping in real content.
- Click-free voice stop: probably a 2–5 ms fade-out ramp on stop rather
  than a hard cut — decide by ear in Milestone 2.
- Whether an `AudioEmitter` component belongs in `ge_world`'s component
  set or stays sandbox-local — follow the Phase 5 `RigidBodyRef`
  precedent (sandbox-local first, promote when a second consumer exists).

## Cross-phase watch items
- Phase 11 will cook WAV/OGG into an engine-native format; the loaders
  here should keep parse-from-bytes entry points so the cooked path can
  reuse them offline.
- Decode jobs share the Phase 2 job system with everything else; if music
  ever stutters under load, that's the trigger for job priorities — don't
  build them speculatively.
- The command-ring pattern (lock-free game→worker handoff) is the same
  shape Phase 10's net thread will want; keep the ring generic in
  `ge_core` if a second consumer appears.

## Depends on
- Phase 2 (job system for stream decode, `HandleTable` for ids, arenas,
  CVars for bus gains).
- Phase 4/5 (world/physics — emitter and listener positions come from
  entity transforms via sandbox glue).
- Phase 0 (math: vec3, the listener basis).
