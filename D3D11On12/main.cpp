#include <Windows.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <memory>

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

auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance, [[maybe_unused]] _In_ wchar_t* const lpCmdLine, _In_ int const nShowCmd) -> int {
  // CREATE WINDOW

  WNDCLASSW const windowClass{
    .lpfnWndProc = &WindowProc,
    .hInstance = hInstance,
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"D3D11On12Test"
  };

  auto const result{RegisterClassW(&windowClass)};
  assert(result);

  std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) -> void { if (hwnd) { DestroyWindow(hwnd); } })> const hwnd{CreateWindowExW(0, windowClass.lpszClassName, L"D3D11On12Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr)};
  assert(hwnd);

  ShowWindow(hwnd.get(), nShowCmd);

  HRESULT hr{};

  using Microsoft::WRL::ComPtr;

  // CREATE D3D12 DEVICE

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

  // CREATE D3D12 COMMAND ALLOCATOR AND LISTS

  constexpr auto MAX_FRAMES_IN_FLIGHT{2};

  D3D12_COMMAND_QUEUE_DESC constexpr commandQueueDesc{
    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
    .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    .NodeMask = 0
  };

  ComPtr<ID3D12CommandQueue> cmdQueue;
  hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&cmdQueue));
  assert(SUCCEEDED(hr));

  std::array<ComPtr<ID3D12CommandAllocator>, MAX_FRAMES_IN_FLIGHT> cmdAllocs;
  std::array<ComPtr<ID3D12GraphicsCommandList7>, MAX_FRAMES_IN_FLIGHT> cmdLists;

  for (auto i{0}; i < MAX_FRAMES_IN_FLIGHT; i++) {
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocs[i]));
    assert(SUCCEEDED(hr));

    hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdLists[i]));
    assert(SUCCEEDED(hr));
  }

  // CREATE D3D11 DEVICE

  UINT creationFlags{0};
#ifndef NDEBUG
  creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> imCtx;
  hr = D3D11On12CreateDevice(device.Get(), creationFlags, nullptr, 0, std::array<IUnknown*, 1>{cmdQueue.Get()}.data(), 1, 0, &device11, &imCtx, nullptr);
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  ComPtr<ID3D11Debug> d3dDebug;
  hr = device11.As(&d3dDebug);
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11InfoQueue> d3dInfoQueue;
  hr = d3dDebug.As<ID3D11InfoQueue>(&d3dInfoQueue);
  assert(SUCCEEDED(hr));

  hr = d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
  assert(SUCCEEDED(hr));

  hr = d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
  assert(SUCCEEDED(hr));
#endif

  // GET D3D11On12 DEVICE

  ComPtr<ID3D11On12Device2> device11On12;
  hr = device11.As(&device11On12);
  assert(SUCCEEDED(hr));

  // CREATE D3D12 FENCE

  UINT64 frameFenceVal{MAX_FRAMES_IN_FLIGHT - 1};

  ComPtr<ID3D12Fence1> frameFence;
  hr = device->CreateFence(frameFenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameFence));
  assert(SUCCEEDED(hr));

  hr = frameFence->SetName(L"Frame Fence");
  assert(SUCCEEDED(hr));

  UINT64 miscFenceVal{0};

  ComPtr<ID3D12Fence1> miscFence;
  hr = device->CreateFence(miscFenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&miscFence));
  assert(SUCCEEDED(hr));

  hr = miscFence->SetName(L"Misc Fence");
  assert(SUCCEEDED(hr));

  auto const signalAndWaitFence{
    [&cmdQueue](ID3D12Fence* const fence, UINT64 const signalValue, UINT64 const waitValue) {
      [[maybe_unused]] auto hr1{cmdQueue->Signal(fence, signalValue)};
      assert(SUCCEEDED(hr1));

      if (fence->GetCompletedValue() < waitValue) {
        hr1 = fence->SetEventOnCompletion(waitValue, nullptr);
        assert(SUCCEEDED(hr1));
      }
    }
  };

  auto const waitForInFlightFrameLimit{
    [&] {
      auto const signalValue{++frameFenceVal};
      auto const waitValue{signalValue - MAX_FRAMES_IN_FLIGHT + 1};
      signalAndWaitFence(frameFence.Get(), signalValue, waitValue);
    }
  };

  auto const waitForAllFrames{
    [&] {
      auto const signalValue{++frameFenceVal};
      auto const waitValue{signalValue};
      signalAndWaitFence(frameFence.Get(), signalValue, waitValue);
    }
  };

  UINT64 frameIdx{0};

  // CREATE SWAPCHAIN

  constexpr auto SWAP_CHAIN_BUFFER_COUNT{2};
  constexpr auto SWAP_CHAIN_FORMAT{DXGI_FORMAT_R8G8B8A8_UNORM};
  DXGI_SWAP_CHAIN_DESC1 constexpr swapChainDesc{
    .Width = 1,
    .Height = 1,
    .Format = SWAP_CHAIN_FORMAT,
    .Stereo = FALSE,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = SWAP_CHAIN_BUFFER_COUNT,
    .Scaling = DXGI_SCALING_STRETCH,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
    .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
  };

  ComPtr<IDXGISwapChain1> tmpSwapChain;
  hr = factory->CreateSwapChainForHwnd(cmdQueue.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr, tmpSwapChain.GetAddressOf());
  assert(SUCCEEDED(hr));

  ComPtr<IDXGISwapChain4> swapChain;
  hr = tmpSwapChain.As(&swapChain);
  assert(SUCCEEDED(hr));

  auto backBufIdx{swapChain->GetCurrentBackBufferIndex()};

  // CREATE D3D12 SWAPCHAIN RTVs

  /*D3D12_DESCRIPTOR_HEAP_DESC constexpr rtvHeapDesc{
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
    .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0
  };

  ComPtr<ID3D12DescriptorHeap> rtvHeap;
  hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()));
  assert(SUCCEEDED(hr));

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
  }*/

  // CREATE D3D11 SWAPCHAIN RTVs

  std::array<ComPtr<ID3D12Resource2>, SWAP_CHAIN_BUFFER_COUNT> swapChainBufs;
  std::array<ComPtr<ID3D11Texture2D>, SWAP_CHAIN_BUFFER_COUNT> swapChainBufs11;
  std::array<ComPtr<ID3D11RenderTargetView>, SWAP_CHAIN_BUFFER_COUNT> rtvs11;

  for (auto i{0}; i < SWAP_CHAIN_BUFFER_COUNT; i++) {
    hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBufs[i]));
    assert(SUCCEEDED(hr));

    D3D11_RESOURCE_FLAGS constexpr resFlags11{
      .BindFlags = D3D11_BIND_RENDER_TARGET,
      .MiscFlags = 0,
      .CPUAccessFlags = 0,
      .StructureByteStride = 0
    };

    hr = device11On12->CreateWrappedResource(swapChainBufs[i].Get(), &resFlags11, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&swapChainBufs11[i]));
    assert(SUCCEEDED(hr));

    D3D11_RENDER_TARGET_VIEW_DESC constexpr rtvDesc11{
      .Format = SWAP_CHAIN_FORMAT,
      .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
      .Texture2D = {.MipSlice = 0}
    };

    hr = device11->CreateRenderTargetView(swapChainBufs11[i].Get(), &rtvDesc11, &rtvs11[i]);
    assert(SUCCEEDED(hr));
  }

  D3D11_TEXTURE2D_DESC constexpr tex2dDesc{
    .Width = 1,
    .Height = 1,
    .MipLevels = 1,
    .ArraySize = 1,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Usage = D3D11_USAGE_DEFAULT,
    .BindFlags = 0,
    .CPUAccessFlags = 0,
    .MiscFlags = 0
  };

  std::array<unsigned char, 4> constexpr texData{255, 0, 255, 255};

  D3D11_SUBRESOURCE_DATA const subresData{
    .pSysMem = texData.data(),
    .SysMemPitch = sizeof(texData),
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Texture2D> tex2d;
  hr = device11->CreateTexture2D(&tex2dDesc, &subresData, &tex2d);
  assert(SUCCEEDED(hr));

  while (true) {
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        waitForAllFrames();
        return static_cast<int>(msg.wParam);
      }

      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    /*device11On12->AcquireWrappedResources(std::array<ID3D11Resource*, 1>{swapChainBufs11[backBufIdx].Get()}.data(), 1);
    imCtx->ClearRenderTargetView(rtvs11[backBufIdx].Get(), std::array{1.0f, 0.0f, 1.0f, 1.0f}.data());
    device11On12->ReleaseWrappedResources(std::array<ID3D11Resource*, 1>{swapChainBufs11[backBufIdx].Get()}.data(), 1);
    imCtx->Flush();*/

    ComPtr<ID3D12Resource> tex2d12;
    hr = device11On12->UnwrapUnderlyingResource(tex2d.Get(), cmdQueue.Get(), IID_PPV_ARGS(&tex2d12));
    assert(SUCCEEDED(hr));

    std::array<D3D12_RESOURCE_BARRIER, 2> const beforeBarriers{
      CD3DX12_RESOURCE_BARRIER::Transition(tex2d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
      CD3DX12_RESOURCE_BARRIER::Transition(swapChainBufs[backBufIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
    };

    std::array<D3D12_RESOURCE_BARRIER, 2> const afterBarriers{
      CD3DX12_RESOURCE_BARRIER::Transition(tex2d12.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
      CD3DX12_RESOURCE_BARRIER::Transition(swapChainBufs[backBufIdx].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
    };

    hr = cmdAllocs[frameIdx]->Reset();
    assert(SUCCEEDED(hr));

    hr = cmdLists[frameIdx]->Reset(cmdAllocs[frameIdx].Get(), nullptr);
    assert(SUCCEEDED(hr));

    cmdLists[frameIdx]->ResourceBarrier(static_cast<UINT>(std::size(beforeBarriers)), beforeBarriers.data());
    cmdLists[frameIdx]->CopyResource(swapChainBufs[backBufIdx].Get(), tex2d12.Get());
    cmdLists[frameIdx]->ResourceBarrier(static_cast<UINT>(std::size(afterBarriers)), afterBarriers.data());

    hr = cmdLists[frameIdx]->Close();
    assert(SUCCEEDED(hr));

    cmdQueue->ExecuteCommandLists(1, CommandListCast(cmdLists[frameIdx].GetAddressOf()));

    ++miscFenceVal;
    hr = cmdQueue->Signal(miscFence.Get(), miscFenceVal);
    assert(SUCCEEDED(hr));

    hr = device11On12->ReturnUnderlyingResource(tex2d.Get(), 1, &miscFenceVal, std::array<ID3D12Fence*, 1>{miscFence.Get()}.data());
    assert(SUCCEEDED(hr));

    hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    assert(SUCCEEDED(hr));

    waitForInFlightFrameLimit();
    backBufIdx = swapChain->GetCurrentBackBufferIndex();
    frameIdx = (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
  }
}
