#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <type_traits>

#ifndef NDEBUG
#include "shaders/generated/PSBinDebug.h"
#include "shaders/generated/VSBinDebug.h"
#else
#include "shaders/generated/VSBin.h"
#include "shaders/generated/PSBin.h"
#endif

using Microsoft::WRL::ComPtr;
using Window = std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) -> void {
    if (hwnd) {
        DestroyWindow(hwnd);
    }
})>;
using Event = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype([](HANDLE const handle) -> void {
    if (handle) {
        CloseHandle(handle);
    }
})>;
using Vec2 = std::array<float, 2>;


namespace {
constexpr auto SWAP_CHAIN_BUFFER_COUNT{ 2 };
constexpr auto SWAP_CHAIN_FORMAT{ DXGI_FORMAT_R8G8B8A8_UNORM };


template<typename... Args>
auto ThrowIfFailed(HRESULT const hr, std::format_string<Args...> const errMsgFmt, Args&&... args) -> void {
    if (FAILED(hr)) {
        LPSTR sysErrMsg{ nullptr };
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&sysErrMsg), 0, nullptr);
        std::string ret{ sysErrMsg };
        LocalFree(sysErrMsg);
        throw std::runtime_error{ std::format("{} {}", std::format(errMsgFmt, std::forward<Args>(args)...), sysErrMsg) };
    }
}


auto ThrowIfFailed(HRESULT const hr, std::format_string<std::string_view> const usrErrFmt, ID3DBlob* const errBlob) -> void {
    return ThrowIfFailed(hr, usrErrFmt, errBlob ? std::string_view{ static_cast<char const*>(errBlob->GetBufferPointer()), errBlob->GetBufferSize() } : std::string_view{});
}


auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
    if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
}


auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance, [[maybe_unused]] _In_ wchar_t* const lpCmdLine, _In_ int const nShowCmd) -> int {
    try {
        WNDCLASSW const windowClass{
            .lpfnWndProc = &WindowProc,
            .hInstance = hInstance,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .lpszClassName = L"MyClass"
        };

        if (!RegisterClassW(&windowClass)) {
            throw std::runtime_error{ "Failed to register window class." };
        }

        Window const hwnd{ CreateWindowExW(0, windowClass.lpszClassName, L"MyWindow", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInstance, nullptr) };
        if (!hwnd) {
            throw std::runtime_error{ "Failed to create window." };
        }

        ShowWindow(hwnd.get(), nShowCmd);

#ifndef NDEBUG
        {
            ComPtr<ID3D12Debug5> debug;
            ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())), "Failed to get debug interface.");
            debug->EnableDebugLayer();
        }
#endif

        UINT dxgiFactoryFlags{ 0 };
#ifndef NDEBUG
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

        ComPtr<IDXGIFactory7> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(factory.GetAddressOf())), "Failed to create dxgi factory.");

        ComPtr<ID3D12Device9> device;
        ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf())), "Failed to create d3d device.");

#ifndef NDEBUG
        {
            ComPtr<ID3D12InfoQueue> infoQueue;
            ThrowIfFailed(device.As(&infoQueue), "Failed to get info queue.");
            ThrowIfFailed(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE), "Failed to set break on d3d12 debug error messages.");
            ThrowIfFailed(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE), "Failed to set break on d3d12 debug corruption messages.");
        }
#endif

        ComPtr<ID3D12CommandQueue> commandQueue;

        {
            D3D12_COMMAND_QUEUE_DESC constexpr commandQueueDesc{
                .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
                .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
                .NodeMask = 0
            };
            ThrowIfFailed(device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf())), "Failed to create command queue.");
        }

        BOOL isTearingSupported{ false };
        ThrowIfFailed(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &isTearingSupported, sizeof isTearingSupported), "Failed to check tearing support.");

        UINT swapChainFlags{ 0 };
        UINT presentFlags{ 0 };

        if (isTearingSupported) {
            swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
        }

        ComPtr<IDXGISwapChain4> swapChain;

        {
            DXGI_SWAP_CHAIN_DESC1 const swapChainDesc{
                .Width = 0,
                .Height = 0,
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .Stereo = FALSE,
                .SampleDesc = {
                    .Count = 1,
                    .Quality = 0
                },
                .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                .BufferCount = SWAP_CHAIN_BUFFER_COUNT,
                .Scaling = DXGI_SCALING_NONE,
                .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
                .Flags = swapChainFlags
            };
            ComPtr<IDXGISwapChain1> swapChain1;
            ThrowIfFailed(factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr, swapChain1.GetAddressOf()), "Failed to create swapchain.");
            ThrowIfFailed(swapChain1.As(&swapChain), "Failed to cast swap chain to newer interface.");
        }

        auto backBufIdx{ swapChain->GetCurrentBackBufferIndex() };

        ComPtr<ID3D12DescriptorHeap> rtvHeap;

        {
            D3D12_DESCRIPTOR_HEAP_DESC constexpr rtvHeapDesc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .NumDescriptors = 2,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                .NodeMask = 0
            };
            ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf())), "Failed to create rtv heap.");
        }

        auto const rtvHeapInc{ device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) };
        auto const rtvHeapCpuStart{ rtvHeap->GetCPUDescriptorHandleForHeapStart() };

        std::array<ComPtr<ID3D12Resource2>, SWAP_CHAIN_BUFFER_COUNT> backBuffers;
        std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, SWAP_CHAIN_BUFFER_COUNT> backBufferRTVs{};

        for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++) {
            ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(backBuffers[i].GetAddressOf())), "Failed to get backbuffer {} from swapchain.", i);
            backBufferRTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtvHeapCpuStart, static_cast<INT>(i), rtvHeapInc };

            D3D12_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
                .Format = SWAP_CHAIN_FORMAT,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D = {
                    .MipSlice = 0,
                    .PlaneSlice = 0
                }
            };

            device->CreateRenderTargetView(backBuffers[i].Get(), &rtvDesc, backBufferRTVs[i]);
        }

        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf())), "Failed to create command allocator.");

        ComPtr<ID3D12RootSignature> rootSig;

        {
            CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc{ 0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };

            ComPtr<ID3DBlob> rootSigBlob;
            ComPtr<ID3DBlob> errBlob;
            ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, rootSigBlob.GetAddressOf(), errBlob.GetAddressOf()), "Failed to serialize root signature. {}.", errBlob.Get());

            ThrowIfFailed(device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(rootSig.GetAddressOf())), "Failed to create root signature.");
        }

        ComPtr<ID3D12PipelineState> pso;

        {
            D3D12_INPUT_ELEMENT_DESC constexpr inputElementDesc{
                .SemanticName = "VERTEXPOS",
                .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32G32_FLOAT,
                .InputSlot = 0,
                .AlignedByteOffset = 0,
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                .InstanceDataStepRate = 0
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC const psoDesc{
                .pRootSignature = rootSig.Get(),
                .VS = CD3DX12_SHADER_BYTECODE{ gVSBin, ARRAYSIZE(gVSBin) },
                .PS = CD3DX12_SHADER_BYTECODE{ gPSBin, ARRAYSIZE(gPSBin) },
                .BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT },
                .SampleMask = UINT_MAX,
                .RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT },
                .InputLayout = { .pInputElementDescs = &inputElementDesc, .NumElements = 1 },
                .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
                .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                .NumRenderTargets = 1,
                .RTVFormats = { SWAP_CHAIN_FORMAT },
                .SampleDesc = { .Count = 1, .Quality = 0 },
                .NodeMask = 0,
                .CachedPSO = { nullptr, 0 },
                .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
            };

            ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.GetAddressOf())), "Failed to create pso.");
        }

        ComPtr<ID3D12GraphicsCommandList> commandList;
        ThrowIfFailed(device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(commandList.GetAddressOf())), "Failed to create command list.");

        ComPtr<ID3D12Fence1> fence;
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())), "Failed to create fence.");

        UINT64 fenceValue{ 1 };

        Event const fenceEvent{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
        if (!fenceEvent) {
            throw std::runtime_error{ "Failed to create fence event." };
        }

        auto const waitForGpu{
            [&] {
                ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValue), "Failed to signal fence.");

                if (fence->GetCompletedValue() < fenceValue) {
                    ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent.get()), "Failed to set on-completion event for gpu fence.");
                    WaitForSingleObject(fenceEvent.get(), INFINITE);
                }

                fenceValue += 1;
            }
        };

        std::array const vertices{
            Vec2{ 0, 0.5f },
            Vec2{ 0.5f, -0.5f },
            Vec2{ -0.5f, -0.5f }
        };

        CD3DX12_HEAP_PROPERTIES const defaultHeapProps{ D3D12_HEAP_TYPE_DEFAULT };
        auto const vertBufDesc{ CD3DX12_RESOURCE_DESC::Buffer(sizeof vertices) };

        ComPtr<ID3D12Resource> vertexBuffer;
        ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vertBufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(vertexBuffer.GetAddressOf())), "Failed to create vertex buffer.");

        {
            CD3DX12_HEAP_PROPERTIES const uploadHeapProps{ D3D12_HEAP_TYPE_UPLOAD };

            ComPtr<ID3D12Resource> vertexUploadBuffer;
            ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vertBufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(vertexUploadBuffer.GetAddressOf())), "Failed to create vertex upload buffer.");

            void* mappedVertexUploadBuffer;
            ThrowIfFailed(vertexUploadBuffer->Map(0, nullptr, &mappedVertexUploadBuffer), "Failed to map vertex upload buffer.");

            std::memcpy(mappedVertexUploadBuffer, vertices.data(), sizeof vertices);
            vertexUploadBuffer->Unmap(0, nullptr);

            ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pso.Get()), "Failed to reset command list before copying vertex buffer data.");

            commandList->CopyResource(vertexBuffer.Get(), vertexUploadBuffer.Get());

            auto const uploadBarrier{ CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) };
            commandList->ResourceBarrier(1, &uploadBarrier);

            ThrowIfFailed(commandList->Close(), "Failed to close command list before copying vertex buffer data.");

            commandQueue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{ commandList.Get() }.data());
            waitForGpu();
        }

        D3D12_VERTEX_BUFFER_VIEW const vertexBufferView{
            .BufferLocation = vertexBuffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof vertices,
            .StrideInBytes = sizeof(decltype(vertices)::value_type)
        };

        while (true) {
            MSG msg;

            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    waitForGpu();
                    return static_cast<int>(msg.wParam);
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            ThrowIfFailed(commandAllocator->Reset(), "Failed to reset command allocator.");
            ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pso.Get()), "Failed to reset command list.");

            commandList->SetGraphicsRootSignature(rootSig.Get());

            CD3DX12_VIEWPORT const viewport{ backBuffers[backBufIdx].Get() };
            commandList->RSSetViewports(1, &viewport);

            CD3DX12_RECT const scissorRect{ static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY), static_cast<LONG>(viewport.TopLeftX + viewport.Width), static_cast<LONG>(viewport.TopLeftY + viewport.Height) };
            commandList->RSSetScissorRects(1, &scissorRect);

            auto const swapChainRtvBarrier{ CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET) };
            commandList->ResourceBarrier(1, &swapChainRtvBarrier);

            float constexpr clearColor[]{ 0.2f, 0.3f, 0.3f, 1.f };
            commandList->ClearRenderTargetView(backBufferRTVs[backBufIdx], clearColor, 0, nullptr);
            commandList->OMSetRenderTargets(1, &backBufferRTVs[backBufIdx], FALSE, nullptr);

            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->DrawInstanced(3, 1, 0, 0);

            auto const swapChainPresentBarrier{ CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT) };
            commandList->ResourceBarrier(1, &swapChainPresentBarrier);

            ThrowIfFailed(commandList->Close(), "Failed to close command list in render loop.");

            ID3D12CommandList* const commandLists{ commandList.Get() };
            commandQueue->ExecuteCommandLists(1, &commandLists);

            ThrowIfFailed(swapChain->Present(0, presentFlags), "Failed to present.");

            waitForGpu();
            backBufIdx = swapChain->GetCurrentBackBufferIndex();
        }
    }
    catch (std::exception const& ex) {
        MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR);
        return -1;
    }
}
