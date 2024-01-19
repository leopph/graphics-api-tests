/* D3D12 test project demonstrating
 * - multiple geometry pipeline methods
 *   - vertex pushing
 *   - vertex pulling
 * - multiple resource binding methods
 *   - bindful descriptors
 *   - bindless descriptors using SM 5.1 dynamic indexing and unbounded arrays
 *   - bindless descriptors using SM 6.6 dynamic resources
 * - multiple barrier usage methods
 *   - legacy resource barriers
 *   - enhanced barriers
 * - multiple fullscreen methods
 *   - using a fullscreen swap chain
 *   - using a windowed swap chain and a screen sized window
 *
 * Define NO_VERTEX_PULLING to prevent reading vertex buffers as shader resources
 *
 * Define NO_DYNAMIC_RESOURCES to prevent the use of SM 6.6 dynamic resources even on supported hardware.
 * Define NO_DYNAMIC_INDEXING to prevent the use of SM 5.1 dynamic indexing and unbounded arrays.
 * Define both NO_DYNAMIC_RESOURCES and NO_DYNAMIC_INDEXING to force the use of the traditional bindful approach.
 *
 * Define NO_ENHANCED_BARRIERS to prevent to use of enhanced barriers even on supported hardware.
 *
 * Define USE_FULLSCREEN_SWAP_CHAIN to use a fullscreen swap chain.
 */

// Uncomment this if you want to opt out of reading vertex buffers as shader resources
// #define NO_VERTEX_PULLING

// Uncomment this if you want to opt out of using SM 6.6 dynamic resources
// #define NO_DYNAMIC_RESOURCES

// Uncomment this if you want to opt out of using SM 5.1 dynamic indexing and unbounded arrays
// #define NO_DYNAMIC_INDEXING

// Uncomment this if you want to opt out of using enhanced barriers
// #define NO_ENHANCED_BARRIERS

// Uncomment this if you want to use a fullscreen swap chain
// #define USE_FULLSCREEN_SWAP_CHAIN

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>

#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#define MAKE_SHADER_INCLUDE_PATH5(x) #x
#define MAKE_SHADER_INCLUDE_PATH4(x) MAKE_SHADER_INCLUDE_PATH5(x)
#define MAKE_SHADER_INCLUDE_PATH3(x, y, z) MAKE_SHADER_INCLUDE_PATH4(x ## y ## z)
#define MAKE_SHADER_INCLUDE_PATH2(x, y, z) MAKE_SHADER_INCLUDE_PATH3(x, y, z)
#ifndef NDEBUG
#define MAKE_SHADER_INCLUDE_PATH1(x) MAKE_SHADER_INCLUDE_PATH2(shaders/generated/Debug/, x, .h)
#else
#define MAKE_SHADER_INCLUDE_PATH1(x) MAKE_SHADER_INCLUDE_PATH2(shaders/generated/Release/, x, .h)
#endif
#define MAKE_SHADER_INCLUDE_PATH(x) MAKE_SHADER_INCLUDE_PATH1(x)

#ifndef NO_DYNAMIC_RESOURCES
#include MAKE_SHADER_INCLUDE_PATH(DynResPS)
#ifdef NO_VERTEX_PULLING
#include MAKE_SHADER_INCLUDE_PATH(VertexPushVS6)
#else
#include MAKE_SHADER_INCLUDE_PATH(DynResVS)
#endif
#endif

#ifndef NO_DYNAMIC_INDEXING
#include MAKE_SHADER_INCLUDE_PATH(DynIdxPS)
#ifdef NO_VERTEX_PULLING
#include MAKE_SHADER_INCLUDE_PATH(VertexPushVS)
#else
#include MAKE_SHADER_INCLUDE_PATH(DynIdxVS)
#endif
#else
#include MAKE_SHADER_INCLUDE_PATH(BindfulPS)
#ifdef NO_VERTEX_PULLING
#include MAKE_SHADER_INCLUDE_PATH(VertexPushVS)
#else
#include MAKE_SHADER_INCLUDE_PATH(BindfulVS)
#endif
#endif

extern "C" {
__declspec(dllexport) extern UINT const D3D12SDKVersion{D3D12_SDK_VERSION};
__declspec(dllexport) extern char const* D3D12SDKPath{R"(.\D3D12\)"};
}

namespace {
auto ThrowIfFailed(HRESULT const hr) -> void {
  if (FAILED(hr)) {
    throw std::exception{};
  }
}

auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam,
                         LPARAM const lparam) -> LRESULT {
  if (msg == WM_CLOSE) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}
}

auto WINAPI wWinMain(_In_ HINSTANCE const hInstance,
                     [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance,
                     [[maybe_unused]] _In_ PWSTR const pCmdLine,
                     _In_ int const nShowCmd) -> int {
#ifdef NO_VERTEX_PULLING
  OutputDebugStringW(L"Using the input assembler.\n");
#else
  OutputDebugStringW(L"Using vertex pulling.\n");
#endif

  using Microsoft::WRL::ComPtr;

#ifndef NDEBUG
  ComPtr<ID3D12Debug5> debug;
  ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
  debug->EnableDebugLayer();
#endif

  UINT factory_create_flags{0};
#ifndef NDEBUG
  factory_create_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  ComPtr<IDXGIFactory7> factory;
  ThrowIfFailed(
    CreateDXGIFactory2(factory_create_flags, IID_PPV_ARGS(&factory)));

  ComPtr<IDXGIAdapter4> high_performance_adapter;
  ThrowIfFailed(factory->EnumAdapterByGpuPreference(
    0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
    IID_PPV_ARGS(&high_performance_adapter)));

  ComPtr<IDXGIOutput> output;
  ThrowIfFailed(high_performance_adapter->EnumOutputs(0, &output));

  DXGI_OUTPUT_DESC output_desc;
  ThrowIfFailed(output->GetDesc(&output_desc));

  auto const output_width{
    output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left
  };
  auto const output_height{
    output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top
  };

  auto tearing_supported{FALSE};
  ThrowIfFailed(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                             &tearing_supported,
                                             sizeof tearing_supported));

  auto windowed_hardware_composition_supported{FALSE};

  if (ComPtr<IDXGIOutput6> output6; SUCCEEDED(output.As(&output6))) {
    if (UINT hardware_composition_support{0}; SUCCEEDED(
      output6->CheckHardwareCompositionSupport(&hardware_composition_support
      ))) {
      windowed_hardware_composition_supported =
        hardware_composition_support &
        DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED
          ? TRUE
          : FALSE;
    }
  }

  ComPtr<ID3D12Device10> device;
  ThrowIfFailed(D3D12CreateDevice(high_performance_adapter.Get(),
                                  D3D_FEATURE_LEVEL_11_0,
                                  IID_PPV_ARGS(&device)));

#ifndef NDEBUG
  ComPtr<ID3D12InfoQueue> info_queue;
  ThrowIfFailed(device.As(&info_queue));
  ThrowIfFailed(
    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
  ThrowIfFailed(
    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
#endif

#ifndef NO_DYNAMIC_RESOURCES
  D3D12_FEATURE_DATA_D3D12_OPTIONS options;
  ThrowIfFailed(
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                sizeof options));

  D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_6};
  ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
                                            &shader_model,
                                            sizeof shader_model));

  auto const dynamic_resources_supported{
    options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3 && shader_model
    .HighestShaderModel >= D3D_SHADER_MODEL_6_6
  };
#endif

#ifndef NO_DYNAMIC_RESOURCES
  if (dynamic_resources_supported) {
    OutputDebugStringW(L"Using dynamic resources.\n");
  } else {
#endif
#ifndef NO_DYNAMIC_INDEXING
    OutputDebugStringW(L"Using dynamic indexing.\n");
#else
  OutputDebugStringW(L"Using bindful resources.\n");
#endif
#ifndef NO_DYNAMIC_RESOURCES
  }
#endif

#ifndef NO_ENHANCED_BARRIERS
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 option12;
  ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
                                            &option12, sizeof option12));

  auto const enhanced_barriers_supported{option12.EnhancedBarriersSupported};
#endif

#ifndef NO_ENHANCED_BARRIERS
  if (enhanced_barriers_supported) {
    OutputDebugStringW(L"Using enhanced barriers.\n");
  } else {
#endif
    OutputDebugStringW(L"Using legacy resource barriers.\n");
#ifndef NO_ENHANCED_BARRIERS
  }
#endif

  WNDCLASSW const window_class{
    .style = 0, .lpfnWndProc = &WindowProc, .hInstance = hInstance,
    .hIcon = nullptr, .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"D3D12 Test"
  };

  if (!RegisterClassW(&window_class)) {
    throw std::exception{};
  }

  using WindowDeleter = decltype([](HWND const hwnd) {
    if (hwnd) {
      DestroyWindow(hwnd);
    }
  });

  std::unique_ptr<std::remove_pointer_t<HWND>, WindowDeleter> const hwnd{
    CreateWindowExW(0, window_class.lpszClassName, L"D3D12 Test", WS_POPUP,
                    output_desc.DesktopCoordinates.left,
                    output_desc.DesktopCoordinates.top, output_width,
                    output_height, nullptr, nullptr, window_class.hInstance,
                    nullptr)
  };
  ShowWindow(hwnd.get(), nShowCmd);

  D3D12_COMMAND_QUEUE_DESC constexpr direct_command_queue_desc{
    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
    .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, .NodeMask = 0
  };

  ComPtr<ID3D12CommandQueue> direct_command_queue;
  ThrowIfFailed(device->CreateCommandQueue(&direct_command_queue_desc,
                                           IID_PPV_ARGS(
                                             &direct_command_queue)));

  auto const swap_chain_width{output_width};
  auto const swap_chain_height{output_height};
  constexpr auto swap_chain_format{DXGI_FORMAT_R8G8B8A8_UNORM};
  constexpr auto swap_chain_buffer_count{2};

  UINT swap_chain_flags{0};
  UINT present_flags{0};

#ifndef USE_FULLSCREEN_SWAP_CHAIN
  if (tearing_supported) {
    swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    present_flags |= DXGI_PRESENT_ALLOW_TEARING;
  }
#endif

  DXGI_SWAP_CHAIN_DESC1 const swap_chain_desc{
    .Width = static_cast<UINT>(swap_chain_width),
    .Height = static_cast<UINT>(swap_chain_height), .Format = swap_chain_format,
    .Stereo = FALSE, .SampleDesc = {.Count = 1, .Quality = 0},
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = swap_chain_buffer_count, .Scaling = DXGI_SCALING_STRETCH,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED, .Flags = swap_chain_flags
  };

  ComPtr<IDXGISwapChain1> swap_chain1;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(direct_command_queue.Get(),
                                                hwnd.get(), &swap_chain_desc,
                                                nullptr, nullptr,
                                                &swap_chain1));

  ComPtr<IDXGISwapChain4> swap_chain;
  ThrowIfFailed(swap_chain1.As(&swap_chain));

#ifdef USE_FULLSCREEN_SWAP_CHAIN
  ThrowIfFailed(swap_chain->SetFullscreenState(TRUE, output.Get()));
  BOOL is_fullscreen{FALSE};
#endif

  D3D12_DESCRIPTOR_HEAP_DESC constexpr rtv_heap_desc{
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE, .NodeMask = 0
  };

  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  ThrowIfFailed(
    device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)));

  auto const rtv_heap_increment{
    device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
  };
  auto const rtv_heap_cpu_start{rtv_heap->GetCPUDescriptorHandleForHeapStart()};

  std::array<ComPtr<ID3D12Resource2>, swap_chain_buffer_count>
    swap_chain_buffers;
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, swap_chain_buffer_count>
    swap_chain_rtvs;

#ifdef USE_FULLSCREEN_SWAP_CHAIN
  auto recreate_swap_chain_rtvs{
    [&] {
#endif
      for (UINT i = 0; i < swap_chain_buffer_count; i++) {
        ThrowIfFailed(
          swap_chain->GetBuffer(i, IID_PPV_ARGS(&swap_chain_buffers[i])));

        swap_chain_rtvs[i].ptr = rtv_heap_cpu_start.ptr + static_cast<SIZE_T>(i)
          * rtv_heap_increment;

        D3D12_RENDER_TARGET_VIEW_DESC constexpr rtv_desc{
          .Format = swap_chain_format,
          .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
          .Texture2D = {.MipSlice = 0, .PlaneSlice = 0}
        };

        device->CreateRenderTargetView(swap_chain_buffers[i].Get(), &rtv_desc,
                                       swap_chain_rtvs[i]);
      }
#ifdef USE_FULLSCREEN_SWAP_CHAIN
    }
  };
#endif

  constexpr auto max_frames_in_flight{2};
  UINT64 this_frame_fence_value{max_frames_in_flight - 1};

  ComPtr<ID3D12Fence1> fence;
  ThrowIfFailed(
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

  auto const signal_and_wait_fence{
    [&](UINT64 const signal_value, UINT64 const wait_value) {
      ThrowIfFailed(direct_command_queue->Signal(fence.Get(), signal_value));

      if (fence->GetCompletedValue() < wait_value) {
        ThrowIfFailed(fence->SetEventOnCompletion(wait_value, nullptr));
      }
    }
  };

  auto const wait_for_gpu_idle{
    [&] {
      auto const signal_value{++this_frame_fence_value};
      auto const wait_value{signal_value};
      signal_and_wait_fence(signal_value, wait_value);
    }
  };

  auto const wait_for_in_flight_frames{
    [&] {
      auto const signal_value{++this_frame_fence_value};
      auto const wait_value{signal_value - max_frames_in_flight + 1};
      signal_and_wait_fence(signal_value, wait_value);
    }
  };

  std::array<ComPtr<ID3D12CommandAllocator>, max_frames_in_flight>
    direct_command_allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList7>, max_frames_in_flight>
    direct_command_lists;

  for (auto i{0}; i < max_frames_in_flight; i++) {
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(
                                                   &direct_command_allocators[i
                                                   ])));

    ThrowIfFailed(device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             D3D12_COMMAND_LIST_FLAG_NONE,
                                             IID_PPV_ARGS(
                                               &direct_command_lists[i])));
  }

  std::vector<D3D12_DESCRIPTOR_RANGE1> descriptor_ranges;
  std::vector<D3D12_ROOT_PARAMETER1> root_parameters;
  auto root_signature_flags{D3D12_ROOT_SIGNATURE_FLAG_NONE};

#ifdef NO_VERTEX_PULLING
  root_signature_flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#endif

#ifndef NO_DYNAMIC_RESOURCES
  if (dynamic_resources_supported) {
    root_parameters.emplace_back(D3D12_ROOT_PARAMETER1{
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
      .Constants = {
        .ShaderRegister = 0, .RegisterSpace = 0, .Num32BitValues = 2
      },
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    });

    root_signature_flags |=
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
  } else {
#endif
#ifndef NO_DYNAMIC_INDEXING
    descriptor_ranges.emplace_back(D3D12_DESCRIPTOR_RANGE1{
      .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1,
      .BaseShaderRegister = 0, .RegisterSpace = 0,
      .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
      .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
    });

    descriptor_ranges.emplace_back(D3D12_DESCRIPTOR_RANGE1{
      .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1,
      .BaseShaderRegister = 0, .RegisterSpace = 1,
      .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
      .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
    });

    // The tables containing the vertex buffer and the texture must be separate, because the vertex buffer is vertex-shader-only, while the texture is pixel-shader-only, and the tables containing them have to respect that.

    root_parameters.emplace_back(D3D12_ROOT_PARAMETER1{
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
      .Constants = {
        .ShaderRegister = 0, .RegisterSpace = 0, .Num32BitValues = 2
      },
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
    });

    root_parameters.emplace_back(D3D12_ROOT_PARAMETER1{
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
      .DescriptorTable = {
        .NumDescriptorRanges = 1, .pDescriptorRanges = &descriptor_ranges[0]
      },
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
    });

    root_parameters.emplace_back(D3D12_ROOT_PARAMETER1{
      .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
      .DescriptorTable = {
        .NumDescriptorRanges = 1, .pDescriptorRanges = &descriptor_ranges[1]
      },
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
    });
#else
  descriptor_ranges.emplace_back(D3D12_DESCRIPTOR_RANGE1{
    .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    .NumDescriptors = 1,
    .BaseShaderRegister = 0,
    .RegisterSpace = 0,
    .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
    .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  });

  descriptor_ranges.emplace_back(D3D12_DESCRIPTOR_RANGE1{
    .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    .NumDescriptors = 1,
    .BaseShaderRegister = 1,
    .RegisterSpace = 0,
    .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
    .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  });

  // The tables containing the vertex buffer and the texture must be separate, because the vertex buffer is vertex-shader-only, while the texture is pixel-shader-only, and the tables containing them have to respect that.

  root_parameters.emplace_back(D3D12_ROOT_PARAMETER1{
    .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
    .DescriptorTable = {
      .NumDescriptorRanges = 1,
      .pDescriptorRanges = &descriptor_ranges[0]
    },
    .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
  });

  root_parameters.emplace_back(D3D12_ROOT_PARAMETER1{
    .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
    .DescriptorTable = {
      .NumDescriptorRanges = 1,
      .pDescriptorRanges = &descriptor_ranges[1]
    },
    .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
  });
#endif
#ifndef NO_DYNAMIC_RESOURCES
  }
#endif

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC const root_signature_desc{
    .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
    .Desc_1_1 = {
      .NumParameters = static_cast<UINT>(root_parameters.size()),
      .pParameters = root_parameters.data(), .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr, .Flags = root_signature_flags
    }
  };

  ComPtr<ID3DBlob> root_signature_blob;
  ThrowIfFailed(D3D12SerializeVersionedRootSignature(&root_signature_desc,
    &root_signature_blob, nullptr));

  ComPtr<ID3D12RootSignature> root_signature;
  ThrowIfFailed(device->CreateRootSignature(
    0, root_signature_blob->GetBufferPointer(),
    root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)));

  D3D12_SHADER_BYTECODE vs_bytecode;
  D3D12_SHADER_BYTECODE ps_bytecode;
  D3D12_INPUT_LAYOUT_DESC input_layout_desc{nullptr, 0};

#ifndef NO_DYNAMIC_RESOURCES
  if (dynamic_resources_supported) {
    ps_bytecode.BytecodeLength = ARRAYSIZE(kDynResPSBin);
    ps_bytecode.pShaderBytecode = kDynResPSBin;
#ifdef NO_VERTEX_PULLING
      vs_bytecode.BytecodeLength = ARRAYSIZE(kVertexPushVS6Bin);
      vs_bytecode.pShaderBytecode = kVertexPushVS6Bin;
#else
    vs_bytecode.BytecodeLength = ARRAYSIZE(kDynResVSBin);
    vs_bytecode.pShaderBytecode = kDynResVSBin;
#endif
  } else {
#endif
#ifndef NO_DYNAMIC_INDEXING
    ps_bytecode.BytecodeLength = ARRAYSIZE(kDynIdxPSBin);
    ps_bytecode.pShaderBytecode = kDynIdxPSBin;
#ifdef NO_VERTEX_PULLING
      vs_bytecode.BytecodeLength = ARRAYSIZE(kVertexPushVSBin);
      vs_bytecode.pShaderBytecode = kVertexPushVSBin;
#else
    vs_bytecode.BytecodeLength = ARRAYSIZE(kDynIdxVSBin);
    vs_bytecode.pShaderBytecode = kDynIdxVSBin;
#endif
#else
  ps_bytecode.BytecodeLength = ARRAYSIZE(kBindfulPSBin);
  ps_bytecode.pShaderBytecode = kBindfulPSBin;
#ifdef NO_VERTEX_PULLING
  vs_bytecode.BytecodeLength = ARRAYSIZE(kVertexPushVSBin);
  vs_bytecode.pShaderBytecode = kVertexPushVSBin;
#else
    vs_bytecode.BytecodeLength = ARRAYSIZE(kBindfulVSBin);
    vs_bytecode.pShaderBytecode = kBindfulVSBin;
#endif
#endif
#ifndef NO_DYNAMIC_RESOURCES
  }
#endif

#ifdef NO_VERTEX_PULLING
  D3D12_INPUT_ELEMENT_DESC constexpr input_element_desc{
    .SemanticName = "POSITION",
    .SemanticIndex = 0,
    .Format = DXGI_FORMAT_R32G32_FLOAT,
    .InputSlot = 0,
    .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
    .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
    .InstanceDataStepRate = 0
  };

  input_layout_desc.NumElements = 1;
  input_layout_desc.pInputElementDescs = &input_element_desc;
#endif

  D3D12_GRAPHICS_PIPELINE_STATE_DESC const pso_desc{
    .pRootSignature = root_signature.Get(), .VS = vs_bytecode,
    .PS = ps_bytecode, .DS = {nullptr, 0}, .HS = {nullptr, 0},
    .GS = {nullptr, 0}, .StreamOutput = {},
    .BlendState = {
      .AlphaToCoverageEnable = FALSE, .IndependentBlendEnable = FALSE,
      .RenderTarget = {
        D3D12_RENDER_TARGET_BLEND_DESC{
          .BlendEnable = FALSE, .LogicOpEnable = FALSE,
          .SrcBlend = D3D12_BLEND_ONE, .DestBlend = D3D12_BLEND_ZERO,
          .BlendOp = D3D12_BLEND_OP_ADD, .SrcBlendAlpha = D3D12_BLEND_ONE,
          .DestBlendAlpha = D3D12_BLEND_ONE, .BlendOpAlpha = D3D12_BLEND_OP_ADD,
          .LogicOp = D3D12_LOGIC_OP_NOOP,
          .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
        }
      }
    },
    .SampleMask = std::numeric_limits<UINT>::max(),
    .RasterizerState = {
      .FillMode = D3D12_FILL_MODE_SOLID, .CullMode = D3D12_CULL_MODE_BACK,
      .FrontCounterClockwise = FALSE, .DepthBias = 0, .DepthBiasClamp = 0,
      .SlopeScaledDepthBias = 0, .DepthClipEnable = TRUE,
      .MultisampleEnable = FALSE, .AntialiasedLineEnable = FALSE,
      .ForcedSampleCount = 0,
      .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    },
    .DepthStencilState = {
      .DepthEnable = FALSE, .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
      .DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS, .StencilEnable = FALSE,
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
    .InputLayout = input_layout_desc,
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1, .RTVFormats = {swap_chain_format},
    .DSVFormat = DXGI_FORMAT_UNKNOWN, .SampleDesc = {.Count = 1, .Quality = 0},
    .NodeMask = 0, .CachedPSO = {nullptr, 0},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
  };

  ComPtr<ID3D12PipelineState> pso;
  ThrowIfFailed(
    device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)));

  D3D12_HEAP_PROPERTIES constexpr upload_heap_properties{
    .Type = D3D12_HEAP_TYPE_UPLOAD,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN, .CreationNodeMask = 0,
    .VisibleNodeMask = 0
  };

  auto constexpr upload_buffer_size{64 * 1024};

  D3D12_RESOURCE_DESC1 constexpr upload_buffer_desc{
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
    .Width = upload_buffer_size, .Height = 1, .DepthOrArraySize = 1,
    .MipLevels = 1, .Format = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR, .Flags = D3D12_RESOURCE_FLAG_NONE,
    .SamplerFeedbackMipRegion = {.Width = 0, .Height = 0, .Depth = 0}
  };

  ComPtr<ID3D12Resource2> upload_buffer;
#ifndef NO_ENHANCED_BARRIERS
  if (enhanced_barriers_supported) {
    ThrowIfFailed(device->CreateCommittedResource3(&upload_heap_properties,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &upload_buffer_desc,
                                                   D3D12_BARRIER_LAYOUT_UNDEFINED,
                                                   nullptr, nullptr, 0, nullptr,
                                                   IID_PPV_ARGS(
                                                     &upload_buffer)));
  } else {
#endif
    ThrowIfFailed(device->CreateCommittedResource2(&upload_heap_properties,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &upload_buffer_desc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, nullptr,
                                                   IID_PPV_ARGS(
                                                     &upload_buffer)));
#ifndef NO_ENHANCED_BARRIERS
  }
#endif

  void* mapped_upload_buffer;
  ThrowIfFailed(upload_buffer->Map(0, nullptr, &mapped_upload_buffer));

  std::array constexpr vertex_data{
    std::array{0.0f, 0.5f}, std::array{0.5f, -0.5f}, std::array{-0.5f, -0.5f}
  };

  D3D12_RESOURCE_DESC1 constexpr vertex_buffer_desc{
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
    .Width = sizeof(vertex_data), .Height = 1, .DepthOrArraySize = 1,
    .MipLevels = 1, .Format = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR, .Flags = D3D12_RESOURCE_FLAG_NONE,
    .SamplerFeedbackMipRegion = {.Width = 0, .Height = 0, .Depth = 0}
  };

  D3D12_HEAP_PROPERTIES constexpr vertex_buffer_heap_properties{
    .Type = D3D12_HEAP_TYPE_DEFAULT,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN, .CreationNodeMask = 0,
    .VisibleNodeMask = 0
  };

  ComPtr<ID3D12Resource2> vertex_buffer;
#ifndef NO_ENHANCED_BARRIERS
  if (enhanced_barriers_supported) {
    ThrowIfFailed(device->CreateCommittedResource3(
      &vertex_buffer_heap_properties, D3D12_HEAP_FLAG_NONE, &vertex_buffer_desc,
      D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
      IID_PPV_ARGS(&vertex_buffer)));
  } else {
#endif
    ThrowIfFailed(device->CreateCommittedResource2(
      &vertex_buffer_heap_properties, D3D12_HEAP_FLAG_NONE, &vertex_buffer_desc,
      D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
      IID_PPV_ARGS(&vertex_buffer)));
#ifndef NO_ENHANCED_BARRIERS
  }
#endif

  std::memcpy(mapped_upload_buffer, vertex_data.data(), sizeof(vertex_data));

  ThrowIfFailed(
    direct_command_lists[0]->Reset(direct_command_allocators[0].Get(),
                                   pso.Get()));

  direct_command_lists[0]->CopyBufferRegion(vertex_buffer.Get(), 0,
                                            upload_buffer.Get(), 0,
                                            sizeof(vertex_data));

#ifndef NO_ENHANCED_BARRIERS
  if (enhanced_barriers_supported) {
#ifndef NO_VERTEX_PULLING
    auto constexpr access_after{D3D12_BARRIER_ACCESS_SHADER_RESOURCE};
#else
    auto constexpr access_after{D3D12_BARRIER_ACCESS_VERTEX_BUFFER};
#endif

    D3D12_BUFFER_BARRIER const vertex_buffer_post_upload_barrier{
      .SyncBefore = D3D12_BARRIER_SYNC_COPY,
      .SyncAfter = D3D12_BARRIER_SYNC_VERTEX_SHADING,
      .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
      .AccessAfter = access_after, .pResource = vertex_buffer.Get(),
      .Offset = 0, .Size = UINT64_MAX
    };

    D3D12_BARRIER_GROUP const barrier_group{
      .Type = D3D12_BARRIER_TYPE_BUFFER, .NumBarriers = 1,
      .pBufferBarriers = &vertex_buffer_post_upload_barrier
    };

    direct_command_lists[0]->Barrier(1, &barrier_group);
  } else {
#endif
#ifndef NO_VERTEX_PULLING
    auto constexpr state_after{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
#else
    auto constexpr state_after{D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
#endif

    D3D12_RESOURCE_BARRIER const vertex_buffer_post_upload_barrier{
      .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
      .Transition = {
        .pResource = vertex_buffer.Get(), .Subresource = 0,
        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST, .StateAfter = state_after
      }
    };

    direct_command_lists[0]->ResourceBarrier(
      1, &vertex_buffer_post_upload_barrier);
#ifndef NO_ENHANCED_BARRIERS
  }
#endif

  ThrowIfFailed(direct_command_lists[0]->Close());

  direct_command_queue->ExecuteCommandLists(
    1, std::array<ID3D12CommandList*, 1>{direct_command_lists[0].Get()}.data());
  wait_for_gpu_idle();

  constexpr std::array<std::uint8_t, 4> red_unorm{255, 0, 0, 255};
  constexpr std::array<std::uint8_t, 4> green_unorm{0, 255, 0, 255};

  D3D12_HEAP_PROPERTIES constexpr texture_heap_properties{
    .Type = D3D12_HEAP_TYPE_DEFAULT,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN, .CreationNodeMask = 0,
    .VisibleNodeMask = 0
  };

  D3D12_RESOURCE_DESC1 constexpr texture_desc{
    .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Alignment = 0, .Width = 1,
    .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN, .Flags = D3D12_RESOURCE_FLAG_NONE,
    .SamplerFeedbackMipRegion = {.Width = 0, .Height = 0, .Depth = 0}
  };

  ComPtr<ID3D12Resource2> texture;
#ifndef NO_ENHANCED_BARRIERS
  if (enhanced_barriers_supported) {
    ThrowIfFailed(device->CreateCommittedResource3(&texture_heap_properties,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &texture_desc,
                                                   D3D12_BARRIER_LAYOUT_COPY_DEST,
                                                   nullptr, nullptr, 0, nullptr,
                                                   IID_PPV_ARGS(&texture)));
  } else {
#endif
    ThrowIfFailed(device->CreateCommittedResource2(&texture_heap_properties,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &texture_desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, nullptr,
                                                   IID_PPV_ARGS(&texture)));
#ifndef NO_ENHANCED_BARRIERS
  }
#endif

  std::memcpy(mapped_upload_buffer,
              (windowed_hardware_composition_supported
                 ? green_unorm
                 : red_unorm).data(), 4);

  ThrowIfFailed(
    direct_command_lists[0]->Reset(direct_command_allocators[0].Get(),
                                   pso.Get()));

  D3D12_TEXTURE_COPY_LOCATION const src_texture_copy_location{
    .pResource = upload_buffer.Get(),
    .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
    .PlacedFootprint = {
      .Offset = 0,
      .Footprint = {
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .Width = 1, .Height = 1,
        .Depth = 1, .RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
      }
    }
  };

  D3D12_TEXTURE_COPY_LOCATION const dst_texture_copy_location{
    .pResource = texture.Get(),
    .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0
  };

  direct_command_lists[0]->CopyTextureRegion(&dst_texture_copy_location, 0, 0,
                                             0, &src_texture_copy_location,
                                             nullptr);

#ifndef NO_ENHANCED_BARRIERS
  if (enhanced_barriers_supported) {
    D3D12_TEXTURE_BARRIER const texture_post_upload_barrier{
      .SyncBefore = D3D12_BARRIER_SYNC_COPY,
      .SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING,
      .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
      .AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
      .LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST,
      .LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
      .pResource = texture.Get(),
      .Subresources = {
        .IndexOrFirstMipLevel = 0, .NumMipLevels = 1, .FirstArraySlice = 0,
        .NumArraySlices = 1, .FirstPlane = 0, .NumPlanes = 1
      },
      .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE
    };

    D3D12_BARRIER_GROUP const barrier_group{
      .Type = D3D12_BARRIER_TYPE_TEXTURE, .NumBarriers = 1,
      .pTextureBarriers = &texture_post_upload_barrier
    };

    direct_command_lists[0]->Barrier(1, &barrier_group);
  } else {
#endif
    D3D12_RESOURCE_BARRIER const texture_post_upload_barrier{
      .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
      .Transition = {
        .pResource = texture.Get(), .Subresource = 0,
        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
      }
    };

    direct_command_lists[0]->ResourceBarrier(1, &texture_post_upload_barrier);
#ifndef NO_ENHANCED_BARRIERS
  }
#endif

  ThrowIfFailed(direct_command_lists[0]->Close());

  direct_command_queue->ExecuteCommandLists(
    1, std::array<ID3D12CommandList*, 1>{direct_command_lists[0].Get()}.data());
  wait_for_gpu_idle();

  D3D12_DESCRIPTOR_HEAP_DESC constexpr resource_heap_desc{
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, .NodeMask = 0
  };

  ComPtr<ID3D12DescriptorHeap> resource_heap;
  ThrowIfFailed(device->CreateDescriptorHeap(&resource_heap_desc,
                                             IID_PPV_ARGS(&resource_heap)));

  auto const resource_heap_increment{
    device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
  };
  auto const resource_heap_cpu_start{
    resource_heap->GetCPUDescriptorHandleForHeapStart()
  };

  auto constexpr vertex_buffer_srv_heap_idx{0};
  auto constexpr tex_srv_heap_idx{vertex_buffer_srv_heap_idx + 1};

#ifdef NO_VERTEX_PULLING
  D3D12_VERTEX_BUFFER_VIEW const vertex_buffer_view{
    .BufferLocation = vertex_buffer->GetGPUVirtualAddress(),
    .SizeInBytes = static_cast<UINT>(vertex_buffer->GetDesc().Width),
    .StrideInBytes = sizeof vertex_data[0]
  };
#else
  D3D12_SHADER_RESOURCE_VIEW_DESC constexpr vertex_buffer_srv_desc{
    .Format = DXGI_FORMAT_R32G32_FLOAT,
    .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Buffer = {
      .FirstElement = 0,
      .NumElements = static_cast<UINT>(std::size(vertex_data)),
      .StructureByteStride = 0, .Flags = D3D12_BUFFER_SRV_FLAG_NONE
    }
  };

  device->CreateShaderResourceView(vertex_buffer.Get(), &vertex_buffer_srv_desc,
                                   D3D12_CPU_DESCRIPTOR_HANDLE{
                                     resource_heap_cpu_start.ptr + static_cast<
                                       SIZE_T>(vertex_buffer_srv_heap_idx) *
                                     resource_heap_increment
                                   });
#endif

  D3D12_SHADER_RESOURCE_VIEW_DESC constexpr texture_srv_desc{
    .Format = texture_desc.Format,
    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Texture2D = {
      .MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0,
      .ResourceMinLODClamp = 0.0f
    }
  };

  device->CreateShaderResourceView(texture.Get(), &texture_srv_desc,
                                   D3D12_CPU_DESCRIPTOR_HANDLE{
                                     resource_heap_cpu_start.ptr + static_cast<
                                       SIZE_T>(tex_srv_heap_idx) *
                                     resource_heap_increment
                                   });

  UINT constexpr vertex_buffer_shader_idx{0};
  UINT tex_shader_idx;

#ifndef NO_DYNAMIC_RESOURCES
  if (dynamic_resources_supported) {
    tex_shader_idx = vertex_buffer_shader_idx + 1;
  } else {
#endif
    tex_shader_idx = 0;
#ifndef NO_DYNAMIC_RESOURCES
  }
#endif

  ComPtr<ID3D12CommandAllocator> bundle_allocator;
  ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE,
                                               IID_PPV_ARGS(
                                                 &bundle_allocator)));

  ComPtr<ID3D12GraphicsCommandList6> bundle;
  ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
                                          bundle_allocator.Get(), pso.Get(),
                                          IID_PPV_ARGS(&bundle)));

  bundle->SetDescriptorHeaps(1, resource_heap.GetAddressOf());
  bundle->SetGraphicsRootSignature(root_signature.Get());

#if !defined(NO_DYNAMIC_RESOURCES) || !defined(NO_DYNAMIC_INDEXING)
  std::array const root_constants{vertex_buffer_shader_idx, tex_shader_idx};
#endif

#ifndef NO_DYNAMIC_RESOURCES
  if (dynamic_resources_supported) {
    bundle->SetGraphicsRoot32BitConstants(
      0, static_cast<UINT>(root_constants.size()), root_constants.data(), 0);
  } else {
#endif
#ifndef NO_DYNAMIC_INDEXING
    bundle->SetGraphicsRoot32BitConstants(
      0, static_cast<UINT>(root_constants.size()), root_constants.data(), 0);
    bundle->SetGraphicsRootDescriptorTable(
      1, resource_heap->GetGPUDescriptorHandleForHeapStart());
    bundle->SetGraphicsRootDescriptorTable(2, {
                                             resource_heap->
                                             GetGPUDescriptorHandleForHeapStart()
                                             .ptr + static_cast<SIZE_T>(
                                               tex_srv_heap_idx) *
                                             resource_heap_increment
                                           });
#else
  bundle->SetGraphicsRootDescriptorTable(0, resource_heap->GetGPUDescriptorHandleForHeapStart());
  bundle->SetGraphicsRootDescriptorTable(1, {resource_heap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(tex_srv_heap_idx) * resource_heap_increment});
#endif
#ifndef NO_DYNAMIC_RESOURCES
  }
#endif

#ifdef NO_VERTEX_PULLING
  bundle->IASetVertexBuffers(0, 1, &vertex_buffer_view);
#endif

  bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  bundle->DrawInstanced(3, 1, 0, 0);

  ThrowIfFailed(bundle->Close());

  auto frame_idx{0};

  while (true) {
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
#ifdef USE_FULLSCREEN_SWAP_CHAIN
        ThrowIfFailed(swap_chain->SetFullscreenState(FALSE, nullptr));
#endif
        wait_for_gpu_idle();
        return static_cast<int>(msg.wParam);
      }

      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

#ifdef USE_FULLSCREEN_SWAP_CHAIN
    BOOL fullscreen_state;
    ThrowIfFailed(swap_chain->GetFullscreenState(&fullscreen_state, nullptr));

    if (fullscreen_state != is_fullscreen) {
      swap_chain_buffers.fill(nullptr);
      ThrowIfFailed(
        swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN,
                                  swap_chain_flags));
      recreate_swap_chain_rtvs();
      is_fullscreen = fullscreen_state;
    }
#endif

    auto back_buffer_idx{swap_chain->GetCurrentBackBufferIndex()};

    ThrowIfFailed(direct_command_allocators[frame_idx]->Reset());
    ThrowIfFailed(direct_command_lists[frame_idx]->Reset(
      direct_command_allocators[frame_idx].Get(), pso.Get()));

    direct_command_lists[frame_idx]->SetDescriptorHeaps(
      1, resource_heap.GetAddressOf());

    D3D12_VIEWPORT const viewport{
      .TopLeftX = 0, .TopLeftY = 0,
      .Width = static_cast<FLOAT>(swap_chain_width),
      .Height = static_cast<FLOAT>(swap_chain_height), .MinDepth = 0,
      .MaxDepth = 1
    };

    direct_command_lists[frame_idx]->RSSetViewports(1, &viewport);

    D3D12_RECT const scissor_rect{
      .left = static_cast<LONG>(viewport.TopLeftX),
      .top = static_cast<LONG>(viewport.TopLeftY),
      .right = static_cast<LONG>(viewport.TopLeftX + viewport.Width),
      .bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height)
    };

    direct_command_lists[frame_idx]->RSSetScissorRects(1, &scissor_rect);

#ifndef NO_ENHANCED_BARRIERS
    if (enhanced_barriers_supported) {
      D3D12_TEXTURE_BARRIER const swap_chain_rtv_barrier{
        .SyncBefore = D3D12_BARRIER_SYNC_NONE,
        .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
        .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
        .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT,
        .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        .pResource = swap_chain_buffers[back_buffer_idx].Get(),
        .Subresources = {
          .IndexOrFirstMipLevel = 0, .NumMipLevels = 1, .FirstArraySlice = 0,
          .NumArraySlices = 1, .FirstPlane = 0, .NumPlanes = 1
        },
        .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE
      };
      D3D12_BARRIER_GROUP const pre_render_barrier_group{
        .Type = D3D12_BARRIER_TYPE_TEXTURE, .NumBarriers = 1,
        .pTextureBarriers = &swap_chain_rtv_barrier
      };
      direct_command_lists[frame_idx]->Barrier(1, &pre_render_barrier_group);
    } else {
#endif
      D3D12_RESOURCE_BARRIER const swap_chain_rtv_barrier{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
          .pResource = swap_chain_buffers[back_buffer_idx].Get(),
          .Subresource = 0, .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
          .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
        }
      };
      direct_command_lists[frame_idx]->ResourceBarrier(
        1, &swap_chain_rtv_barrier);
#ifndef NO_ENHANCED_BARRIERS
    }
#endif

    direct_command_lists[frame_idx]->ClearRenderTargetView(
      swap_chain_rtvs[back_buffer_idx],
      std::array{0.1f, 0.1f, 0.1f, 1.0f}.data(), 0, nullptr);
    direct_command_lists[frame_idx]->OMSetRenderTargets(
      1, &swap_chain_rtvs[back_buffer_idx], FALSE, nullptr);

    direct_command_lists[frame_idx]->ExecuteBundle(bundle.Get());

#ifndef NO_ENHANCED_BARRIERS
    if (enhanced_barriers_supported) {
      D3D12_TEXTURE_BARRIER const swap_chain_present_barrier{
        .SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET,
        .SyncAfter = D3D12_BARRIER_SYNC_NONE,
        .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS,
        .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        .LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT,
        .pResource = swap_chain_buffers[back_buffer_idx].Get(),
        .Subresources = {
          .IndexOrFirstMipLevel = 0, .NumMipLevels = 1, .FirstArraySlice = 0,
          .NumArraySlices = 1, .FirstPlane = 0, .NumPlanes = 1
        },
        .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE
      };
      D3D12_BARRIER_GROUP const post_render_barrier_group{
        .Type = D3D12_BARRIER_TYPE_TEXTURE, .NumBarriers = 1,
        .pTextureBarriers = &swap_chain_present_barrier
      };
      direct_command_lists[frame_idx]->Barrier(1, &post_render_barrier_group);
    } else {
#endif
      D3D12_RESOURCE_BARRIER const swap_chain_present_barrier{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
          .pResource = swap_chain_buffers[back_buffer_idx].Get(),
          .Subresource = 0, .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
          .StateAfter = D3D12_RESOURCE_STATE_PRESENT
        }
      };
      direct_command_lists[frame_idx]->ResourceBarrier(
        1, &swap_chain_present_barrier);
#ifndef NO_ENHANCED_BARRIERS
    }
#endif

    ThrowIfFailed(direct_command_lists[frame_idx]->Close());

    direct_command_queue->ExecuteCommandLists(
      1, std::array<ID3D12CommandList*, 1>{
        direct_command_lists[frame_idx].Get()
      }.data());

    ThrowIfFailed(swap_chain->Present(0, present_flags));

    wait_for_in_flight_frames();
    back_buffer_idx = swap_chain->GetCurrentBackBufferIndex();
    frame_idx = (frame_idx + 1) % max_frames_in_flight;
  }
}
