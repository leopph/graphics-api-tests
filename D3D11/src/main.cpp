/* D3D11 test project
 * Define NO_WAITABLE_SWAP_CHAIN to prevent the use of waitable swap chains even on supported hardware.
 */

// Uncomment this if you want to opt out of using  waitable swap chains
// #define NO_WAITABLE_SWAP_CHAIN

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3dcompiler.h>

#include <array>
#include <cassert>
#include <memory>
#include <type_traits>
#include <vector>

#include "shaders/interop.h"

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

#include MAKE_SHADER_INCLUDE_PATH(vs)
#include MAKE_SHADER_INCLUDE_PATH(ps)
#include MAKE_SHADER_INCLUDE_PATH(cs)


namespace {
auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
  if (msg == WM_CLOSE) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}
}


auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance, [[maybe_unused]] _In_ PWSTR const pCmdLine, _In_ int const nCmdShow) -> int {
  WNDCLASSW const window_class{
    .style = 0,
    .lpfnWndProc = &WindowProc,
    .hInstance = hInstance,
    .hIcon = nullptr,
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"D3D11 Test"
  };

  [[maybe_unused]] auto const atom{RegisterClassW(&window_class)};
  assert(atom);

  using WindowDeleter = decltype([](HWND const hwnd) {
    if (hwnd) {
      DestroyWindow(hwnd);
    }
  });

  std::unique_ptr<std::remove_pointer_t<HWND>, WindowDeleter> const hwnd{CreateWindowExW(0, window_class.lpszClassName, L"D3D11 Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, window_class.hInstance, nullptr)};
  ShowWindow(hwnd.get(), nCmdShow);

  using Microsoft::WRL::ComPtr;

  UINT device_create_flags{0};
#ifndef NDEBUG
  device_create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  [[maybe_unused]] HRESULT hr;

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate_ctx;
  hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_create_flags, std::array{D3D_FEATURE_LEVEL_11_0}.data(), 1, D3D11_SDK_VERSION, &device, nullptr, &immediate_ctx);
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  ComPtr<ID3D11Debug> debug;
  hr = device.As<ID3D11Debug>(&debug);
  assert(SUCCEEDED(hr));
  ComPtr<ID3D11InfoQueue> info_queue;
  hr = debug.As<ID3D11InfoQueue>(&info_queue);
  assert(SUCCEEDED(hr));
  hr = info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
  assert(SUCCEEDED(hr));
  hr = info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
  assert(SUCCEEDED(hr));
#endif

  ComPtr<ID3D11DeviceContext> deferred_ctx;
  hr = device->CreateDeferredContext(0, &deferred_ctx);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIDevice4> dxgi_device;
  hr = device.As(&dxgi_device);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIAdapter> adapter;
  hr = dxgi_device->GetAdapter(&adapter);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIFactory6> factory;
  hr = adapter->GetParent(IID_PPV_ARGS(&factory));
  assert(SUCCEEDED(hr));

  auto tearing_supported{FALSE};
  hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_supported, sizeof tearing_supported);
  assert(SUCCEEDED(hr));

  UINT swap_chain_flags{0};
  UINT present_flags{0};

  if (tearing_supported) {
    swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    present_flags |= DXGI_PRESENT_ALLOW_TEARING;
  }

#ifndef NO_WAITABLE_SWAP_CHAIN
  swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
#endif

  auto constexpr swap_chain_width{2560};
  auto constexpr swap_chain_height{1440};
  auto constexpr swap_chain_format{DXGI_FORMAT_R8G8B8A8_UNORM};
  auto constexpr swap_chain_buffer_count{2};

  DXGI_SWAP_CHAIN_DESC1 const swap_chain_desc{
    .Width = swap_chain_width,
    .Height = swap_chain_height,
    .Format = swap_chain_format,
    .Stereo = FALSE,
    .SampleDesc{.Count = 1, .Quality = 0},
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS,
    .BufferCount = swap_chain_buffer_count,
    .Scaling = DXGI_SCALING_STRETCH,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
    .Flags = swap_chain_flags
  };

  ComPtr<IDXGISwapChain1> tmp_swap_chain;
  hr = factory->CreateSwapChainForHwnd(device.Get(), hwnd.get(), &swap_chain_desc, nullptr, nullptr, &tmp_swap_chain);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGISwapChain2> swap_chain;
  hr = tmp_swap_chain.As(&swap_chain);
  assert(SUCCEEDED(hr));

  auto fullscreen_hardware_composition_supported{FALSE};
  auto windowed_hardware_composition_supported{FALSE};

  if (ComPtr<IDXGIOutput> output; SUCCEEDED(swap_chain->GetContainingOutput(&output))) {
    if (ComPtr<IDXGIOutput6> output6; SUCCEEDED(output.As(&output6))) {
      if (UINT hardware_composition_support{0}; SUCCEEDED(output6->CheckHardwareCompositionSupport(&hardware_composition_support))) {
        fullscreen_hardware_composition_supported = hardware_composition_support & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_FULLSCREEN ? TRUE : FALSE;
        windowed_hardware_composition_supported = hardware_composition_support & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED ? TRUE : FALSE;
      }
    }
  }

  auto constexpr max_frames_in_flight{2};

#ifndef NO_WAITABLE_SWAP_CHAIN
  auto const frame_latency_waitable_object{swap_chain->GetFrameLatencyWaitableObject()};
  hr = swap_chain->SetMaximumFrameLatency(max_frames_in_flight);
#else
  hr = dxgi_device->SetMaximumFrameLatency(max_frames_in_flight);
#endif
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11Texture2D> back_buffer;
  hr = swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  assert(SUCCEEDED(hr));

  D3D11_RENDER_TARGET_VIEW_DESC constexpr rtv_desc{
    .Format = swap_chain_format,
    .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
    .Texture2D = {.MipSlice = 0}
  };

  ComPtr<ID3D11RenderTargetView> back_buffer_rtv;
  hr = device->CreateRenderTargetView(back_buffer.Get(), &rtv_desc, &back_buffer_rtv);
  assert(SUCCEEDED(hr));

  D3D11_UNORDERED_ACCESS_VIEW_DESC constexpr uav_desc{
    .Format = swap_chain_format,
    .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
    .Texture2D = {.MipSlice = 0}
  };

  ComPtr<ID3D11UnorderedAccessView> back_buffer_uav;
  hr = device->CreateUnorderedAccessView(back_buffer.Get(), &uav_desc, &back_buffer_uav);
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11VertexShader> vertex_shader;
  hr = device->CreateVertexShader(kvsBin, ARRAYSIZE(kvsBin), nullptr, &vertex_shader);
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11PixelShader> pixel_shader;
  hr = device->CreatePixelShader(kpsBin, ARRAYSIZE(kpsBin), nullptr, &pixel_shader);
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11ComputeShader> compute_shader;
  hr = device->CreateComputeShader(kcsBin, ARRAYSIZE(kcsBin), nullptr, &compute_shader);
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11ShaderReflection> vs_reflection;
  hr = D3DReflect(kvsBin, ARRAYSIZE(kvsBin), IID_PPV_ARGS(&vs_reflection));
  assert(SUCCEEDED(hr));

  D3D11_SHADER_DESC vs_desc;
  hr = vs_reflection->GetDesc(&vs_desc);
  assert(SUCCEEDED(hr));

  std::vector<D3D11_INPUT_ELEMENT_DESC> input_elements;
  input_elements.reserve(vs_desc.InputParameters);

  for (UINT i{0}; i < vs_desc.InputParameters; i++) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter_desc;
    hr = vs_reflection->GetInputParameterDesc(i, &parameter_desc);
    assert(SUCCEEDED(hr));

    input_elements.emplace_back(parameter_desc.SemanticName, parameter_desc.SemanticIndex, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0);
  }

  ComPtr<ID3D11InputLayout> input_layout;
  hr = device->CreateInputLayout(input_elements.data(), static_cast<UINT>(input_elements.size()), kvsBin, ARRAYSIZE(kvsBin), &input_layout);
  assert(SUCCEEDED(hr));

  std::array constexpr vertex_data{
    0.0f, 0.5f,
    0.5f, -0.5f,
    -0.5f, -0.5f
  };

  D3D11_BUFFER_DESC constexpr vertex_buffer_desc{
    .ByteWidth = sizeof vertex_data,
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_VERTEX_BUFFER,
    .CPUAccessFlags = 0,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  D3D11_SUBRESOURCE_DATA const vertex_buffer_init_data{
    .pSysMem = vertex_data.data(),
    .SysMemPitch = 0,
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Buffer> vertex_buffer;
  hr = device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_init_data, &vertex_buffer);
  assert(SUCCEEDED(hr));

  std::array<std::uint16_t, 3> constexpr index_data{
    0, 1, 2
  };

  D3D11_BUFFER_DESC constexpr index_buffer_desc{
    .ByteWidth = sizeof index_data,
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_INDEX_BUFFER,
    .CPUAccessFlags = 0,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  D3D11_SUBRESOURCE_DATA const index_buffer_init_data{
    .pSysMem = index_data.data(),
    .SysMemPitch = 0,
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Buffer> index_buffer;
  hr = device->CreateBuffer(&index_buffer_desc, &index_buffer_init_data, &index_buffer);
  assert(SUCCEEDED(hr));

  D3D11_BUFFER_DESC constexpr cbuffer_desc{
    .ByteWidth = sizeof(ConstantBuffer) + 16 - sizeof(ConstantBuffer) % 16,
    .Usage = D3D11_USAGE_DYNAMIC,
    .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  ComPtr<ID3D11Buffer> cbuffer;
  hr = device->CreateBuffer(&cbuffer_desc, nullptr, &cbuffer);
  assert(SUCCEEDED(hr));

  D3D11_TEXTURE2D_DESC constexpr texture_desc{
    .Width = 1,
    .Height = 1,
    .MipLevels = 1,
    .ArraySize = 1,
    .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    .CPUAccessFlags = 0,
    .MiscFlags = 0
  };

  std::array constexpr green{0.16f, 0.67f, 0.53f, 1.0f};
  std::array constexpr red{0.89f, 0.14f, 0.17f, 1.0f};
  std::array constexpr dark_gray{0.1f, 0.1f, 0.1f, 1.0f};

  D3D11_SUBRESOURCE_DATA const texture_init_data{
    .pSysMem = (windowed_hardware_composition_supported ? green : red).data(),
    .SysMemPitch = 128,
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Texture2D> texture;
  hr = device->CreateTexture2D(&texture_desc, &texture_init_data, &texture);
  assert(SUCCEEDED(hr));

  D3D11_SHADER_RESOURCE_VIEW_DESC constexpr texture_srv_desc{
    .Format = texture_desc.Format,
    .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
    .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}
  };

  ComPtr<ID3D11ShaderResourceView> texture_srv;
  hr = device->CreateShaderResourceView(texture.Get(), &texture_srv_desc, &texture_srv);
  assert(SUCCEEDED(hr));

  while (true) {
#ifndef NO_WAITABLE_SWAP_CHAIN
    WaitForSingleObjectEx(frame_latency_waitable_object, 1000, true);
#endif

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        return static_cast<int>(msg.wParam);
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    D3D11_MAPPED_SUBRESOURCE cbuffer_mapped;
    hr = deferred_ctx->Map(cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cbuffer_mapped);
    assert(SUCCEEDED(hr));

    ConstantBuffer const cbuffer_data{
      .square_color = fullscreen_hardware_composition_supported ? green : red
    };

    std::memcpy(cbuffer_mapped.pData, &cbuffer_data, sizeof cbuffer_data);
    deferred_ctx->Unmap(cbuffer.Get(), 0);

    deferred_ctx->CSSetShader(compute_shader.Get(), nullptr, 0);
    deferred_ctx->CSSetConstantBuffers(CONSTANT_BUFFER_SLOT, 1, cbuffer.GetAddressOf());
    deferred_ctx->CSSetUnorderedAccessViews(0, 1, back_buffer_uav.GetAddressOf(), nullptr);

    deferred_ctx->ClearUnorderedAccessViewFloat(back_buffer_uav.Get(), dark_gray.data());
    deferred_ctx->Dispatch(50, 50, 1);

    ComPtr<ID3D11CommandList> command_list;
    hr = deferred_ctx->FinishCommandList(FALSE, &command_list);
    assert(SUCCEEDED(hr));

    immediate_ctx->ExecuteCommandList(command_list.Get(), FALSE);

    UINT constexpr vertex_buffer_stride{2 * sizeof(float)};
    UINT constexpr vertex_buffer_offset{0};
    deferred_ctx->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_buffer_stride, &vertex_buffer_offset);
    deferred_ctx->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    deferred_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deferred_ctx->IASetInputLayout(input_layout.Get());

    deferred_ctx->VSSetShader(vertex_shader.Get(), nullptr, 0);
    deferred_ctx->VSSetConstantBuffers(CONSTANT_BUFFER_SLOT, 1, cbuffer.GetAddressOf());

    deferred_ctx->PSSetShader(pixel_shader.Get(), nullptr, 0);
    deferred_ctx->PSSetShaderResources(TEXTURE_SLOT, 1, texture_srv.GetAddressOf());

    D3D11_VIEWPORT constexpr viewport{
      .TopLeftX = 0,
      .TopLeftY = 0,
      .Width = swap_chain_width,
      .Height = swap_chain_height,
      .MinDepth = 0,
      .MaxDepth = 1
    };

    deferred_ctx->RSSetViewports(1, &viewport);

    deferred_ctx->OMSetRenderTargets(1, back_buffer_rtv.GetAddressOf(), nullptr);

    deferred_ctx->DrawIndexedInstanced(static_cast<UINT>(index_data.size()), 1, 0, 0, 0);

    hr = deferred_ctx->FinishCommandList(FALSE, &command_list);
    assert(SUCCEEDED(hr));

    immediate_ctx->ExecuteCommandList(command_list.Get(), FALSE);

    hr = swap_chain->Present(0, present_flags);
    assert(SUCCEEDED(hr));
  }
}
