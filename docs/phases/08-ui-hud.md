# Phase 8 — UI / HUD

> Status: Expanded **ahead of schedule** 2026-07-14 (full pre-expansion
> requested, deliberately overriding the template's just-in-time rule).
> Re-validate scope and tech decisions against the codebase when this phase
> actually begins — phases landing in between may reshape what this design
> assumes. Not started.

## Goal
An in-game UI/HUD system built on the engine's own renderer: batched quad +
font-atlas text rendering, an anchor-based layout pass, input routing with
focus, and core widgets — ending with an interactive HUD and pause menu in
the sandbox, plus Dear ImGui integrated for debug/editor overlays.

## Scope
- In scope: a screen-space 2D render path in `ge_gfx` (dynamic vertex data
  from the ADR-0010 upload ring, orthographic pixel-space projection, alpha
  blending, scissor rects); font atlas baking via vendored stb_truetype +
  text measurement/drawing; a retained widget tree with a per-frame layout
  pass (anchors + offsets, vertical/horizontal stack containers); input
  routing (top-most hit-test, keyboard focus, char input); core widgets —
  Panel, Label, Image, Button, Slider, TextInput; HUD anchoring to screen
  edges/corners; Dear ImGui vendored for debug UI only (explicitly allowed
  by the third-party policy — the *game* UI is the engine's own system).
- Out of scope (deferred): a visual UI editor (Phase 13 territory at the
  earliest); style/skin data files (hardcoded style struct first);
  animations/transitions; rich text and text shaping (kerning pairs yes,
  BiDi/ligatures no); localization; gamepad focus navigation (no gamepad
  input yet); scroll views and clipping hierarchies beyond one scissor per
  widget; SDF text rendering (plain alpha atlas until scaling artifacts
  hurt); DPI scaling (open question below).

## Module layout
New `engine/ui` module (`ge_ui`), **Layer 4 (Frameworks)** — the
architecture diagram places UI/HUD next to world/scene. Depends downward on
`ge_core`, `ge_gfx` (to submit draw data), and `platform` (key codes,
mouse state types); never on `ge_world` (sibling in the same layer — UI
and the world meet only in the application).

- `include/ui/draw_list.h` — `UiDrawList`: vertices (pos/uv/color),
  indices, and commands (texture handle + scissor rect). Pure data; this
  is the only thing that crosses into `ge_gfx`.
- `include/ui/font.h` + `src/font.cpp` — font atlas bake (stb_truetype,
  vendored PRIVATE per the wrapping rule), glyph metrics, `measure_text`,
  `draw_text` into a draw list.
- `include/ui/layout.h` + `src/layout.cpp` — `Rect`, `Anchor` (parent-
  relative min/max anchors + pixel offsets, Unity-RectTransform-style),
  stack containers; `layout_pass(root)` resolves every widget to a final
  screen rect. Pure math, fully unit-testable.
- `include/ui/widgets.h` + `src/widgets.cpp` — the widget tree (fixed
  arena-allocated nodes, parent/child indices — data-oriented, no virtual
  hierarchy; a widget is a struct with a `WidgetType` enum + per-type
  payload union) and per-type build/draw functions.
- `include/ui/input.h` + `src/input.cpp` — `UiInputState` (mouse pos,
  buttons, wheel, chars, key edges) fed by the app each frame; hit-test
  (children over parents, later siblings on top), hover/pressed/focus
  state, focus keyboard routing to the focused widget.
- `include/ui/ui_context.h` — the facade: owns the tree, style, fonts;
  `begin_frame(input) → build/tick widgets → layout_pass → draw(draw_list)`.

`ge_gfx` additions: a `ui.hlsl` pixel/vertex pair (pos/uv/color, one
texture SRV), a UI pipeline (alpha blend, no depth), scissor-rect support
in the RHI command recording, and `Renderer::submit_ui(const UiDrawData&)`
drawn after the 3D passes. The same path renders ImGui's draw data —
ImGui's backend is ~one file in the sandbox/app layer translating
`ImDrawData` to the same submit call, so `ge_gfx` never includes ImGui
headers.

## Tech decisions
- **Retained tree, immediate-feeling API.** The game UI keeps a persistent
  widget tree (HUD elements live across frames; layout only re-resolves on
  change or resize), but building/mutating it is plain C++ calls — no
  data-driven UI files yet (ADR-0009 reasoning). ImGui stays debug-only so
  the game UI never depends on a third-party look/lifecycle.
- **Pixel-space, top-left origin** for UI coordinates (matches every UI
  convention); the ortho projection maps it to NDC in the vertex shader.
- **One draw call per texture/scissor change**, vertices re-uploaded per
  frame through the upload ring — UI vertex volume is tiny next to the 3D
  scene; no persistence optimization until profiling says otherwise.
- **Fonts: stb_truetype** (vendored, solved non-core problem, same
  category as stb_image). One atlas per font+size baked at load into an
  RHI texture; no dynamic atlas growth until a real font size set needs it.
- **Text input** needs UTF-16 char events from the OS: `platform::Window`
  gains a small char queue (WM_CHAR) — the one platform addition this
  phase makes (platform stays `std::` types per golden rule 3).
- **Widget identity** is the node itself (retained tree), so no ID-hashing
  scheme is needed — the usual immediate-mode ID pitfalls don't apply.

## Design
Per frame (sandbox glue):

```
platform window -> UiInputState (mouse, chars, keys)
game state -> widget mutations (health text, menu open?)
ui_context.begin_frame(input):
    input pass: hit-test top-most -> hover/pressed/click events -> focus
    tick widgets (button fires callback, slider drags, text input edits)
    layout_pass: anchors + stacks -> final rects (only if dirty)
    draw pass: widgets -> UiDrawList (quads, text, scissors)
renderer.submit_ui(draw_list)          // after 3D passes
imgui backend: ImDrawData -> submit_ui  // debug overlays, same path
```

## Milestones
1. **2D render path.** `ui.hlsl`, UI pipeline, scissor in the RHI,
   `submit_ui`; colored/textured quads render over the 3D scene. Smoke
   test: N frames with UI quads, 0 debug-layer errors.
2. **Text.** stb_truetype vendored, atlas bake, measure + draw; FPS/debug
   text drawn in the sandbox corner (replacing console prints).
3. **Layout.** Anchors, offsets, stack containers, dirty tracking; unit
   tests on hand-computed layouts.
4. **Input + widgets.** Hit-testing, hover/press/click, focus, char
   routing; Panel/Label/Image/Button/Slider/TextInput working in a test
   menu. Unit tests on hit/focus routing.
5. **HUD + ImGui + sign-off.** Sandbox HUD (speed readout, anim state,
   crosshair-style anchor demo) + a pause menu (buttons + a volume slider
   driving Phase 7 bus gains); ImGui vendored + backend for debug
   overlays; manual look/feel review; phase DoD sign-off.

## Test plan
Per plan.md's Phase 8 critical-test line — layout math, hit-testing and
focus routing, and a look/feel review:
- **Unit (layout):** anchor resolution against hand-computed rects
  (corner-anchored, stretched, center-anchored with offsets); nested
  containers; stack layout with mixed fixed/fill children; resize
  re-resolution yields the same result as a fresh layout (idempotency).
- **Unit (hit-test/focus):** overlapping widgets — top-most (later
  sibling, deeper child) wins; disabled/invisible widgets are skipped;
  click sets focus, Escape/click-away clears it; char events reach only
  the focused TextInput; slider drag math (pixel → value, clamped).
- **Unit (text):** `measure_text` on a known atlas matches summed
  advances; wrapping is out of scope so no wrap cases.
- **Smoke:** UI + 3D scene render N frames, 0 debug-layer errors.
- **Manual:** look/feel review (hover/press feedback, slider feel, text
  caret behavior, HUD anchoring across a window resize) — recorded
  sign-off, per plan.md.

## Open questions
- DPI scaling: ignore (1:1 pixels) until it's visibly wrong on a high-DPI
  display — then a single ui_scale factor applied at layout time is the
  likely answer.
- Whether the pause menu pauses the fixed-timestep simulation or only
  gameplay input — decide when wiring the sandbox (gameplay concern, not
  UI-system concern).
- Golden-image test for a reference UI screen: nice determinism win, but
  text antialiasing may differ across driver texture filtering — evaluate
  once Milestone 2 exists; not a phase gate.

## Cross-phase watch items
- Phase 13's editor uses ImGui (docking) heavily — the ImGui vendoring +
  backend built here is deliberately reusable there (keep the backend free
  of sandbox-specific code).
- Phase 12's console overlay wants exactly this text + quad path; keep
  `UiDrawList` consumable without the widget tree.
- Font atlas baking is cooker bait (Phase 11): keep the bake step separable
  from the load step.

## Depends on
- Phase 3 renderer + ADR-0010 (upload ring, `BindingLayout`) for the 2D
  pass.
- Phase 4 (`ge_input` for action mapping; platform keyboard/mouse state).
- Phase 2 (arenas for the widget tree, `HandleTable` for font/texture
  handles).
