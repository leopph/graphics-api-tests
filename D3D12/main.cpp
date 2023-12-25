#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <D3D12MemAlloc.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <cstring>
#include <limits>
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
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, SWAP_CHAIN_BUFFER_COUNT> backBufferRTVs{};

  for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++) {
    hr = swapChain->GetBuffer(i, IID_PPV_ARGS(backBuffers[i].GetAddressOf()));
    assert(SUCCEEDED(hr));

    backBufferRTVs[i].ptr = rtvHeapCpuStart.ptr + i * rtvHeapInc;

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
    D3D12_ROOT_PARAMETER1 constexpr rootParam{
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
      .Constants = {
        .ShaderRegister = 0,
        .RegisterSpace = 0,
        .Num32BitValues = 2
      },
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    };

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC const rootSigDesc{
      .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
      .Desc_1_1 = {
        .NumParameters = 1,
        .pParameters = &rootParam,
        .NumStaticSamplers = 0,
        .pStaticSamplers = nullptr,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
      }
    };

    ComPtr<ID3DBlob> rootSigBlob;
    hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, rootSigBlob.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    hr = device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                     IID_PPV_ARGS(rootSig.GetAddressOf()));
    assert(SUCCEEDED(hr));
  }

  ComPtr<ID3D12PipelineState> pso;

  {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC const pso_desc{
      .pRootSignature = rootSig.Get(),
      .VS = {gVSBin, ARRAYSIZE(gVSBin)},
      .PS = {gPSBin, ARRAYSIZE(gPSBin)},
      .DS = {nullptr, 0},
      .HS = {nullptr, 0},
      .GS = {nullptr, 0},
      .StreamOutput = {},
      .BlendState = {
        .AlphaToCoverageEnable = FALSE,
        .IndependentBlendEnable = FALSE,
        .RenderTarget = {
          D3D12_RENDER_TARGET_BLEND_DESC{
            .BlendEnable = FALSE,
            .LogicOpEnable = FALSE,
            .SrcBlend = D3D12_BLEND_ONE,
            .DestBlend = D3D12_BLEND_ZERO,
            .BlendOp = D3D12_BLEND_OP_ADD,
            .SrcBlendAlpha = D3D12_BLEND_ONE,
            .DestBlendAlpha = D3D12_BLEND_ONE,
            .BlendOpAlpha = D3D12_BLEND_OP_ADD,
            .LogicOp = D3D12_LOGIC_OP_NOOP,
            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
          }
        }
      },
      .SampleMask = std::numeric_limits<UINT>::max(),
      .RasterizerState = {
        .FillMode = D3D12_FILL_MODE_SOLID,
        .CullMode = D3D12_CULL_MODE_BACK,
        .FrontCounterClockwise = FALSE,
        .DepthBias = 0,
        .DepthBiasClamp = 0,
        .SlopeScaledDepthBias = 0,
        .DepthClipEnable = TRUE,
        .MultisampleEnable = FALSE,
        .AntialiasedLineEnable = FALSE,
        .ForcedSampleCount = 0,
        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
      },
      .DepthStencilState = {
        .DepthEnable = FALSE,
        .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
        .DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
        .StencilEnable = FALSE,
        .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
        .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
        .FrontFace = {
          .StencilFailOp = D3D12_STENCIL_OP_KEEP,
          .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
          .StencilPassOp = D3D12_STENCIL_OP_KEEP,
          .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        },
        .BackFace = {
          .StencilFailOp = D3D12_STENCIL_OP_KEEP,
          .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
          .StencilPassOp = D3D12_STENCIL_OP_KEEP,
          .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        }
      },
      .InputLayout = {nullptr, 0},
      .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
      .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
      .NumRenderTargets = 1,
      .RTVFormats = {SWAP_CHAIN_FORMAT},
      .DSVFormat = DXGI_FORMAT_UNKNOWN,
      .SampleDesc = {
        .Count = 1,
        .Quality = 0
      },
      .NodeMask = 0,
      .CachedPSO = {nullptr, 0},
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
    };

    hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(pso.GetAddressOf()));
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

  D3D12_RESOURCE_DESC1 constexpr uploadResDesc{
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
    .Width = uploadBufSize,
    .Height = 1,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {
      .Count = 1,
      .Quality = 0
    },
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    .Flags = D3D12_RESOURCE_FLAG_NONE,
    .SamplerFeedbackMipRegion = {
      .Width = 0,
      .Height = 0,
      .Depth = 0
    }
  };

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

  D3D12_RESOURCE_DESC1 constexpr vertBufDesc{
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
    .Width = sizeof(vertices),
    .Height = 1,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {
      .Count = 1,
      .Quality = 0
    },
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    .Flags = D3D12_RESOURCE_FLAG_NONE,
    .SamplerFeedbackMipRegion = {
      .Width = 0,
      .Height = 0,
      .Depth = 0
    }
  };

  D3D12MA::ALLOCATION_DESC constexpr vertBufAllocDesc{
    .Flags = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    .ExtraHeapFlags = D3D12_HEAP_FLAG_NONE,
    .CustomPool = nullptr,
    .pPrivateData = nullptr
  };

  ComPtr<D3D12MA::Allocation> vertBufAlloc;
  hr = allocator->CreateResource2(&vertBufAllocDesc, &vertBufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, vertBufAlloc.GetAddressOf(), IID_NULL, nullptr);
  assert(SUCCEEDED(hr));

  // UPLOAD VERTEX DATA

  std::memcpy(mappedUploadBuf, vertices.data(), sizeof(vertices));

  hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
  assert(SUCCEEDED(hr));

  cmdLists[frameIdx]->CopyBufferRegion(vertBufAlloc->GetResource(), 0, uploadBuf.Get(), 0, sizeof(vertices));

  D3D12_RESOURCE_BARRIER const vertexPostUploadBarrier{
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = {
      .pResource = vertBufAlloc->GetResource(),
      .Subresource = 0,
      .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
      .StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    }
  };
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

  D3D12_RESOURCE_DESC1 constexpr texDesc{
    .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    .Alignment = 0,
    .Width = 1,
    .Height = 1,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {
      .Count = 1,
      .Quality = 0
    },
    .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
    .Flags = D3D12_RESOURCE_FLAG_NONE,
    .SamplerFeedbackMipRegion = {
      .Width = 0,
      .Height = 0,
      .Depth = 0
    }
  };

  ComPtr<D3D12MA::Allocation> texAlloc;
  hr = allocator->CreateResource2(&texAllocDesc, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, texAlloc.GetAddressOf(), IID_NULL, nullptr);
  assert(SUCCEEDED(hr));

  // UPLOAD TEXTURE DATA

  std::memcpy(mappedUploadBuf, texColor.data(), sizeof(texColor));

  hr = cmdLists[frameIdx]->Reset(cmdAllocators[frameIdx].Get(), pso.Get());
  assert(SUCCEEDED(hr));

  D3D12_TEXTURE_COPY_LOCATION const srcTexCopyLoc{
    .pResource = uploadBuf.Get(),
    .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
    .PlacedFootprint = {
      .Offset = 0,
      .Footprint = {
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Width = 1,
        .Height = 1,
        .Depth = 1,
        .RowPitch = sizeof(texColor)
      }
    }
  };

  D3D12_TEXTURE_COPY_LOCATION const dstTexCopyLoc{
    .pResource = texAlloc->GetResource(),
    .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
    .SubresourceIndex = 0
  };

  cmdLists[frameIdx]->CopyTextureRegion(&dstTexCopyLoc, 0, 0, 0, &srcTexCopyLoc, nullptr);

  D3D12_RESOURCE_BARRIER const texPostUploadBarrier{
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = {
      .pResource = texAlloc->GetResource(),
      .Subresource = 0,
      .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
      .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    }
  };

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

  device->CreateShaderResourceView(vertBufAlloc->GetResource(), &vertBufSrvDesc, D3D12_CPU_DESCRIPTOR_HANDLE{resHeapCpuStart.ptr + vbSrvIdx * resHeapInc});

  D3D12_SHADER_RESOURCE_VIEW_DESC const texSrvDesc{
    .Format = texDesc.Format,
    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Texture2D = {
      .MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0, .ResourceMinLODClamp = 0.0f
    }
  };

  auto constexpr texSrvIdx{vbSrvIdx + 1};

  device->CreateShaderResourceView(texAlloc->GetResource(), &texSrvDesc, D3D12_CPU_DESCRIPTOR_HANDLE{resHeapCpuStart.ptr + texSrvIdx * resHeapInc});

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

    auto const back_buf_desc{backBuffers[backBufIdx]->GetDesc1()};

    D3D12_VIEWPORT const viewport{
      .TopLeftX = 0,
      .TopLeftY = 0,
      .Width = static_cast<FLOAT>(back_buf_desc.Width),
      .Height = static_cast<FLOAT>(back_buf_desc.Height),
      .MinDepth = 0,
      .MaxDepth = 1
    };
    cmdLists[frameIdx]->RSSetViewports(1, &viewport);

    D3D12_RECT const scissorRect{
      .left = static_cast<LONG>(viewport.TopLeftX),
      .top = static_cast<LONG>(viewport.TopLeftY),
      .right = static_cast<LONG>(viewport.TopLeftX + viewport.Width),
      .bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height)
    };
    cmdLists[frameIdx]->RSSetScissorRects(1, &scissorRect);

    D3D12_RESOURCE_BARRIER const swapChainRtvBarrier{
      .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
      .Transition = {
        .pResource = backBuffers[backBufIdx].Get(),
        .Subresource = 0,
        .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
        .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
      }
    };
    cmdLists[frameIdx]->ResourceBarrier(1, &swapChainRtvBarrier);

    float constexpr clearColor[]{0.2f, 0.3f, 0.3f, 1.f};
    cmdLists[frameIdx]->ClearRenderTargetView(backBufferRTVs[backBufIdx], clearColor, 0, nullptr);
    cmdLists[frameIdx]->OMSetRenderTargets(1, &backBufferRTVs[backBufIdx], FALSE, nullptr);

    cmdLists[frameIdx]->ExecuteBundle(bundle.Get());

    D3D12_RESOURCE_BARRIER const swapChainPresentBarrier{
      .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
      .Transition = {
        .pResource = backBuffers[backBufIdx].Get(),
        .Subresource = 0,
        .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
        .StateAfter = D3D12_RESOURCE_STATE_PRESENT
      }
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
