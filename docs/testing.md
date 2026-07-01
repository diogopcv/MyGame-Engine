# Testing process

The short definition-of-done and self-review checklist live in `CLAUDE.md`
(read every session). This doc holds the detail behind them — read it when
you're setting up a new module's test target or deciding how much to test.
For the *rationale* behind tiered testing and the framework choice, see
[ADR-0003](decisions/0003-testing-strategy.md).

## Framework & layout

- **doctest** (header-only) for unit tests; GoogleTest if heavier fixtures or
  mocking are needed — pull via CMake `FetchContent` or vendor under
  `third_party/`.
- Each module owns its tests in `engine/<module>/tests/`, building a
  `ge_<module>_tests` executable registered with `enable_testing()` + CTest.
- Tiers (full rationale in ADR-0003): unit, simulation-baseline, golden-image,
  smoke, manual.

## Build to be testable

Prefer free functions and explicit dependencies over hidden globals; keep
simulation deterministic and RNG seedable; abstract the clock so tests can
feed time. Keep the DX12 debug layer + DRED and a debug leak-tracking
allocator (or ASan) on in Debug builds as always-on correctness nets.
