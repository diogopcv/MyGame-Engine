#include <doctest/doctest.h>
#include "gfx/render_graph.h"

using namespace ge;
using namespace ge::gfx;

namespace {

TextureHandle make_handle(u32 index) {
    return TextureHandle{index, 0};
}

bool called_a = false;
bool called_b = false;

void pass_a(RhiDevice&, void*) { called_a = true; }
void pass_b(RhiDevice&, void*) { called_b = true; }

} // namespace

TEST_CASE("render graph: single pass barrier insertion") {
    RenderGraph graph;

    TextureHandle rt    = make_handle(0);
    TextureHandle depth = make_handle(1);

    graph.begin();
    graph.import(rt, ResourceState::Present);
    graph.import(depth, ResourceState::DepthWrite);

    graph.add_pass("main", nullptr, nullptr);
    graph.pass_write(rt, ResourceState::RenderTarget);
    graph.pass_write(depth, ResourceState::DepthWrite);

    graph.set_final_state(rt, ResourceState::Present);
    graph.compile();

    // Should emit: Present→RenderTarget for rt before the pass,
    // depth stays DepthWrite (no barrier), then RenderTarget→Present for final
    CHECK(graph.barrier_count() == 2);

    const auto* barriers = graph.barriers();
    // First barrier: rt Present → RenderTarget
    CHECK(barriers[0].texture == rt);
    CHECK(barriers[0].before  == ResourceState::Present);
    CHECK(barriers[0].after   == ResourceState::RenderTarget);

    // Second barrier: rt RenderTarget → Present (final export)
    CHECK(barriers[1].texture == rt);
    CHECK(barriers[1].before  == ResourceState::RenderTarget);
    CHECK(barriers[1].after   == ResourceState::Present);
}

TEST_CASE("render graph: no barrier when state matches") {
    RenderGraph graph;
    TextureHandle depth = make_handle(0);

    graph.begin();
    graph.import(depth, ResourceState::DepthWrite);

    graph.add_pass("shadow", nullptr, nullptr);
    graph.pass_write(depth, ResourceState::DepthWrite);

    graph.compile();

    CHECK(graph.barrier_count() == 0);
}

TEST_CASE("render graph: two passes with state transition") {
    RenderGraph graph;

    TextureHandle shadow_map = make_handle(0);
    TextureHandle rt         = make_handle(1);

    graph.begin();
    graph.import(shadow_map, ResourceState::DepthWrite);
    graph.import(rt, ResourceState::Present);

    // Pass 1: shadow — write depth to shadow map
    graph.add_pass("shadow", nullptr, nullptr);
    graph.pass_write(shadow_map, ResourceState::DepthWrite);

    // Pass 2: main — read shadow map as texture, write render target
    graph.add_pass("main", nullptr, nullptr);
    graph.pass_read(shadow_map, ResourceState::PixelShaderResource);
    graph.pass_write(rt, ResourceState::RenderTarget);

    graph.set_final_state(rt, ResourceState::Present);
    graph.compile();

    // Expected barriers:
    // 1. shadow_map: DepthWrite → PixelShaderResource (between shadow and main)
    // 2. rt: Present → RenderTarget (before main)
    // 3. rt: RenderTarget → Present (final)
    CHECK(graph.barrier_count() == 3);

    const auto* b = graph.barriers();
    CHECK(b[0].texture == shadow_map);
    CHECK(b[0].before  == ResourceState::DepthWrite);
    CHECK(b[0].after   == ResourceState::PixelShaderResource);

    CHECK(b[1].texture == rt);
    CHECK(b[1].before  == ResourceState::Present);
    CHECK(b[1].after   == ResourceState::RenderTarget);

    CHECK(b[2].texture == rt);
    CHECK(b[2].before  == ResourceState::RenderTarget);
    CHECK(b[2].after   == ResourceState::Present);
}

TEST_CASE("render graph: pass count tracking") {
    RenderGraph graph;

    graph.begin();
    CHECK(graph.pass_count() == 0);

    graph.add_pass("a", nullptr, nullptr);
    graph.add_pass("b", nullptr, nullptr);
    graph.add_pass("c", nullptr, nullptr);

    CHECK(graph.pass_count() == 3);
}

TEST_CASE("render graph: begin resets state") {
    RenderGraph graph;
    TextureHandle rt = make_handle(0);

    graph.begin();
    graph.import(rt, ResourceState::Present);
    graph.add_pass("a", nullptr, nullptr);
    graph.pass_write(rt, ResourceState::RenderTarget);
    graph.compile();
    CHECK(graph.pass_count() == 1);
    CHECK(graph.barrier_count() == 1);

    // Second frame — everything should be reset
    graph.begin();
    CHECK(graph.pass_count() == 0);
    CHECK(graph.barrier_count() == 0);
}
