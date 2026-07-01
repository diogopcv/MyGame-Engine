# Third-party libraries

Allowed for **solved, non-core problems only:**
- Asset import: cgltf / assimp
- Image decode: stb_image
- Debug/editor UI: Dear ImGui
- Test framework: doctest / GoogleTest

**Not allowed:** replacing the from-scratch goals (DX12 renderer and physics
solver are hand-written).

Vendor third-party code under `third_party/` and wrap each behind an engine
interface so it never leaks into subsystem APIs.
