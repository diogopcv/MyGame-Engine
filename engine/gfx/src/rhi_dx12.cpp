#include "gfx/rhi.h"

#include "core/log.h"
#include "core/assert.h"
#include "core/arena.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

namespace ge::gfx {

using Microsoft::WRL::ComPtr;

constexpr u32 kFrameCount       = kRhiFrameCount;
constexpr u32 kMaxBuffers       = 1024;
constexpr u32 kMaxTextures      = 512;
constexpr u32 kMaxShaders       = 128;
constexpr u32 kMaxPipelines     = 64;
constexpr u32 kSrvHeapSize      = 512;
constexpr u32 kMaxRootSignatures = 16;

// Descriptor-heap indices below these values are reserved for swapchain
// resources (back-buffer RTVs, swapchain depth DSV) and are re-used on
// resize; dynamic allocation starts above them and is never reset, so a
// resize can't hand out an index that a live user resource still owns.
constexpr u32 kReservedRtvs = kFrameCount;
constexpr u32 kReservedDsvs = 1;

// ---------------------------------------------------------------------------
// Internal resource structs stored in HandleTables
// ---------------------------------------------------------------------------

struct DX12Buffer {
    ComPtr<ID3D12Resource> resource;
    u32        size         = 0;
    BufferUsage usage       = BufferUsage::Vertex;
    bool       cpu_visible  = false;
    void*      mapped       = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address = 0;
};

struct DX12Texture {
    ComPtr<ID3D12Resource> resource;
    u32          width      = 0;
    u32          height     = 0;
    Format       format     = Format::Unknown;
    TextureUsage usage      = TextureUsage::ShaderResource;
    u32          rtv_index  = ~0u;
    u32          dsv_index  = ~0u;
    u32          srv_index  = ~0u;
};

struct DX12Shader {
    std::vector<u8> bytecode;
};

struct DX12Pipeline {
    ComPtr<ID3D12PipelineState> pso;
    ID3D12RootSignature*        root_signature = nullptr; ///< Owned by the root-signature cache.
};

/// One entry of the root-signature dedupe cache: pipelines with the same
/// BindingLayout + static-sampler state share a single root signature.
struct RootSignatureCacheEntry {
    u32                         key = 0;
    ComPtr<ID3D12RootSignature> root_signature;
};

/// Cache key packing: binding counts + the static-sampler state baked into
/// the root signature. All fields fit comfortably in 32 bits.
static u32 root_signature_key(const BindingLayout& b, FilterMode filter, AddressMode address) {
    return (b.cbv_count & 0xFF) | ((b.srv_table_count & 0xFF) << 8) |
           (static_cast<u32>(filter) << 16) | (static_cast<u32>(address) << 24);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool dx_check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        GE_ERROR("{} failed (hr=0x{:08X})", what, static_cast<u32>(hr));
        return false;
    }
    return true;
}

static DXGI_FORMAT to_dxgi_format(Format fmt) {
    switch (fmt) {
        case Format::R8G8B8A8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::R32G32_FLOAT:        return DXGI_FORMAT_R32G32_FLOAT;
        case Format::R32G32B32_FLOAT:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case Format::R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::D32_FLOAT:           return DXGI_FORMAT_D32_FLOAT;
        case Format::R16_UINT:            return DXGI_FORMAT_R16_UINT;
        case Format::R32_UINT:            return DXGI_FORMAT_R32_UINT;
        default:                          return DXGI_FORMAT_UNKNOWN;
    }
}

static D3D12_RESOURCE_STATES to_dx12_state(ResourceState state) {
    switch (state) {
        case ResourceState::Common:              return D3D12_RESOURCE_STATE_COMMON;
        case ResourceState::RenderTarget:        return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case ResourceState::DepthWrite:          return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case ResourceState::DepthRead:           return D3D12_RESOURCE_STATE_DEPTH_READ;
        case ResourceState::PixelShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case ResourceState::CopyDest:            return D3D12_RESOURCE_STATE_COPY_DEST;
        case ResourceState::CopySrc:             return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case ResourceState::Present:             return D3D12_RESOURCE_STATE_PRESENT;
        case ResourceState::GenericRead:         return D3D12_RESOURCE_STATE_GENERIC_READ;
        default:                                 return D3D12_RESOURCE_STATE_COMMON;
    }
}

static D3D12_TEXTURE_ADDRESS_MODE to_dx12_address(AddressMode a) {
    switch (a) {
        case AddressMode::Wrap:   return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case AddressMode::Clamp:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case AddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        default:                  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

static D3D12_COMPARISON_FUNC to_dx12_compare(CompareFunc f) {
    switch (f) {
        case CompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
        case CompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
        case CompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case CompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case CompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
        default:                        return D3D12_COMPARISON_FUNC_LESS;
    }
}

// ---------------------------------------------------------------------------
// RhiDeviceImpl
// ---------------------------------------------------------------------------

struct RhiDeviceImpl {
    // Core DX12 objects
    ComPtr<IDXGIFactory6>             factory;
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<IDXGISwapChain3>           swapchain;
    ComPtr<ID3D12CommandAllocator>    cmd_allocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> cmd_list;
    // Dedicated allocator + list for resource uploads and readbacks, so they
    // never touch the frame command list (safe to create resources mid-frame).
    ComPtr<ID3D12CommandAllocator>    upload_allocator;
    ComPtr<ID3D12GraphicsCommandList> upload_list;
    ComPtr<ID3D12Fence>               fence;
    ComPtr<ID3D12InfoQueue>           info_queue;
    HANDLE                            fence_event         = nullptr;
    u64                               fence_values[kFrameCount] = {};
    u64                               next_fence_value    = 1;
    u32                               frame_index         = 0;

    // Descriptor heaps
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    u32 rtv_descriptor_size = 0;
    u32 dsv_descriptor_size = 0;
    u32 srv_descriptor_size = 0;
    u32 next_rtv_index      = kReservedRtvs;
    u32 next_dsv_index      = kReservedDsvs;
    u32 next_srv_index      = 0;

    // Root-signature dedupe cache (see RootSignatureCacheEntry).
    RootSignatureCacheEntry root_signatures[kMaxRootSignatures];
    u32                     root_signature_count = 0;

    // Swapchain back buffers + depth (managed as texture handles)
    TextureHandle back_buffer_handles[kFrameCount];
    TextureHandle depth_handle;

    // Device state
    u32  width  = 0;
    u32  height = 0;
    bool vsync  = true;

    // Resource tables (using Arena backing)
    Arena table_arena;
    HandleTable<DX12Buffer,   BufferTag>   buffers;
    HandleTable<DX12Texture,  TextureTag>  textures;
    HandleTable<DX12Shader,   ShaderTag>   shaders;
    HandleTable<DX12Pipeline, PipelineTag> pipelines;

    // Currently bound root signature (tracked for set_constant_buffer/set_texture)
    ID3D12RootSignature* bound_root_sig = nullptr;

    void flush() {
        const u64 value = next_fence_value++;
        queue->Signal(fence.Get(), value);
        if (fence->GetCompletedValue() < value) {
            fence->SetEventOnCompletion(value, fence_event);
            ::WaitForSingleObject(fence_event, INFINITE);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu(u32 index) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * rtv_descriptor_size;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu(u32 index) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = dsv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * dsv_descriptor_size;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu(u32 index) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = srv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * srv_descriptor_size;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu(u32 index) const {
        D3D12_GPU_DESCRIPTOR_HANDLE h = srv_heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(index) * srv_descriptor_size;
        return h;
    }

    u32 alloc_rtv() { return next_rtv_index++; }
    u32 alloc_dsv() { return next_dsv_index++; }
    u32 alloc_srv() { return next_srv_index++; }

    /// Reset the upload list for recording copy/readback commands.
    /// Safe at any time: uploads are always submitted + flushed before return,
    /// so the allocator is never in flight.
    void begin_upload() {
        upload_allocator->Reset();
        upload_list->Reset(upload_allocator.Get(), nullptr);
    }

    /// Submit the upload list and block until the GPU has finished it.
    void end_upload() {
        upload_list->Close();
        ID3D12CommandList* lists[] = {upload_list.Get()};
        queue->ExecuteCommandLists(1, lists);
        flush();
    }

    bool create_swapchain_resources() {
        for (u32 i = 0; i < kFrameCount; ++i) {
            DX12Texture tex{};
            tex.width  = width;
            tex.height = height;
            tex.format = Format::R8G8B8A8_UNORM;
            tex.usage  = TextureUsage::RenderTarget;
            swapchain->GetBuffer(i, IID_PPV_ARGS(&tex.resource));

            // Reserved descriptor slot, reused across resizes.
            tex.rtv_index = i;
            device->CreateRenderTargetView(tex.resource.Get(), nullptr, rtv_cpu(tex.rtv_index));

            back_buffer_handles[i] = textures.add(tex);
        }
        return true;
    }

    bool create_depth_buffer_resource() {
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

        DX12Texture tex{};
        tex.width  = width;
        tex.height = height;
        tex.format = Format::D32_FLOAT;
        tex.usage  = TextureUsage::DepthStencil;

        if (!dx_check(device->CreateCommittedResource(
                &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&tex.resource)),
                "CreateCommittedResource (depth buffer)")) {
            return false;
        }

        // Reserved descriptor slot, reused across resizes.
        tex.dsv_index = 0;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
        dsv_desc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(tex.resource.Get(), &dsv_desc, dsv_cpu(tex.dsv_index));

        depth_handle = textures.add(tex);
        return true;
    }
};

// ---------------------------------------------------------------------------
// RhiDevice implementation
// ---------------------------------------------------------------------------

bool RhiDevice::init(const RhiDeviceDesc& desc) {
    auto* impl = new RhiDeviceImpl();
    impl_ = impl;

    impl->width  = desc.width;
    impl->height = desc.height;
    impl->vsync  = desc.vsync;

    // Handle tables need backing memory
    impl->table_arena.init(GE_MEGABYTES(4));
    Allocator alloc = impl->table_arena.allocator();
    impl->buffers.init(kMaxBuffers, alloc);
    impl->textures.init(kMaxTextures, alloc);
    impl->shaders.init(kMaxShaders, alloc);
    impl->pipelines.init(kMaxPipelines, alloc);

    // 1. Debug layer
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

    // 2. Factory
    if (!dx_check(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&impl->factory)),
                  "CreateDXGIFactory2")) {
        shutdown();
        return false;
    }

    // 3. Adapter + device
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

    // 4. Debug info queue
    if (desc.enable_debug_layer) {
        if (SUCCEEDED(impl->device.As(&impl->info_queue))) {
            impl->info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            // Gate ERROR break on debugger presence: INT3 without a debugger attached
            // becomes an unhandled SEH exception that crashes headless test runners.
            if (::IsDebuggerPresent())
                impl->info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }

    // 5. Command queue + per-frame allocators + command list
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!dx_check(impl->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&impl->queue)),
                  "CreateCommandQueue")) {
        shutdown();
        return false;
    }

    for (u32 i = 0; i < kFrameCount; ++i) {
        if (!dx_check(impl->device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&impl->cmd_allocators[i])),
                "CreateCommandAllocator")) {
            shutdown();
            return false;
        }
    }

    if (!dx_check(impl->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                  impl->cmd_allocators[0].Get(), nullptr,
                  IID_PPV_ARGS(&impl->cmd_list)),
                  "CreateCommandList")) {
        shutdown();
        return false;
    }
    impl->cmd_list->Close();

    if (!dx_check(impl->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&impl->upload_allocator)),
            "CreateCommandAllocator (upload)")) {
        shutdown();
        return false;
    }
    if (!dx_check(impl->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                  impl->upload_allocator.Get(), nullptr,
                  IID_PPV_ARGS(&impl->upload_list)),
                  "CreateCommandList (upload)")) {
        shutdown();
        return false;
    }
    impl->upload_list->Close();

    // 6. Swapchain
    DXGI_SWAP_CHAIN_DESC1 sc_desc{};
    sc_desc.Width            = desc.width;
    sc_desc.Height           = desc.height;
    sc_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount      = kFrameCount;
    sc_desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapchain1;
    if (!dx_check(impl->factory->CreateSwapChainForHwnd(
            impl->queue.Get(), static_cast<HWND>(desc.hwnd), &sc_desc,
            nullptr, nullptr, &swapchain1),
            "CreateSwapChainForHwnd")) {
        shutdown();
        return false;
    }
    impl->factory->MakeWindowAssociation(static_cast<HWND>(desc.hwnd), DXGI_MWA_NO_ALT_ENTER);

    if (!dx_check(swapchain1.As(&impl->swapchain), "IDXGISwapChain3 QueryInterface")) {
        shutdown();
        return false;
    }
    impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();

    // 7. Descriptor heaps
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        // RTV heap: kFrameCount for swapchain + extras for user render targets
        heap_desc.NumDescriptors = kFrameCount + 32;
        heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (!dx_check(impl->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&impl->rtv_heap)),
                      "CreateDescriptorHeap (RTV)")) {
            shutdown();
            return false;
        }
        impl->rtv_descriptor_size =
            impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = 16;
        heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (!dx_check(impl->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&impl->dsv_heap)),
                      "CreateDescriptorHeap (DSV)")) {
            shutdown();
            return false;
        }
        impl->dsv_descriptor_size =
            impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = kSrvHeapSize;
        heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!dx_check(impl->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&impl->srv_heap)),
                      "CreateDescriptorHeap (SRV)")) {
            shutdown();
            return false;
        }
        impl->srv_descriptor_size =
            impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // 8. Swapchain RTVs + depth buffer (tracked as texture handles)
    if (!impl->create_swapchain_resources()) {
        shutdown();
        return false;
    }
    if (!impl->create_depth_buffer_resource()) {
        shutdown();
        return false;
    }

    // 9. Fence
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

    GE_INFO("RHI DX12 initialized ({}x{}, {} frames in flight)", impl->width, impl->height,
            kFrameCount);
    return true;
}

void RhiDevice::wait_idle() {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    if (!impl || !impl->queue || !impl->fence) return;
    impl->flush();
}

void RhiDevice::shutdown() {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    if (!impl) return;

    if (impl->queue && impl->fence) {
        impl->flush();
    }

    if (impl->fence_event) {
        ::CloseHandle(impl->fence_event);
        impl->fence_event = nullptr;
    }

    // HandleTables destroy their contents on shutdown (ComPtrs auto-release)
    impl->pipelines.shutdown();
    impl->shaders.shutdown();
    impl->textures.shutdown();
    impl->buffers.shutdown();
    impl->table_arena.shutdown();

    delete impl;
    impl_ = nullptr;
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

/// Create a plain buffer resource description of @p size bytes.
static D3D12_RESOURCE_DESC buffer_resource_desc(u64 size) {
    D3D12_RESOURCE_DESC res_desc{};
    res_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    res_desc.Width            = size;
    res_desc.Height           = 1;
    res_desc.DepthOrArraySize = 1;
    res_desc.MipLevels        = 1;
    res_desc.Format           = DXGI_FORMAT_UNKNOWN;
    res_desc.SampleDesc.Count = 1;
    res_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return res_desc;
}

BufferHandle RhiDevice::create_buffer(const BufferDesc& desc) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);

    D3D12_HEAP_PROPERTIES heap_props{};
    D3D12_RESOURCE_STATES initial_state;

    DX12Buffer buf{};
    buf.size        = desc.size;
    buf.usage       = desc.usage;
    buf.cpu_visible = desc.cpu_visible || desc.usage == BufferUsage::Upload ||
                      desc.usage == BufferUsage::Readback;

    if (desc.usage == BufferUsage::Readback) {
        heap_props.Type = D3D12_HEAP_TYPE_READBACK;
        initial_state   = D3D12_RESOURCE_STATE_COPY_DEST;
    } else if (buf.cpu_visible) {
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
        initial_state   = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else {
        // GPU-only buffer (vertex/index data): default heap, uploaded through
        // a staging buffer below. COMMON lets D3D12's implicit buffer state
        // promotion/decay handle transitions without explicit barriers.
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        initial_state   = D3D12_RESOURCE_STATE_COMMON;
    }

    D3D12_RESOURCE_DESC res_desc = buffer_resource_desc(desc.size);

    if (!dx_check(impl->device->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, initial_state,
            nullptr, IID_PPV_ARGS(&buf.resource)),
            "CreateCommittedResource (buffer)")) {
        return {};
    }

    buf.gpu_address = buf.resource->GetGPUVirtualAddress();

    if (desc.initial_data && buf.cpu_visible && desc.usage != BufferUsage::Readback) {
        void* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        if (SUCCEEDED(buf.resource->Map(0, &no_read, &mapped))) {
            std::memcpy(mapped, desc.initial_data, desc.size);
            buf.resource->Unmap(0, nullptr);
        }
    } else if (desc.initial_data && !buf.cpu_visible) {
        // Stage through an upload buffer and copy on the dedicated upload list.
        D3D12_HEAP_PROPERTIES staging_heap{};
        staging_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        ComPtr<ID3D12Resource> staging;
        if (!dx_check(impl->device->CreateCommittedResource(
                &staging_heap, D3D12_HEAP_FLAG_NONE, &res_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)),
                "CreateCommittedResource (buffer staging)")) {
            return {};
        }

        void* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        if (!dx_check(staging->Map(0, &no_read, &mapped), "Map (buffer staging)")) {
            return {};
        }
        std::memcpy(mapped, desc.initial_data, desc.size);
        staging->Unmap(0, nullptr);

        impl->begin_upload();
        impl->upload_list->CopyBufferRegion(buf.resource.Get(), 0, staging.Get(), 0, desc.size);
        impl->end_upload();
    }

    return impl->buffers.add(buf);
}

TextureHandle RhiDevice::create_texture(const TextureDesc& desc) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    // When a depth texture is also used as a shader resource, create the
    // resource with a typeless format so both DSV and SRV views can coexist.
    const bool depth_srv = (desc.usage & TextureUsage::DepthStencil) &&
                           (desc.usage & TextureUsage::ShaderResource);

    D3D12_RESOURCE_DESC res_desc{};
    res_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    res_desc.Width            = desc.width;
    res_desc.Height           = desc.height;
    res_desc.DepthOrArraySize = 1;
    res_desc.MipLevels        = static_cast<UINT16>(desc.mip_levels);
    res_desc.Format           = (depth_srv && desc.format == Format::D32_FLOAT)
                                ? DXGI_FORMAT_R32_TYPELESS
                                : to_dxgi_format(desc.format);
    res_desc.SampleDesc.Count = 1;
    res_desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (desc.usage & TextureUsage::RenderTarget) {
        res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (desc.usage & TextureUsage::DepthStencil) {
        res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE* clear_ptr = nullptr;
    D3D12_CLEAR_VALUE  clear{};

    if (desc.usage & TextureUsage::DepthStencil) {
        initial_state              = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        clear.Format               = to_dxgi_format(desc.format);
        clear.DepthStencil.Depth   = 1.0f;
        clear.DepthStencil.Stencil = 0;
        clear_ptr                  = &clear;
    } else if (desc.initial_data) {
        initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    DX12Texture tex{};
    tex.width  = desc.width;
    tex.height = desc.height;
    tex.format = desc.format;
    tex.usage  = desc.usage;

    if (!dx_check(impl->device->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, initial_state,
            clear_ptr, IID_PPV_ARGS(&tex.resource)),
            "CreateCommittedResource (texture)")) {
        return {};
    }

    // Create views as needed
    if (desc.usage & TextureUsage::RenderTarget) {
        tex.rtv_index = impl->alloc_rtv();
        impl->device->CreateRenderTargetView(tex.resource.Get(), nullptr,
                                             impl->rtv_cpu(tex.rtv_index));
    }
    if (desc.usage & TextureUsage::DepthStencil) {
        tex.dsv_index = impl->alloc_dsv();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
        dsv_desc.Format        = to_dxgi_format(desc.format);
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        impl->device->CreateDepthStencilView(tex.resource.Get(), &dsv_desc,
                                             impl->dsv_cpu(tex.dsv_index));
    }
    if (desc.usage & TextureUsage::ShaderResource) {
        tex.srv_index = impl->alloc_srv();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format                  = depth_srv ? DXGI_FORMAT_R32_FLOAT
                                                     : to_dxgi_format(desc.format);
        srv_desc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels     = desc.mip_levels;
        impl->device->CreateShaderResourceView(tex.resource.Get(), &srv_desc,
                                               impl->srv_cpu(tex.srv_index));
    }

    // Upload initial data if provided
    if (desc.initial_data) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT   num_rows    = 0;
        UINT64 row_size    = 0;
        UINT64 upload_size = 0;
        impl->device->GetCopyableFootprints(&res_desc, 0, 1, 0, &footprint, &num_rows,
                                            &row_size, &upload_size);

        D3D12_HEAP_PROPERTIES upload_heap{};
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

        ComPtr<ID3D12Resource> upload_buffer;
        if (!dx_check(impl->device->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&upload_buffer)),
                "CreateCommittedResource (texture upload)")) {
            return {};
        }

        u8* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        if (!dx_check(upload_buffer->Map(0, &no_read, reinterpret_cast<void**>(&mapped)),
                      "Map (texture upload)")) {
            return {};
        }

        const u32 src_pitch = desc.row_pitch ? desc.row_pitch : desc.width * 4;
        for (u32 y = 0; y < desc.height; ++y) {
            std::memcpy(mapped + footprint.Footprint.RowPitch * y,
                        static_cast<const u8*>(desc.initial_data) + static_cast<usize>(src_pitch) * y,
                        src_pitch);
        }
        upload_buffer->Unmap(0, nullptr);

        impl->begin_upload();

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource        = tex.resource.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource       = upload_buffer.Get();
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        impl->upload_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = tex.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        impl->upload_list->ResourceBarrier(1, &barrier);

        impl->end_upload();
    }

    return impl->textures.add(tex);
}

ShaderHandle RhiDevice::create_shader(const ShaderDesc& desc) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);

    DX12Shader shader{};
    shader.bytecode.assign(desc.bytecode, desc.bytecode + desc.bytecode_size);
    return impl->shaders.add(shader);
}

PipelineHandle RhiDevice::create_pipeline(const PipelineDesc& desc) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);

    DX12Shader* vs = impl->shaders.get(desc.vertex_shader);
    DX12Shader* ps = desc.pixel_shader.is_valid() ? impl->shaders.get(desc.pixel_shader) : nullptr;
    if (!vs) {
        GE_ERROR("create_pipeline: invalid vertex shader handle");
        return {};
    }

    // -- Shared static samplers (s0 = linear wrap, s1 = comparison for shadows) ----
    D3D12_FILTER s0_filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (desc.sampler_filter == FilterMode::Linear)
        s0_filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    else if (desc.sampler_filter == FilterMode::Anisotropic)
        s0_filter = D3D12_FILTER_ANISOTROPIC;

    const D3D12_TEXTURE_ADDRESS_MODE s0_address = to_dx12_address(desc.sampler_address);
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter           = s0_filter;
    samplers[0].AddressU         = s0_address;
    samplers[0].AddressV         = s0_address;
    samplers[0].AddressW         = s0_address;
    samplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].ShaderRegister   = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ShaderRegister   = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // -- Root signature: derived from desc.bindings, deduped via the cache ------
    // Slot convention (see BindingLayout): root params [0, cbv_count) are CBVs
    // b0..bN-1; params [cbv_count, cbv_count+srv_table_count) are single-SRV
    // tables t0..tM-1.
    const BindingLayout& bindings = desc.bindings;
    if (bindings.cbv_count > kMaxBindingCbvs ||
        bindings.srv_table_count > kMaxBindingSrvTables) {
        GE_ERROR("create_pipeline: BindingLayout exceeds limits ({} CBVs, {} SRV tables)",
                 bindings.cbv_count, bindings.srv_table_count);
        return {};
    }

    DX12Pipeline pipe{};
    const u32 rs_key = root_signature_key(bindings, desc.sampler_filter, desc.sampler_address);
    for (u32 i = 0; i < impl->root_signature_count; ++i) {
        if (impl->root_signatures[i].key == rs_key) {
            pipe.root_signature = impl->root_signatures[i].root_signature.Get();
            break;
        }
    }

    if (!pipe.root_signature) {
        if (impl->root_signature_count >= kMaxRootSignatures) {
            GE_ERROR("create_pipeline: root-signature cache full ({})", kMaxRootSignatures);
            return {};
        }

        D3D12_DESCRIPTOR_RANGE srv_ranges[kMaxBindingSrvTables]{};
        for (u32 r = 0; r < bindings.srv_table_count; ++r) {
            srv_ranges[r].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srv_ranges[r].NumDescriptors     = 1;
            srv_ranges[r].BaseShaderRegister = r;
            srv_ranges[r].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }

        D3D12_ROOT_PARAMETER params[kMaxBindingCbvs + kMaxBindingSrvTables]{};
        for (u32 c = 0; c < bindings.cbv_count; ++c) {
            params[c].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[c].Descriptor.ShaderRegister = c;
            params[c].Descriptor.RegisterSpace  = 0;
            params[c].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        }
        for (u32 s = 0; s < bindings.srv_table_count; ++s) {
            D3D12_ROOT_PARAMETER& p = params[bindings.cbv_count + s];
            p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &srv_ranges[s];
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        }

        D3D12_ROOT_SIGNATURE_DESC rs_desc{};
        rs_desc.NumParameters     = bindings.cbv_count + bindings.srv_table_count;
        rs_desc.pParameters       = params;
        rs_desc.NumStaticSamplers = _countof(samplers);
        rs_desc.pStaticSamplers   = samplers;
        rs_desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sig_blob, err_blob;
        HRESULT hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                 &sig_blob, &err_blob);
        if (FAILED(hr)) {
            if (err_blob)
                GE_ERROR("D3D12SerializeRootSignature: {}",
                         static_cast<const char*>(err_blob->GetBufferPointer()));
            return {};
        }

        RootSignatureCacheEntry& entry = impl->root_signatures[impl->root_signature_count];
        if (!dx_check(impl->device->CreateRootSignature(0, sig_blob->GetBufferPointer(),
                      sig_blob->GetBufferSize(), IID_PPV_ARGS(&entry.root_signature)),
                      "CreateRootSignature")) {
            return {};
        }
        entry.key = rs_key;
        ++impl->root_signature_count;
        pipe.root_signature = entry.root_signature.Get();
    }

    // Input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elems(desc.input_element_count);
    for (u32 i = 0; i < desc.input_element_count; ++i) {
        input_elems[i].SemanticName         = desc.input_elements[i].semantic;
        input_elems[i].SemanticIndex        = desc.input_elements[i].semantic_index;
        input_elems[i].Format               = to_dxgi_format(desc.input_elements[i].format);
        input_elems[i].InputSlot            = 0;
        input_elems[i].AlignedByteOffset    = desc.input_elements[i].offset;
        input_elems[i].InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        input_elems[i].InstanceDataStepRate = 0;
    }

    // Rasterizer state
    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode              = (desc.fill_mode == FillMode::Wireframe)
                                       ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode              = (desc.cull_mode == CullMode::Back)  ? D3D12_CULL_MODE_BACK
                                     : (desc.cull_mode == CullMode::Front) ? D3D12_CULL_MODE_FRONT
                                     : D3D12_CULL_MODE_NONE;
    rasterizer.FrontCounterClockwise = FALSE;
    rasterizer.DepthClipEnable       = TRUE;
    if (desc.depth_bias_enable) {
        rasterizer.DepthBias            = desc.depth_bias;
        rasterizer.DepthBiasClamp       = desc.depth_bias_clamp;
        rasterizer.SlopeScaledDepthBias = desc.slope_scaled_depth_bias;
    }

    // Blend state. AlphaBlend uses straight (non-premultiplied) alpha and
    // forces depth-write off so transparent fragments don't occlude what's
    // behind them; the caller sorts blended draws back-to-front.
    const bool alpha_blend = (desc.blend_mode == BlendMode::AlphaBlend);
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable           = alpha_blend ? TRUE : FALSE;
    blend.RenderTarget[0].LogicOpEnable         = FALSE;
    blend.RenderTarget[0].SrcBlend              = alpha_blend ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend             = alpha_blend ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha         = alpha_blend ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Transparent geometry tests depth but must not write it.
    const bool depth_write = desc.depth_write && !alpha_blend;

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature        = pipe.root_signature;
    pso_desc.VS                    = {vs->bytecode.data(), vs->bytecode.size()};
    if (ps) pso_desc.PS            = {ps->bytecode.data(), ps->bytecode.size()};
    pso_desc.InputLayout           = {input_elems.data(), static_cast<UINT>(input_elems.size())};
    pso_desc.RasterizerState       = rasterizer;
    pso_desc.BlendState            = blend;
    pso_desc.DepthStencilState.DepthEnable    = desc.depth_enable ? TRUE : FALSE;
    pso_desc.DepthStencilState.DepthWriteMask = depth_write ? D3D12_DEPTH_WRITE_MASK_ALL
                                                            : D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc      = to_dx12_compare(desc.depth_func);
    pso_desc.DepthStencilState.StencilEnable  = FALSE;
    pso_desc.DSVFormat             = to_dxgi_format(desc.ds_format);
    pso_desc.SampleMask            = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets      = desc.rt_count;
    for (u32 i = 0; i < desc.rt_count; ++i)
        pso_desc.RTVFormats[i]     = to_dxgi_format(desc.rt_format);
    pso_desc.SampleDesc.Count      = 1;

    if (!dx_check(impl->device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipe.pso)),
                  "CreateGraphicsPipelineState")) {
        return {};
    }

    return impl->pipelines.add(pipe);
}

void RhiDevice::destroy_buffer(BufferHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    if (buf && buf->mapped) {
        buf->resource->Unmap(0, nullptr);
        buf->mapped = nullptr;
    }
    impl->buffers.remove(h);
}

void RhiDevice::destroy_texture(TextureHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    impl->textures.remove(h);
}

void RhiDevice::destroy_shader(ShaderHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    impl->shaders.remove(h);
}

void RhiDevice::destroy_pipeline(PipelineHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    impl->pipelines.remove(h);
}

// ---------------------------------------------------------------------------
// Data transfer
// ---------------------------------------------------------------------------

void RhiDevice::update_buffer(BufferHandle h, const void* data, u32 size, u32 offset) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    if (!buf) return;

    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (SUCCEEDED(buf->resource->Map(0, &no_read, &mapped))) {
        std::memcpy(static_cast<u8*>(mapped) + offset, data, size);
        buf->resource->Unmap(0, nullptr);
    }
}

void* RhiDevice::map_buffer(BufferHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    GE_ASSERT(buf == nullptr || buf->cpu_visible); // default-heap buffers cannot be mapped
    if (!buf) return nullptr;

    if (!buf->mapped) {
        const D3D12_RANGE no_read{0, 0};
        buf->resource->Map(0, &no_read, &buf->mapped);
    }
    return buf->mapped;
}

void RhiDevice::unmap_buffer(BufferHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    if (!buf || !buf->mapped) return;
    buf->resource->Unmap(0, nullptr);
    buf->mapped = nullptr;
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void RhiDevice::begin_frame() {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);

    impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();

    const u64 wait_value = impl->fence_values[impl->frame_index];
    if (impl->fence->GetCompletedValue() < wait_value) {
        impl->fence->SetEventOnCompletion(wait_value, impl->fence_event);
        ::WaitForSingleObject(impl->fence_event, INFINITE);
    }

    impl->cmd_allocators[impl->frame_index]->Reset();
    impl->cmd_list->Reset(impl->cmd_allocators[impl->frame_index].Get(), nullptr);

    // Bind the shader-visible SRV heap for the entire frame
    ID3D12DescriptorHeap* heaps[] = {impl->srv_heap.Get()};
    impl->cmd_list->SetDescriptorHeaps(1, heaps);
}

void RhiDevice::end_frame() {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    impl->cmd_list->Close();

    ID3D12CommandList* lists[] = {impl->cmd_list.Get()};
    impl->queue->ExecuteCommandLists(1, lists);
}

void RhiDevice::present(bool vsync) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    impl->swapchain->Present(vsync ? 1 : 0, 0);

    const u64 value = impl->next_fence_value++;
    impl->queue->Signal(impl->fence.Get(), value);
    impl->fence_values[impl->frame_index] = value;
}

void RhiDevice::resize(u32 w, u32 h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    if (w == 0 || h == 0) return;
    if (w == impl->width && h == impl->height) return;

    impl->flush();

    // Release old swapchain textures and depth
    for (u32 i = 0; i < kFrameCount; ++i) {
        DX12Texture* tex = impl->textures.get(impl->back_buffer_handles[i]);
        if (tex) tex->resource.Reset();
        impl->textures.remove(impl->back_buffer_handles[i]);
    }
    {
        DX12Texture* dtex = impl->textures.get(impl->depth_handle);
        if (dtex) dtex->resource.Reset();
        impl->textures.remove(impl->depth_handle);
    }

    // Swapchain resources reuse their reserved descriptor slots (indices
    // below kReservedRtvs/kReservedDsvs); dynamic counters are never reset,
    // so live user render targets keep their descriptors across resizes.

    DXGI_SWAP_CHAIN_DESC desc{};
    impl->swapchain->GetDesc(&desc);
    dx_check(impl->swapchain->ResizeBuffers(kFrameCount, w, h, desc.BufferDesc.Format, desc.Flags),
             "ResizeBuffers");

    impl->width  = w;
    impl->height = h;
    impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();
    impl->create_swapchain_resources();
    impl->create_depth_buffer_resource();
}

// ---------------------------------------------------------------------------
// Command recording
// ---------------------------------------------------------------------------

void RhiDevice::barrier(TextureHandle tex, ResourceState before, ResourceState after) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Texture* t = impl->textures.get(tex);
    if (!t) return;

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = t->resource.Get();
    b.Transition.StateBefore = to_dx12_state(before);
    b.Transition.StateAfter  = to_dx12_state(after);
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->cmd_list->ResourceBarrier(1, &b);
}

void RhiDevice::set_render_targets(const TextureHandle* rts, u32 rt_count, TextureHandle depth) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[8]{};
    for (u32 i = 0; i < rt_count && i < 8; ++i) {
        DX12Texture* t = impl->textures.get(rts[i]);
        if (t && t->rtv_index != ~0u)
            rtv_handles[i] = impl->rtv_cpu(t->rtv_index);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* dsv_ptr = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE  dsv_handle{};
    DX12Texture* dt = impl->textures.get(depth);
    if (dt && dt->dsv_index != ~0u) {
        dsv_handle = impl->dsv_cpu(dt->dsv_index);
        dsv_ptr = &dsv_handle;
    }

    impl->cmd_list->OMSetRenderTargets(rt_count, rtv_handles, FALSE, dsv_ptr);
}

void RhiDevice::clear_render_target(TextureHandle rt, const f32 color[4]) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Texture* t = impl->textures.get(rt);
    if (!t || t->rtv_index == ~0u) return;
    impl->cmd_list->ClearRenderTargetView(impl->rtv_cpu(t->rtv_index), color, 0, nullptr);
}

void RhiDevice::clear_depth(TextureHandle ds, f32 depth) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Texture* t = impl->textures.get(ds);
    if (!t || t->dsv_index == ~0u) return;
    impl->cmd_list->ClearDepthStencilView(impl->dsv_cpu(t->dsv_index),
                                          D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void RhiDevice::set_pipeline(PipelineHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Pipeline* p = impl->pipelines.get(h);
    GE_ASSERT(p != nullptr); // invalid/stale pipeline handle
    if (!p) return;
    impl->cmd_list->SetGraphicsRootSignature(p->root_signature);
    impl->cmd_list->SetPipelineState(p->pso.Get());
    impl->bound_root_sig = p->root_signature;
}

void RhiDevice::set_vertex_buffer(BufferHandle h, u32 stride) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    GE_ASSERT(buf != nullptr); // invalid/stale buffer handle
    if (!buf) return;

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = buf->gpu_address;
    vbv.StrideInBytes  = stride;
    vbv.SizeInBytes    = buf->size;
    impl->cmd_list->IASetVertexBuffers(0, 1, &vbv);
    impl->cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void RhiDevice::set_index_buffer(BufferHandle h, Format format) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    GE_ASSERT(buf != nullptr); // invalid/stale buffer handle
    if (!buf) return;

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = buf->gpu_address;
    ibv.Format         = to_dxgi_format(format);
    ibv.SizeInBytes    = buf->size;
    impl->cmd_list->IASetIndexBuffer(&ibv);
}

void RhiDevice::set_constant_buffer(u32 slot, BufferHandle h, u32 offset) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Buffer* buf = impl->buffers.get(h);
    GE_ASSERT(buf != nullptr);          // invalid/stale buffer handle
    if (!buf) return;
    GE_ASSERT((offset & 255u) == 0);    // CBV addresses must be 256-byte aligned
    GE_ASSERT(offset < buf->size);
    impl->cmd_list->SetGraphicsRootConstantBufferView(slot, buf->gpu_address + offset);
}

void RhiDevice::set_texture(u32 slot, TextureHandle h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    DX12Texture* t = impl->textures.get(h);
    GE_ASSERT(t != nullptr && t->srv_index != ~0u); // invalid handle or texture has no SRV
    if (!t || t->srv_index == ~0u) return;
    impl->cmd_list->SetGraphicsRootDescriptorTable(slot, impl->srv_gpu(t->srv_index));
}

void RhiDevice::set_viewport(f32 x, f32 y, f32 w, f32 h, f32 min_depth, f32 max_depth) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    D3D12_VIEWPORT vp{x, y, w, h, min_depth, max_depth};
    impl->cmd_list->RSSetViewports(1, &vp);
}

void RhiDevice::set_scissor(i32 x, i32 y, u32 w, u32 h) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    D3D12_RECT rect{x, y, static_cast<LONG>(x + w), static_cast<LONG>(y + h)};
    impl->cmd_list->RSSetScissorRects(1, &rect);
}

void RhiDevice::draw_indexed(u32 index_count, u32 start_index, i32 base_vertex) {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    impl->cmd_list->DrawIndexedInstanced(index_count, 1, start_index, base_vertex, 0);
}

// ---------------------------------------------------------------------------
// Swapchain queries
// ---------------------------------------------------------------------------

TextureHandle RhiDevice::back_buffer() const {
    auto* impl = static_cast<const RhiDeviceImpl*>(impl_);
    return impl->back_buffer_handles[impl->frame_index];
}

TextureHandle RhiDevice::depth_buffer() const {
    auto* impl = static_cast<const RhiDeviceImpl*>(impl_);
    return impl->depth_handle;
}

u32 RhiDevice::width() const {
    return static_cast<const RhiDeviceImpl*>(impl_)->width;
}

u32 RhiDevice::height() const {
    return static_cast<const RhiDeviceImpl*>(impl_)->height;
}

u32 RhiDevice::frame_index() const {
    return static_cast<const RhiDeviceImpl*>(impl_)->frame_index;
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

u32 RhiDevice::debug_error_count() const {
    auto* impl = static_cast<const RhiDeviceImpl*>(impl_);
    if (!impl || !impl->info_queue) return 0;

    const UINT64 total = impl->info_queue->GetNumStoredMessages();
    u32 count = 0;
    for (UINT64 i = 0; i < total; ++i) {
        SIZE_T msg_len = 0;
        impl->info_queue->GetMessage(i, nullptr, &msg_len);
        if (msg_len == 0) continue;

        std::vector<std::byte> buffer(msg_len);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        impl->info_queue->GetMessage(i, message, &msg_len);
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            GE_ERROR("D3D12 [{}]: {}", i, message->pDescription);
            ++count;
        }
    }
    return count;
}

bool RhiDevice::capture_back_buffer(std::vector<std::uint8_t>& out_rgba,
                                    std::uint32_t& out_width,
                                    std::uint32_t& out_height) const {
    auto* impl = static_cast<RhiDeviceImpl*>(impl_);
    if (!impl) return false;

    out_width  = impl->width;
    out_height = impl->height;

    DX12Texture* bb = impl->textures.get(impl->back_buffer_handles[impl->frame_index]);
    if (!bb) return false;

    ID3D12Resource* back_buffer_res = bb->resource.Get();
    D3D12_RESOURCE_DESC tex_desc = back_buffer_res->GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT   num_rows   = 0;
    UINT64 row_size   = 0;
    UINT64 total_size = 0;
    impl->device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &num_rows,
                                        &row_size, &total_size);

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
            "CreateCommittedResource (readback)")) {
        return false;
    }

    impl->flush();

    impl->begin_upload();

    D3D12_RESOURCE_BARRIER to_copy{};
    to_copy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy.Transition.pResource   = back_buffer_res;
    to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    to_copy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->upload_list->ResourceBarrier(1, &to_copy);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource       = readback.Get();
    dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = back_buffer_res;
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    impl->upload_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER to_present{};
    to_present.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_present.Transition.pResource   = back_buffer_res;
    to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    to_present.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    to_present.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl->upload_list->ResourceBarrier(1, &to_present);

    impl->end_upload();

    u8* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
    if (!dx_check(readback->Map(0, &read_range, reinterpret_cast<void**>(&mapped)),
                  "Map (readback)")) {
        return false;
    }

    out_rgba.resize(static_cast<usize>(impl->width) * impl->height * 4);
    for (u32 y = 0; y < impl->height; ++y) {
        std::memcpy(out_rgba.data() + static_cast<usize>(y) * impl->width * 4,
                    mapped + static_cast<usize>(footprint.Footprint.RowPitch) * y,
                    static_cast<usize>(impl->width) * 4);
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
    return true;
}

} // namespace ge::gfx
