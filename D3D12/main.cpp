#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <d3dx12.h>
#include <D3D12MemAlloc.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <type_traits>

#ifndef NDEBUG
#include "shaders/generated/PSBinDebug.h"
#include "shaders/generated/VSBinDebug.h"
#else
#include "shaders/generated/VSBin.h"
#include "shaders/generated/PSBin.h"
#endif

#include "shaders/interop.h"


extern "C" {
__declspec(dllexport) extern UINT const D3D12SDKVersion{D3D12_SDK_VERSION};
__declspec(dllexport) extern char const* D3D12SDKPath{".\\D3D12\\"};
}


namespace {
  auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
    if (msg == WM_CLOSE) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}


auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance,
                     [[maybe_unused]] _In_ wchar_t* const lpCmdLine, _In_ int const nShowCmd) -> int {
  WNDCLASSW const windowClass{
    .lpfnWndProc = &WindowProc,
    .hInstance = hInstance,
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"D3D12Test"
  };

  auto const result{RegisterClassW(&windowClass)};
  assert(result);

  std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) -> void {
    if (hwnd) {
      DestroyWindow(hwnd);
    }
  })> const hwnd{
    CreateWindowExW(0, windowClass.lpszClassName, L"D3D12Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr)
  };

  assert(hwnd);

  ShowWindow(hwnd.get(), nShowCmd);

  HRESULT hr{};

  using Microsoft::WRL::ComPtr;

#ifndef NDEBUG
  {
    ComPtr<ID3D12Debug5> debug;
    hr = D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()));
    assert(SUCCEEDED(hr));

    debug->EnableDebugLayer();
  }
#endif

  UINT dxgiFactoryFlags{0};
#ifndef NDEBUG
  dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  ComPtr<IDXGIFactory7> factory;
  hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(factory.GetAddressOf()));
  assert(SUCCEEDED(hr));

  ComPtr<ID3D12Device9> device;
  hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  {
    ComPtr<ID3D12InfoQueue> infoQueue;
    hr = device.As(&infoQueue);
    assert(SUCCEEDED(hr));

    hr = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    assert(SUCCEEDED(hr));

    hr = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    assert(SUCCEEDED(hr));
  }
#endif

  ComPtr<D3D12MA::Allocator> allocator;

  {
    ComPtr<IDXGIAdapter4> adapter4;
    hr = factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(adapter4.GetAddressOf()));
    assert(SUCCEEDED(hr));

    D3D12MA::ALLOCATOR_DESC const allocatorDesc{
      .Flags = D3D12MA::ALLOCATOR_FLAG_NONE,
      .pDevice = device.Get(),
      .PreferredBlockSize = 0,
      .pAllocationCallbacks = nullptr,
      .pAdapter = adapter4.Get()
    };

    hr = CreateAllocator(&allocatorDesc, allocator.GetAddressOf());
    assert(SUCCEEDED(hr));
  }

  ComPtr<ID3D12CommandQueue> commandQueue;

  {
    D3D12_COMMAND_QUEUE_DESC constexpr commandQueueDesc{
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0
    };

    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  constexpr auto SWAP_CHAIN_BUFFER_COUNT{2};
  constexpr auto SWAP_CHAIN_FORMAT{DXGI_FORMAT_R8G8B8A8_UNORM};
  ComPtr<IDXGISwapChain4> swapChain;

  {
    DXGI_SWAP_CHAIN_DESC1 constexpr swapChainDesc{
      .Width = 0,
      .Height = 0,
      .Format = SWAP_CHAIN_FORMAT,
      .Stereo = FALSE,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = SWAP_CHAIN_BUFFER_COUNT,
      .Scaling = DXGI_SCALING_NONE,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
      .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
      .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
    };

    ComPtr<IDXGISwapChain1> tmpSwapChain;
    hr = factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr,
                                         tmpSwapChain.GetAddressOf());
    assert(SUCCEEDED(hr));

    hr = tmpSwapChain.As(&swapChain);
    assert(SUCCEEDED(hr));
  }

  auto backBufIdx{swapChain->GetCurrentBackBufferIndex()};

  ComPtr<ID3D12DescriptorHeap> rtvHeap;

  {
    D3D12_DESCRIPTOR_HEAP_DESC constexpr rtvHeapDesc{
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
      .NodeMask = 0
    };

    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  auto const rtvHeapInc{device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)};
  auto const rtvHeapCpuStart{rtvHeap->GetCPUDescriptorHandleForHeapStart()};

  std::array<ComPtr<ID3D12Resource2>, SWAP_CHAIN_BUFFER_COUNT> backBuffers;
  std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, SWAP_CHAIN_BUFFER_COUNT> backBufferRTVs{};

  for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++) {
    hr = swapChain->GetBuffer(i, IID_PPV_ARGS(backBuffers[i].GetAddressOf()));
    assert(SUCCEEDED(hr));

    backBufferRTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE{rtvHeapCpuStart, static_cast<INT>(i), rtvHeapInc};

    D3D12_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
      .Format = SWAP_CHAIN_FORMAT,
      .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
      .Texture2D = {.MipSlice = 0, .PlaneSlice = 0}
    };

    device->CreateRenderTargetView(backBuffers[i].Get(), &rtvDesc, backBufferRTVs[i]);
  }

  constexpr auto MAX_FRAMES_IN_FLIGHT{2};
  UINT64 thisFrameFenceValue{MAX_FRAMES_IN_FLIGHT - 1};

  ComPtr<ID3D12Fence1> fence;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
  assert(SUCCEEDED(hr));

  auto const signalAndWaitFence{
    [&](UINT64 const signalValue, UINT64 const waitValue) {
      hr = commandQueue->Signal(fence.Get(), signalValue);
      assert(SUCCEEDED(hr));

      if (fence->GetCompletedValue() < waitValue) {
        hr = fence->SetEventOnCompletion(waitValue, nullptr);
        assert(SUCCEEDED(hr));
      }
    }
  };

  auto const waitForGpuCompletion{
    [&] {
      auto const signalValue{++thisFrameFenceValue};
      auto const waitValue{signalValue};
      signalAndWaitFence(signalValue, waitValue);
    }
  };

  auto const waitForInFlightFrames{
    [&] {
      auto const signalValue{++thisFrameFenceValue};
      auto const waitValue{signalValue - MAX_FRAMES_IN_FLIGHT + 1};
      signalAndWaitFence(signalValue, waitValue);
    }
  };

  UINT64 frameIdx{0};

  std::array<ComPtr<ID3D12CommandAllocator>, MAX_FRAMES_IN_FLIGHT> cmdAllocators;
  std::array<ComPtr<ID3D12GraphicsCommandList6>, MAX_FRAMES_IN_FLIGHT> cmdLists;

  for (auto i{0}; i < MAX_FRAMES_IN_FLIGHT; i++) {
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdAllocators[i].GetAddressOf()));
    assert(SUCCEEDED(hr));

    hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(cmdLists[i].GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  ComPtr<ID3D12RootSignature> rootSig;

  {
    CD3DX12_ROOT_PARAMETER1 rootParam;
    rootParam.InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC const rootSigDesc{1, &rootParam, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED};

    ComPtr<ID3DBlob> rootSigBlob;
    hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, rootSigBlob.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    hr = device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                     IID_PPV_ARGS(rootSig.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  ComPtr<ID3D12PipelineState> pso;

  {
    struct {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSig;
      CD3DX12_PIPELINE_STATE_STREAM_VS vs;
      CD3DX12_PIPELINE_STATE_STREAM_PS ps;
      CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
    } stream{
        .rootSig = rootSig.Get(),
        .vs = CD3DX12_SHADER_BYTECODE{gVSBin, ARRAYSIZE(gVSBin)},
        .ps = CD3DX12_SHADER_BYTECODE{gPSBin, ARRAYSIZE(gPSBin)},
        .rtvFormats = {D3D12_RT_FORMAT_ARRAY{.RTFormats = {SWAP_CHAIN_FORMAT}, .NumRenderTargets = 1}}
      };

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{
      .SizeInBytes = sizeof(stream),
      .pPipelineStateSubobjectStream = &stream
    };

    hr = device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(pso.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  // CREATE UPLOAD BUFFER

  D3D12MA::ALLOCATION_DESC constexpr uploadAllocDesc{
    .Flags = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_UPLOAD,
    .ExtraHeapFlags = D3D12_HEAP_FLAG_NONE,
    .CustomPool = nullptr,
    .pPrivateData = nullptr
  };

  auto constexpr uploadBufSize{64 * 1024};
  auto const uploadResDesc{CD3DX12_RESOURCE_DESC1::Buffer(uploadBufSize)};

  ComPtr<D3D12MA::Allocation> uploadAlloc;
  ComPtr<ID3D12Resource2> uploadBuf;

  hr = allocator->CreateResource2(&uploadAllocDesc, &uploadResDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &uploadAlloc, IID_PPV_ARGS(&uploadBuf));
  assert(SUCCEEDED(hr));

  void* mappedUploadBuf;
  hr = uploadBuf->Map(0, nullptr, &mappedUploadBuf);
  assert(SUCCEEDED(hr));

  // CREATE VERTEX BUFFER

  std::array constexpr vertices{
    std::array{0.0f, 0.5f},
    std::array{0.5f, -0.5f},
    std::array{-0.5f, -0.5f}
  };

  auto const vertBufDesc{CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices))};

  D3D12MA::ALLOCATION_DESC constexpr vertBufAllocDesc{
    .Flags = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    .ExtraHeapFlags = D3D12_HEAP_FLAG_NONE,
    .CustomPool = nullptr,
    .pPrivateData = nullptr
  };

  ComPtr<D3D12MA::Allocation> vertBufAlloc;
  hr = allocator->CreateResource(&vertBufAllocDesc, &vertBufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, vertBufAlloc.GetAddressOf(), IID_NULL, nullptr);
  assert(SUCCEEDED(hr));

  // UPLOAD VERTEX DATA

  std::memcpy(mappedUploadBuf, vertices.data(), sizeof(vertices));

  hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
  assert(SUCCEEDED(hr));

  cmdLists[frameIdx]->CopyBufferRegion(vertBufAlloc->GetResource(), 0, uploadBuf.Get(), 0, sizeof(vertices));

  auto const vertexPostUploadBarrier{CD3DX12_RESOURCE_BARRIER::Transition(vertBufAlloc->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};

  cmdLists[frameIdx]->ResourceBarrier(1, &vertexPostUploadBarrier);

  hr = cmdLists[frameIdx]->Close();
  assert(SUCCEEDED(hr));

  commandQueue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{cmdLists[frameIdx].Get()}.data());
  waitForGpuCompletion();

  // CREATE TEXTURE

  std::array<unsigned char, 4> constexpr texColor{255, 0, 255, 255};

  D3D12MA::ALLOCATION_DESC constexpr texAllocDesc{
    .Flags = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    .ExtraHeapFlags = D3D12_HEAP_FLAG_NONE,
    .CustomPool = nullptr,
    .pPrivateData = nullptr
  };

  auto const texDesc{CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1)};

  ComPtr<D3D12MA::Allocation> texAlloc;
  hr = allocator->CreateResource(&texAllocDesc, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, texAlloc.GetAddressOf(), IID_NULL, nullptr);
  assert(SUCCEEDED(hr));

  // UPLOAD TEXTURE DATA

  std::memcpy(mappedUploadBuf, texColor.data(), sizeof(texColor));

  hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
  assert(SUCCEEDED(hr));

  CD3DX12_TEXTURE_COPY_LOCATION const srcTexCopyLoc{uploadBuf.Get(), D3D12_PLACED_SUBRESOURCE_FOOTPRINT{.Offset = 0, .Footprint = CD3DX12_SUBRESOURCE_FOOTPRINT{DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, sizeof(texColor)}}};
  CD3DX12_TEXTURE_COPY_LOCATION const dstTexCopyLoc{texAlloc->GetResource(), 0};
  cmdLists[frameIdx]->CopyTextureRegion(&dstTexCopyLoc, 0, 0, 0, &srcTexCopyLoc, nullptr);

  auto const texPostUploadBarrier{CD3DX12_RESOURCE_BARRIER::Transition(texAlloc->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};

  cmdLists[frameIdx]->ResourceBarrier(1, &texPostUploadBarrier);

  hr = cmdLists[frameIdx]->Close();
  assert(SUCCEEDED(hr));

  commandQueue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{cmdLists[frameIdx].Get()}.data());
  waitForGpuCompletion();

  D3D12_DESCRIPTOR_HEAP_DESC constexpr resHeapDesc{
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    .NodeMask = 0
  };

  ComPtr<ID3D12DescriptorHeap> resHeap;
  hr = device->CreateDescriptorHeap(&resHeapDesc, IID_PPV_ARGS(resHeap.GetAddressOf()));
  assert(SUCCEEDED(hr));

  auto const resHeapInc{device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)};
  auto const resHeapCpuStart{resHeap->GetCPUDescriptorHandleForHeapStart()};

  D3D12_SHADER_RESOURCE_VIEW_DESC constexpr vertBufSrvDesc{
    .Format = DXGI_FORMAT_UNKNOWN,
    .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Buffer = {
      .FirstElement = 0,
      .NumElements = static_cast<UINT>(std::size(vertices)),
      .StructureByteStride = sizeof(decltype(vertices)::value_type),
      .Flags = D3D12_BUFFER_SRV_FLAG_NONE
    }
  };

  auto constexpr vbSrvIdx{0};

  device->CreateShaderResourceView(vertBufAlloc->GetResource(), &vertBufSrvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE{resHeapCpuStart, vbSrvIdx, resHeapInc});

  D3D12_SHADER_RESOURCE_VIEW_DESC const texSrvDesc{
    .Format = texDesc.Format,
    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Texture2D = {
      .MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0, .ResourceMinLODClamp = 0.0f
    }
  };

  auto constexpr texSrvIdx{vbSrvIdx + 1};

  device->CreateShaderResourceView(texAlloc->GetResource(), &texSrvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE{resHeapCpuStart, texSrvIdx, resHeapInc});

  ComPtr<ID3D12CommandAllocator> bundleAllocator;
  hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&bundleAllocator));
  assert(SUCCEEDED(hr));

  ComPtr<ID3D12GraphicsCommandList6> bundle;
  hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundleAllocator.Get(), pso.Get(), IID_PPV_ARGS(&bundle));
  assert(SUCCEEDED(hr));

  bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  bundle->DrawInstanced(3, 1, 0, 0);

  hr = bundle->Close();
  assert(SUCCEEDED(hr));

  while (true) {
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        waitForGpuCompletion();
        return static_cast<int>(msg.wParam);
      }

      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    hr = cmdAllocators[frameIdx]->Reset();
    assert(SUCCEEDED(hr));

    hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
    assert(SUCCEEDED(hr));

    cmdLists[frameIdx]->SetDescriptorHeaps(1, resHeap.GetAddressOf());
    cmdLists[frameIdx]->SetGraphicsRootSignature(rootSig.Get());
    cmdLists[frameIdx]->SetGraphicsRoot32BitConstants(0, 2, std::array{vbSrvIdx, texSrvIdx}.data(), 0);

    CD3DX12_VIEWPORT const viewport{backBuffers[backBufIdx].Get()};
    cmdLists[frameIdx]->RSSetViewports(1, &viewport);

    CD3DX12_RECT const scissorRect{
      static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
      static_cast<LONG>(viewport.TopLeftX + viewport.Width), static_cast<LONG>(viewport.TopLeftY + viewport.Height)
    };
    cmdLists[frameIdx]->RSSetScissorRects(1, &scissorRect);

    auto const swapChainRtvBarrier{
      CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufIdx].Get(), D3D12_RESOURCE_STATE_PRESENT,
                                           D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    cmdLists[frameIdx]->ResourceBarrier(1, &swapChainRtvBarrier);

    float constexpr clearColor[]{0.2f, 0.3f, 0.3f, 1.f};
    cmdLists[frameIdx]->ClearRenderTargetView(backBufferRTVs[backBufIdx], clearColor, 0, nullptr);
    cmdLists[frameIdx]->OMSetRenderTargets(1, &backBufferRTVs[backBufIdx], FALSE, nullptr);

    cmdLists[frameIdx]->ExecuteBundle(bundle.Get());

    auto const swapChainPresentBarrier{
      CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                           D3D12_RESOURCE_STATE_PRESENT)
    };
    cmdLists[frameIdx]->ResourceBarrier(1, &swapChainPresentBarrier);

    hr = cmdLists[frameIdx]->Close();
    assert(SUCCEEDED(hr));

    commandQueue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{cmdLists[frameIdx].Get()}.data());

    hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    assert(SUCCEEDED(hr));

    waitForInFlightFrames();
    backBufIdx = swapChain->GetCurrentBackBufferIndex();
    frameIdx = (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
  }
}
