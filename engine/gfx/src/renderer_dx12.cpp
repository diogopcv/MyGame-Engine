#include "gfx/renderer.h"

#include "core/log.h"
#include "core/types.h"
#include "core/mat4.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ge::gfx {

namespace {

using Microsoft::WRL::ComPtr;

// Double buffering: two back buffers, two per-frame command allocators, two
// fence values. See docs/phases/01-dx12-triangle.md "Tech decisions".
constexpr u32 kFrameCount = 2;

// Cube vertex format (position + UV), matched by the input layout in
// RendererImpl::create_pipeline() and the HLSL input struct in
// shaders/cube_vs.hlsl.
struct Vertex {
    f32 position[3];
    f32 uv[2];
};

// Constant buffer layout for shaders/cube_vs.hlsl (`row_major float4x4 mvp`).
// CBVs must be 256-byte aligned/sized; the upload buffers are over-sized to
// kConstantBufferSize and only the first sizeof(CubeCB) bytes are written.
struct CubeCB {
    mat4 mvp;
};
constexpr u32 kConstantBufferSize = 256;

bool dx_check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        GE_ERROR("{} failed (hr=0x{:08X})", what, static_cast<u32>(hr));
        return false;
    }
    return true;
}

// Compiled shaders (.cso) are placed next to the executable under shaders/
// by the CMake DXC build step (engine/gfx/CMakeLists.txt).
std::vector<u8> load_shader_bytecode(const wchar_t* filename) {
    wchar_t exe_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path path = std::filesystem::path(exe_path).parent_path() / "shaders" / filename;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        GE_ERROR("Failed to open shader file: {}", path.string());
        return {};
    }

    const auto size = static_cast<usize>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<u8> bytecode(size);
    file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(size));
    return bytecode;
}

} // namespace

struct RendererImpl {
    ComPtr<IDXGIFactory6>             factory;
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<IDXGISwapChain3>           swapchain;
    ComPtr<ID3D12DescriptorHeap>      rtv_heap;
    ComPtr<ID3D12Resource>            back_buffers[kFrameCount];
    ComPtr<ID3D12CommandAllocator>    allocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> cmd_list;
    ComPtr<ID3D12Fence>               fence;
    ComPtr<ID3D12InfoQueue>           info_queue;

    // Pipeline (root signature + PSO).
    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12Resource>      vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW    vertex_buffer_view{};

    // Cube geometry (index buffer) + texture/SRV heap.
    ComPtr<ID3D12Resource>      index_buffer;
    D3D12_INDEX_BUFFER_VIEW     index_buffer_view{};
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    ComPtr<ID3D12Resource>      texture;

    // Milestone 4: per-frame MVP constant buffers (root CBV at b0).
    ComPtr<ID3D12Resource> cb_resources[kFrameCount];
    u8*                    cb_mapped[kFrameCount] = {};

    // Milestone 4: depth buffer (DSV heap holds a single, resize-recreated view).
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    ComPtr<ID3D12Resource>       depth_buffer;

    HANDLE fence_event                = nullptr;
    u64    fence_values[kFrameCount]  = {};
    u64    next_fence_value           = 1;
    u32    frame_index                = 0;
    u32    rtv_descriptor_size        = 0;
    u32    width                      = 0;
    u32    height                     = 0;
    bool   vsync                      = true;
    float  clear_color[4]             = { 0.05f, 0.1f, 0.2f, 1.0f };
    f32    total_time                 = 0.0f;

    void create_rtvs() {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (u32 i = 0; i < kFrameCount; ++i) {
            swapchain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]));
            device->CreateRenderTargetView(back_buffers[i].Get(), nullptr, handle);
            handle.ptr += rtv_descriptor_size;
        }
    }

    // Block the CPU until the GPU has completed everything submitted so far.
    void flush() {
        const u64 value = next_fence_value++;
        queue->Signal(fence.Get(), value);
        if (fence->GetCompletedValue() < value) {
            fence->SetEventOnCompletion(value, fence_event);
            ::WaitForSingleObject(fence_event, INFINITE);
        }
    }

    // Root signature (Milestone 4): a root CBV at b0 (vertex-visible MVP
    // matrix) plus the Milestone 3 descriptor table (one SRV at t0, pixel-
    // visible) and its static sampler at s0, per ADR-0006.
    bool create_root_signature() {
        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = 1;
        srv_range.BaseShaderRegister                = 0;
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER root_params[2]{};
        root_params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_params[0].Descriptor.ShaderRegister = 0;
        root_params[0].Descriptor.RegisterSpace  = 0;
        root_params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

        root_params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[1].DescriptorTable.NumDescriptorRanges = 1;
        root_params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        root_params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        sampler.ShaderRegister   = 0;
        sampler.RegisterSpace    = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters     = _countof(root_params);
        desc.pParameters       = root_params;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers   = &sampler;
        desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature_blob;
        ComPtr<ID3DBlob> error_blob;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                   &signature_blob, &error_blob);
        if (FAILED(hr)) {
            if (error_blob) {
                GE_ERROR("D3D12SerializeRootSignature failed: {}",
                         static_cast<const char*>(error_blob->GetBufferPointer()));
            }
            return false;
        }

        return dx_check(device->CreateRootSignature(0, signature_blob->GetBufferPointer(),
                                                      signature_blob->GetBufferSize(),
                                                      IID_PPV_ARGS(&root_signature)),
                         "CreateRootSignature");
    }

    // Cube pipeline: position+UV input layout, depth test/write enabled
    // (Milestone 4). Back-face culling stays off — the cube is small and the
    // depth buffer alone gives correct results; revisit once a real winding
    // convention is needed for lighting.
    bool create_pipeline() {
        std::vector<u8> vs_bytecode = load_shader_bytecode(L"cube_vs.cso");
        std::vector<u8> ps_bytecode = load_shader_bytecode(L"cube_ps.cso");
        if (vs_bytecode.empty() || ps_bytecode.empty()) {
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC input_elements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode              = D3D12_FILL_MODE_SOLID;
        rasterizer.CullMode              = D3D12_CULL_MODE_NONE;
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthClipEnable       = TRUE;

        D3D12_BLEND_DESC blend{};
        blend.RenderTarget[0].BlendEnable           = FALSE;
        blend.RenderTarget[0].LogicOpEnable         = FALSE;
        blend.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature        = root_signature.Get();
        pso_desc.VS                    = { vs_bytecode.data(), vs_bytecode.size() };
        pso_desc.PS                    = { ps_bytecode.data(), ps_bytecode.size() };
        pso_desc.InputLayout           = { input_elements, _countof(input_elements) };
        pso_desc.RasterizerState       = rasterizer;
        pso_desc.BlendState            = blend;
        pso_desc.DepthStencilState.DepthEnable      = TRUE;
        pso_desc.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
        pso_desc.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_LESS;
        pso_desc.DepthStencilState.StencilEnable    = FALSE;
        pso_desc.DSVFormat              = DXGI_FORMAT_D32_FLOAT;
        pso_desc.SampleMask             = UINT_MAX;
        pso_desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets       = 1;
        pso_desc.RTVFormats[0]          = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count       = 1;

        return dx_check(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)),
                         "CreateGraphicsPipelineState");
    }

    // Generic helper: create an upload-heap buffer and copy `data` into it
    // via a persistently-mapped pointer (Phase 1's "simple upload heap +
    // mapped-pointer copy" decision; a dedicated copy queue/default-heap path
    // comes later).
    bool create_upload_buffer(const void* data, u32 size, ComPtr<ID3D12Resource>& out,
                               const char* what) {
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC res_desc{};
        res_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        res_desc.Width            = size;
        res_desc.Height           = 1;
        res_desc.DepthOrArraySize = 1;
        res_desc.MipLevels        = 1;
        res_desc.Format           = DXGI_FORMAT_UNKNOWN;
        res_desc.SampleDesc.Count = 1;
        res_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (!dx_check(device->CreateCommittedResource(
                           &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc,
                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out)),
                       what)) {
            return false;
        }

        void* mapped = nullptr;
        const D3D12_RANGE no_read{ 0, 0 };
        if (!dx_check(out->Map(0, &no_read, &mapped), "Map (upload buffer)")) {
            return false;
        }
        std::memcpy(mapped, data, size);
        out->Unmap(0, nullptr);
        return true;
    }

    // A unit cube: 24 vertices (4 per face, own UVs spanning [0,1]^2 per
    // face) so each face samples the whole checkerboard texture.
    bool create_vertex_buffer() {
        constexpr f32 k = 0.5f;
        const Vertex vertices[] = {
            // -X
            { { -k, -k, -k }, { 0.0f, 1.0f } },
            { { -k, -k,  k }, { 1.0f, 1.0f } },
            { { -k,  k,  k }, { 1.0f, 0.0f } },
            { { -k,  k, -k }, { 0.0f, 0.0f } },
            // +X
            { {  k, -k, -k }, { 1.0f, 1.0f } },
            { {  k,  k, -k }, { 1.0f, 0.0f } },
            { {  k,  k,  k }, { 0.0f, 0.0f } },
            { {  k, -k,  k }, { 0.0f, 1.0f } },
            // -Y
            { { -k, -k, -k }, { 0.0f, 1.0f } },
            { {  k, -k, -k }, { 1.0f, 1.0f } },
            { {  k, -k,  k }, { 1.0f, 0.0f } },
            { { -k, -k,  k }, { 0.0f, 0.0f } },
            // +Y
            { { -k,  k, -k }, { 0.0f, 0.0f } },
            { { -k,  k,  k }, { 0.0f, 1.0f } },
            { {  k,  k,  k }, { 1.0f, 1.0f } },
            { {  k,  k, -k }, { 1.0f, 0.0f } },
            // -Z
            { { -k, -k, -k }, { 1.0f, 1.0f } },
            { { -k,  k, -k }, { 1.0f, 0.0f } },
            { {  k,  k, -k }, { 0.0f, 0.0f } },
            { {  k, -k, -k }, { 0.0f, 1.0f } },
            // +Z
            { { -k, -k,  k }, { 0.0f, 1.0f } },
            { {  k, -k,  k }, { 1.0f, 1.0f } },
            { {  k,  k,  k }, { 1.0f, 0.0f } },
            { { -k,  k,  k }, { 0.0f, 0.0f } },
        };
        constexpr u32 kBufferSize = sizeof(vertices);

        if (!create_upload_buffer(vertices, kBufferSize, vertex_buffer,
                                   "CreateCommittedResource (vertex buffer)")) {
            return false;
        }

        vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
        vertex_buffer_view.StrideInBytes  = sizeof(Vertex);
        vertex_buffer_view.SizeInBytes    = kBufferSize;
        return true;
    }

    // Two triangles per face (6 faces x 6 indices = 36).
    bool create_index_buffer() {
        u16 indices[36];
        for (u32 face = 0; face < 6; ++face) {
            const u16 base = static_cast<u16>(face * 4);
            const u32 off  = face * 6;
            indices[off + 0] = static_cast<u16>(base + 0);
            indices[off + 1] = static_cast<u16>(base + 1);
            indices[off + 2] = static_cast<u16>(base + 2);
            indices[off + 3] = static_cast<u16>(base + 0);
            indices[off + 4] = static_cast<u16>(base + 2);
            indices[off + 5] = static_cast<u16>(base + 3);
        }
        constexpr u32 kBufferSize = sizeof(indices);

        if (!create_upload_buffer(indices, kBufferSize, index_buffer,
                                   "CreateCommittedResource (index buffer)")) {
            return false;
        }

        index_buffer_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
        index_buffer_view.Format         = DXGI_FORMAT_R16_UINT;
        index_buffer_view.SizeInBytes    = kBufferSize;
        return true;
    }

    // A small procedural checkerboard, uploaded to a default-heap texture via
    // an upload-heap staging buffer + CopyTextureRegion, then a transition to
    // PIXEL_SHADER_RESOURCE. The SRV is staged into srv_heap (shader-visible),
    // matching the descriptor table in the root signature (ADR-0006).
    bool create_texture() {
        constexpr u32 kTexWidth  = 4;
        constexpr u32 kTexHeight = 4;

        u8 pixels[kTexHeight][kTexWidth][4];
        for (u32 y = 0; y < kTexHeight; ++y) {
            for (u32 x = 0; x < kTexWidth; ++x) {
                const bool light = (x + y) % 2 == 0;
                pixels[y][x][0] = light ? 230 : 30;
                pixels[y][x][1] = light ? 230 : 30;
                pixels[y][x][2] = light ? 230 : 30;
                pixels[y][x][3] = 255;
            }
        }

        D3D12_RESOURCE_DESC tex_desc{};
        tex_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex_desc.Width            = kTexWidth;
        tex_desc.Height           = kTexHeight;
        tex_desc.DepthOrArraySize = 1;
        tex_desc.MipLevels        = 1;
        tex_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        if (!dx_check(device->CreateCommittedResource(
                           &default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)),
                       "CreateCommittedResource (texture)")) {
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT   num_rows      = 0;
        UINT64 row_size      = 0;
        UINT64 upload_size   = 0;
        device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &num_rows, &row_size,
                                       &upload_size);

        ComPtr<ID3D12Resource> upload_buffer;
        D3D12_HEAP_PROPERTIES  upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width            = upload_size;
        upload_desc.Height           = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels        = 1;
        upload_desc.Format           = DXGI_FORMAT_UNKNOWN;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (!dx_check(device->CreateCommittedResource(
                           &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                           IID_PPV_ARGS(&upload_buffer)),
                       "CreateCommittedResource (texture upload buffer)")) {
            return false;
        }

        u8* mapped = nullptr;
        const D3D12_RANGE no_read{ 0, 0 };
        if (!dx_check(upload_buffer->Map(0, &no_read, reinterpret_cast<void**>(&mapped)),
                       "Map (texture upload buffer)")) {
            return false;
        }
        for (u32 y = 0; y < kTexHeight; ++y) {
            std::memcpy(mapped + footprint.Footprint.RowPitch * y, pixels[y], kTexWidth * 4);
        }
        upload_buffer->Unmap(0, nullptr);

        // One-time copy + barrier, recorded and submitted right here, then
        // flushed before begin_frame ever resets the command list.
        cmd_list->Reset(allocators[0].Get(), nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource        = texture.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource       = upload_buffer.Get();
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd_list->ResourceBarrier(1, &barrier);

        cmd_list->Close();
        ID3D12CommandList* lists[] = { cmd_list.Get() };
        queue->ExecuteCommandLists(1, lists);
        flush();

        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
        srv_heap_desc.NumDescriptors = 1;
        srv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!dx_check(device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap)),
                       "CreateDescriptorHeap (SRV)")) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format                  = tex_desc.Format;
        srv_desc.ViewDimension            = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels      = 1;
        device->CreateShaderResourceView(texture.Get(), &srv_desc,
                                          srv_heap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    // kFrameCount persistently-mapped upload buffers for the MVP constant
    // buffer (Milestone 4). One per in-flight frame so the CPU never writes a
    // buffer the GPU may still be reading.
    bool create_constant_buffers() {
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC res_desc{};
        res_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        res_desc.Width            = kConstantBufferSize;
        res_desc.Height           = 1;
        res_desc.DepthOrArraySize = 1;
        res_desc.MipLevels        = 1;
        res_desc.Format           = DXGI_FORMAT_UNKNOWN;
        res_desc.SampleDesc.Count = 1;
        res_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        for (u32 i = 0; i < kFrameCount; ++i) {
            if (!dx_check(device->CreateCommittedResource(
                               &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc,
                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                               IID_PPV_ARGS(&cb_resources[i])),
                           "CreateCommittedResource (constant buffer)")) {
                return false;
            }
            const D3D12_RANGE no_read{ 0, 0 };
            if (!dx_check(cb_resources[i]->Map(0, &no_read, reinterpret_cast<void**>(&cb_mapped[i])),
                           "Map (constant buffer)")) {
                return false;
            }
        }
        return true;
    }

    // Depth buffer (Milestone 4): D32_FLOAT, default heap, sized to the
    // swapchain. Called from init() and resize(); the DSV heap itself is
    // created once in init().
    bool create_depth_buffer() {
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = width;
        desc.Height           = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear{};
        clear.Format             = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;

        if (!dx_check(device->CreateCommittedResource(
                           &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                           D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_buffer)),
                       "CreateCommittedResource (depth buffer)")) {
            return false;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
        dsv_desc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth_buffer.Get(), &dsv_desc,
                                        dsv_heap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    // Recompute the MVP matrix from total_time and write it into this
    // frame's constant buffer (Milestone 4).
    void update_constant_buffer() {
        constexpr f32 kFovY = 1.0471975512f; // 60 degrees

        const f32 angle_x = total_time * 0.5f;
        const f32 angle_y = total_time * 0.8f;
        const mat4 model  = mat4_rotation_x(angle_x) * mat4_rotation_y(angle_y);
        const mat4 view   = mat4_look_at_lh({ 0.0f, 1.5f, -3.0f }, { 0.0f, 0.0f, 0.0f },
                                             { 0.0f, 1.0f, 0.0f });
        const f32  aspect = static_cast<f32>(width) / static_cast<f32>(height);
        const mat4 proj   = mat4_perspective_lh(kFovY, aspect, 0.1f, 100.0f);

        CubeCB cb{};
        cb.mvp = model * view * proj;
        std::memcpy(cb_mapped[frame_index], &cb, sizeof(cb));
    }

    // Records the cube draw against the already-open command list (called
    // from Renderer::begin_frame, after the render target/depth are bound and
    // cleared).
    void record_cube_draw() const {
        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width    = static_cast<f32>(width);
        viewport.Height   = static_cast<f32>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor{};
        scissor.left   = 0;
        scissor.top    = 0;
        scissor.right  = static_cast<LONG>(width);
        scissor.bottom = static_cast<LONG>(height);

        cmd_list->SetGraphicsRootSignature(root_signature.Get());
        cmd_list->SetPipelineState(pso.Get());

        cmd_list->SetGraphicsRootConstantBufferView(0, cb_resources[frame_index]->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* heaps[] = { srv_heap.Get() };
        cmd_list->SetDescriptorHeaps(1, heaps);
        cmd_list->SetGraphicsRootDescriptorTable(1, srv_heap->GetGPUDescriptorHandleForHeapStart());

        cmd_list->RSSetViewports(1, &viewport);
        cmd_list->RSSetScissorRects(1, &scissor);
        cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
        cmd_list->IASetIndexBuffer(&index_buffer_view);
        cmd_list->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }
};

bool Renderer::init(const RendererDesc& desc) {
    auto* impl = new RendererImpl();
    impl_       = impl;

    impl->width  = desc.width;
    impl->height = desc.height;
    impl->vsync  = desc.vsync;

    // 1. Debug layer + GPU-based validation + DRED, before device creation.
    UINT factory_flags = 0;
    if (desc.enable_debug_layer) {
        ComPtr<ID3D12Debug1> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
            debug_controller->EnableDebugLayer();
            debug_controller->SetEnableGPUBasedValidation(TRUE);
        }

        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred_settings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings)))) {
            dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }

        factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    // 2. Factory.
    if (!dx_check(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&impl->factory)),
                   "CreateDXGIFactory2")) {
        shutdown();
        return false;
    }

    // 3. Adapter + device (skip software adapters, prefer high performance).
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;
         impl->factory->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i) {
        DXGI_ADAPTER_DESC1 adapter_desc{};
        adapter->GetDesc1(&adapter_desc);
        if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter.Reset();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                         IID_PPV_ARGS(&impl->device)))) {
            break;
        }
        adapter.Reset();
    }
    if (!impl->device) {
        GE_ERROR("No suitable D3D12 adapter found");
        shutdown();
        return false;
    }

    // 4. Debug info queue: break on errors/corruption, and let us query the
    // message count later for the smoke test.
    if (desc.enable_debug_layer) {
        if (SUCCEEDED(impl->device.As(&impl->info_queue))) {
            impl->info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            impl->info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }

    // 5. Command queue, per-frame allocators, command list.
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!dx_check(impl->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&impl->queue)),
                   "CreateCommandQueue")) {
        shutdown();
        return false;
    }

    for (u32 i = 0; i < kFrameCount; ++i) {
        if (!dx_check(impl->device->CreateCommandAllocator(
                           D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&impl->allocators[i])),
                       "CreateCommandAllocator")) {
            shutdown();
            return false;
        }
    }

    if (!dx_check(impl->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    impl->allocators[0].Get(), nullptr,
                                                    IID_PPV_ARGS(&impl->cmd_list)),
                   "CreateCommandList")) {
        shutdown();
        return false;
    }
    impl->cmd_list->Close();

    // 6. Flip-model swapchain.
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc{};
    swapchain_desc.Width            = desc.width;
    swapchain_desc.Height           = desc.height;
    swapchain_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount      = kFrameCount;
    swapchain_desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapchain1;
    if (!dx_check(impl->factory->CreateSwapChainForHwnd(
                       impl->queue.Get(), static_cast<HWND>(desc.hwnd), &swapchain_desc,
                       nullptr, nullptr, &swapchain1),
                   "CreateSwapChainForHwnd")) {
        shutdown();
        return false;
    }
    // We handle resize ourselves; don't let DXGI auto-fullscreen on Alt+Enter.
    impl->factory->MakeWindowAssociation(static_cast<HWND>(desc.hwnd), DXGI_MWA_NO_ALT_ENTER);

    if (!dx_check(swapchain1.As(&impl->swapchain), "IDXGISwapChain3 QueryInterface")) {
        shutdown();
        return false;
    }
    impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();

    // 7. RTV heap + per-back-buffer RTVs.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = kFrameCount;
    rtv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!dx_check(impl->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&impl->rtv_heap)),
                   "CreateDescriptorHeap (RTV)")) {
        shutdown();
        return false;
    }
    impl->rtv_descriptor_size =
        impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    impl->create_rtvs();

    // 7b. DSV heap + depth buffer (Milestone 4).
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (!dx_check(impl->device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&impl->dsv_heap)),
                   "CreateDescriptorHeap (DSV)")) {
        shutdown();
        return false;
    }
    if (!impl->create_depth_buffer()) {
        shutdown();
        return false;
    }

    // 8. Fence + CPU wait event.
    if (!dx_check(impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl->fence)),
                   "CreateFence")) {
        shutdown();
        return false;
    }
    impl->fence_event = ::CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!impl->fence_event) {
        GE_ERROR("CreateEventA failed for the frame fence");
        shutdown();
        return false;
    }

    // Pipeline (root signature, PSO), cube geometry (vertex + index buffers),
    // texture + SRV heap, and the per-frame MVP constant buffers.
    if (!impl->create_root_signature() || !impl->create_pipeline() ||
        !impl->create_vertex_buffer() || !impl->create_index_buffer() ||
        !impl->create_texture() || !impl->create_constant_buffers()) {
        shutdown();
        return false;
    }

    GE_INFO("DX12 renderer initialized ({}x{}, {} frames in flight)", impl->width, impl->height,
            kFrameCount);
    return true;
}

void Renderer::shutdown() {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);
    if (!impl) return;

    if (impl->queue && impl->fence) {
        impl->flush();
    }

    for (u32 i = 0; i < kFrameCount; ++i) {
        if (impl->cb_resources[i] && impl->cb_mapped[i]) {
            impl->cb_resources[i]->Unmap(0, nullptr);
            impl->cb_mapped[i] = nullptr;
        }
    }

    if (impl->fence_event) {
        ::CloseHandle(impl->fence_event);
        impl->fence_event = nullptr;
    }

    delete impl;
    impl_ = nullptr;
}

void Renderer::begin_frame(float dt) {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);

    impl->total_time += dt;

    impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();

    // Wait until the GPU has finished with this frame slot's resources.
    const u64 wait_value = impl->fence_values[impl->frame_index];
    if (impl->fence->GetCompletedValue() < wait_value) {
        impl->fence->SetEventOnCompletion(wait_value, impl->fence_event);
        ::WaitForSingleObject(impl->fence_event, INFINITE);
    }

    impl->allocators[impl->frame_index]->Reset();
    impl->cmd_list->Reset(impl->allocators[impl->frame_index].Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = impl->back_buffers[impl->frame_index].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->cmd_list->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(impl->frame_index) * impl->rtv_descriptor_size;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = impl->dsv_heap->GetCPUDescriptorHandleForHeapStart();

    impl->cmd_list->ClearRenderTargetView(rtv, impl->clear_color, 0, nullptr);
    impl->cmd_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    impl->cmd_list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    impl->update_constant_buffer();
    impl->record_cube_draw();
}

void Renderer::end_frame() {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = impl->back_buffers[impl->frame_index].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->cmd_list->ResourceBarrier(1, &barrier);

    impl->cmd_list->Close();
    ID3D12CommandList* lists[] = { impl->cmd_list.Get() };
    impl->queue->ExecuteCommandLists(1, lists);

    impl->swapchain->Present(impl->vsync ? 1 : 0, 0);

    const u64 value = impl->next_fence_value++;
    impl->queue->Signal(impl->fence.Get(), value);
    impl->fence_values[impl->frame_index] = value;
}

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);
    if (width == 0 || height == 0) return;
    if (width == impl->width && height == impl->height) return;

    impl->flush();

    for (u32 i = 0; i < kFrameCount; ++i) {
        impl->back_buffers[i].Reset();
    }
    impl->depth_buffer.Reset();

    DXGI_SWAP_CHAIN_DESC desc{};
    impl->swapchain->GetDesc(&desc);
    dx_check(impl->swapchain->ResizeBuffers(kFrameCount, width, height, desc.BufferDesc.Format,
                                             desc.Flags),
             "ResizeBuffers");

    impl->width  = width;
    impl->height = height;
    impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();
    impl->create_rtvs();
    impl->create_depth_buffer();
}

void Renderer::set_clear_color(float r, float g, float b, float a) {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);
    impl->clear_color[0] = r;
    impl->clear_color[1] = g;
    impl->clear_color[2] = b;
    impl->clear_color[3] = a;
}

std::uint32_t Renderer::debug_error_count() const {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);
    if (!impl || !impl->info_queue) return 0;

    const UINT64 total = impl->info_queue->GetNumStoredMessages();
    u32          count = 0;
    for (UINT64 i = 0; i < total; ++i) {
        SIZE_T msg_len = 0;
        impl->info_queue->GetMessage(i, nullptr, &msg_len);
        if (msg_len == 0) continue;

        std::vector<std::byte> buffer(msg_len);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        impl->info_queue->GetMessage(i, message, &msg_len);
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            ++count;
        }
    }
    return count;
}

bool Renderer::capture_back_buffer(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_width,
                                    std::uint32_t& out_height) const {
    auto* impl = reinterpret_cast<RendererImpl*>(impl_);
    if (!impl) return false;

    out_width  = impl->width;
    out_height = impl->height;

    ID3D12Resource* back_buffer = impl->back_buffers[impl->frame_index].Get();
    D3D12_RESOURCE_DESC tex_desc = back_buffer->GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT   num_rows    = 0;
    UINT64 row_size    = 0;
    UINT64 total_size  = 0;
    impl->device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &num_rows, &row_size,
                                         &total_size);

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC buf_desc{};
    buf_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Width            = total_size;
    buf_desc.Height           = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels        = 1;
    buf_desc.Format           = DXGI_FORMAT_UNKNOWN;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readback;
    if (!dx_check(impl->device->CreateCommittedResource(
                       &heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                       nullptr, IID_PPV_ARGS(&readback)),
                   "CreateCommittedResource (readback buffer)")) {
        return false;
    }

    // Ensure end_frame's submission has fully completed before reusing this
    // frame's allocator/command list.
    impl->flush();

    impl->allocators[impl->frame_index]->Reset();
    impl->cmd_list->Reset(impl->allocators[impl->frame_index].Get(), nullptr);

    D3D12_RESOURCE_BARRIER to_copy{};
    to_copy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy.Transition.pResource   = back_buffer;
    to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    to_copy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->cmd_list->ResourceBarrier(1, &to_copy);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource        = readback.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint  = footprint;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = back_buffer;
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    impl->cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER to_present{};
    to_present.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_present.Transition.pResource   = back_buffer;
    to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    to_present.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    to_present.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->cmd_list->ResourceBarrier(1, &to_present);

    impl->cmd_list->Close();
    ID3D12CommandList* lists[] = { impl->cmd_list.Get() };
    impl->queue->ExecuteCommandLists(1, lists);
    impl->flush();

    u8* mapped = nullptr;
    const D3D12_RANGE read_range{ 0, static_cast<SIZE_T>(total_size) };
    if (!dx_check(readback->Map(0, &read_range, reinterpret_cast<void**>(&mapped)),
                   "Map (readback buffer)")) {
        return false;
    }

    out_rgba.resize(static_cast<usize>(impl->width) * impl->height * 4);
    for (u32 y = 0; y < impl->height; ++y) {
        std::memcpy(out_rgba.data() + static_cast<usize>(y) * impl->width * 4,
                     mapped + static_cast<usize>(footprint.Footprint.RowPitch) * y,
                     static_cast<usize>(impl->width) * 4);
    }
    const D3D12_RANGE no_write{ 0, 0 };
    readback->Unmap(0, &no_write);
    return true;
}

} // namespace ge::gfx
