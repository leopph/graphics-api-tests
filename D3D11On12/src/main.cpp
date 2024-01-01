#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d11on12.h>

#include <wrl/client.h>

#include <array>
#include <cassert>
#include <memory>

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
  WNDCLASSW const window_class{
    .lpfnWndProc = &WindowProc,
    .hInstance = hInstance,
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"D3D11On12Test"
  };

  auto const result{RegisterClassW(&window_class)};
  assert(result);

  using WindowDeleterType = decltype([](HWND const hwnd) {
    if (hwnd) {
      DestroyWindow(hwnd);
    }
  });

  std::unique_ptr<std::remove_pointer_t<HWND>, WindowDeleterType> const hwnd{CreateWindowExW(0, window_class.lpszClassName, L"D3D11On12Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr)};
  assert(hwnd);
  ShowWindow(hwnd.get(), nShowCmd);

  HRESULT hr{};

  using Microsoft::WRL::ComPtr;

#ifndef NDEBUG
  ComPtr<ID3D12Debug5> debug;
  hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
  assert(SUCCEEDED(hr));
  debug->EnableDebugLayer();
#endif

  UINT factory_create_flags{0};
#ifndef NDEBUG
  factory_create_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  ComPtr<IDXGIFactory7> factory;
  hr = CreateDXGIFactory2(factory_create_flags, IID_PPV_ARGS(&factory));
  assert(SUCCEEDED(hr));

  ComPtr<ID3D12Device9> device;
  hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  ComPtr<ID3D12InfoQueue> info_queue;
  hr = device.As(&info_queue);
  assert(SUCCEEDED(hr));
  hr = info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
  assert(SUCCEEDED(hr));
  hr = info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
  assert(SUCCEEDED(hr));
#endif

  constexpr auto max_frames_in_flight{1};

  D3D12_COMMAND_QUEUE_DESC constexpr queue_desc{
    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
    .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    .NodeMask = 0
  };

  ComPtr<ID3D12CommandQueue> queue;
  hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
  assert(SUCCEEDED(hr));

  std::array<ComPtr<ID3D12CommandAllocator>, max_frames_in_flight> command_allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList7>, max_frames_in_flight> command_lists;

  for (auto i{0}; i < max_frames_in_flight; i++) {
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators[i]));
    assert(SUCCEEDED(hr));

    hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&command_lists[i]));
    assert(SUCCEEDED(hr));
  }

  constexpr auto swap_chain_buffer_count{2};

  DXGI_SWAP_CHAIN_DESC1 constexpr swap_chain_desc{
    .Width = 1,
    .Height = 1,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .Stereo = FALSE,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .BufferUsage = 0,
    .BufferCount = swap_chain_buffer_count,
    .Scaling = DXGI_SCALING_NONE,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
    .Flags = 0
  };

  ComPtr<IDXGISwapChain1> tmp_swap_chain;
  hr = factory->CreateSwapChainForHwnd(queue.Get(), hwnd.get(), &swap_chain_desc, nullptr, nullptr, &tmp_swap_chain);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGISwapChain4> swap_chain;
  hr = tmp_swap_chain.As(&swap_chain);
  assert(SUCCEEDED(hr));

  auto back_buffer_idx{swap_chain->GetCurrentBackBufferIndex()};

  std::array<ComPtr<ID3D12Resource2>, swap_chain_buffer_count> back_buffers;

  for (auto i{0}; i < swap_chain_buffer_count; i++) {
    hr = swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]));
    assert(SUCCEEDED(hr));
  }

  UINT64 frame_fence_value{max_frames_in_flight - 1};
  ComPtr<ID3D12Fence1> frame_fence;
  hr = device->CreateFence(frame_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frame_fence));
  assert(SUCCEEDED(hr));
  hr = frame_fence->SetName(L"Frame Fence");
  assert(SUCCEEDED(hr));

  UINT64 misc_fence_value{0};
  ComPtr<ID3D12Fence1> misc_fence;
  hr = device->CreateFence(misc_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&misc_fence));
  assert(SUCCEEDED(hr));
  hr = misc_fence->SetName(L"Misc Fence");
  assert(SUCCEEDED(hr));

  auto const signal_and_wait_fence{
    [&queue](ID3D12Fence* const fence, UINT64 const signal_value, UINT64 const wait_value) {
      [[maybe_unused]] auto hr1{queue->Signal(fence, signal_value)};
      assert(SUCCEEDED(hr1));

      if (fence->GetCompletedValue() < wait_value) {
        hr1 = fence->SetEventOnCompletion(wait_value, nullptr);
        assert(SUCCEEDED(hr1));
      }
    }
  };

  auto const wait_for_in_flight_frames{
    [&] {
      auto const signal_value{++frame_fence_value};
      auto const wait_value{signal_value - max_frames_in_flight + 1};
      signal_and_wait_fence(frame_fence.Get(), signal_value, wait_value);
    }
  };

  auto const wait_for_gpu_idle{
    [&] {
      ComPtr<ID3D12Fence1> fence;
      hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
      assert(SUCCEEDED(hr));
      signal_and_wait_fence(fence.Get(), 1, 1);
    }
  };

  UINT device_creation_flags11{0};
#ifndef NDEBUG
  device_creation_flags11 |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> im_ctx11;
  hr = D3D11On12CreateDevice(device.Get(), device_creation_flags11, nullptr, 0, std::array<IUnknown*, 1>{queue.Get()}.data(), 1, 0, &device11, &im_ctx11, nullptr);
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  ComPtr<ID3D11Debug> debug11;
  hr = device11.As(&debug11);
  assert(SUCCEEDED(hr));
  ComPtr<ID3D11InfoQueue> info_queue11;
  hr = debug11.As<ID3D11InfoQueue>(&info_queue11);
  assert(SUCCEEDED(hr));
  hr = info_queue11->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
  assert(SUCCEEDED(hr));
  hr = info_queue11->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
  assert(SUCCEEDED(hr));
#endif

  ComPtr<ID3D11On12Device2> device11_on12;
  hr = device11.As(&device11_on12);
  assert(SUCCEEDED(hr));

  std::array constexpr tex_data{1.0f, 0.0f, 1.0f, 1.0f};

  D3D11_SUBRESOURCE_DATA const tex_init_data{
    .pSysMem = tex_data.data(),
    .SysMemPitch = sizeof(tex_data),
    .SysMemSlicePitch = 0
  };

  D3D11_TEXTURE2D_DESC constexpr tex_desc{
    .Width = 1,
    .Height = 1,
    .MipLevels = 1,
    .ArraySize = 1,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    .CPUAccessFlags = 0,
    .MiscFlags = 0
  };

  ComPtr<ID3D11Texture2D> tex;
  hr = device11->CreateTexture2D(&tex_desc, &tex_init_data, &tex);
  assert(SUCCEEDED(hr));

  im_ctx11->Flush();

  int frame_count{0};

  while (true) {
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        wait_for_gpu_idle();
        return static_cast<int>(msg.wParam);
      }

      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    auto const frame_idx{frame_count % max_frames_in_flight};

    ComPtr<ID3D12Resource> tex12;
    // TODO THIS STALLS THE QUEUE
    hr = device11_on12->UnwrapUnderlyingResource(tex.Get(), queue.Get(), IID_PPV_ARGS(&tex12));
    assert(SUCCEEDED(hr));

    std::array const pre_copy_barriers{
      D3D12_RESOURCE_BARRIER{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
          .pResource = tex12.Get(),
          .Subresource = 0,
          .StateBefore = D3D12_RESOURCE_STATE_COMMON,
          .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE

        }
      },
      D3D12_RESOURCE_BARRIER{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
          .pResource = back_buffers[back_buffer_idx].Get(),
          .Subresource = 0,
          .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
          .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST

        }
      }
    };

    std::array const post_copy_barriers{
      D3D12_RESOURCE_BARRIER{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
          .pResource = tex12.Get(),
          .Subresource = 0,
          .StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE,
          .StateAfter = D3D12_RESOURCE_STATE_COMMON
        }
      },
      D3D12_RESOURCE_BARRIER{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
          .pResource = back_buffers[back_buffer_idx].Get(),
          .Subresource = 0,
          .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
          .StateAfter = D3D12_RESOURCE_STATE_PRESENT

        }
      }
    };

    hr = command_allocators[frame_idx]->Reset();
    assert(SUCCEEDED(hr));

    hr = command_lists[frame_idx]->Reset(command_allocators[frame_idx].Get(), nullptr);
    assert(SUCCEEDED(hr));

    command_lists[frame_idx]->ResourceBarrier(static_cast<UINT>(std::size(pre_copy_barriers)), pre_copy_barriers.data());
    command_lists[frame_idx]->CopyResource(back_buffers[back_buffer_idx].Get(), tex12.Get());
    command_lists[frame_idx]->ResourceBarrier(static_cast<UINT>(std::size(post_copy_barriers)), post_copy_barriers.data());

    hr = command_lists[frame_idx]->Close();
    assert(SUCCEEDED(hr));

    queue->ExecuteCommandLists(1, std::array<ID3D12CommandList*, 1>{command_lists[frame_idx].Get()}.data());

    ++misc_fence_value;
    hr = queue->Signal(misc_fence.Get(), misc_fence_value);
    assert(SUCCEEDED(hr));

    hr = device11_on12->ReturnUnderlyingResource(tex.Get(), 1, &misc_fence_value, std::array<ID3D12Fence*, 1>{misc_fence.Get()}.data());
    assert(SUCCEEDED(hr));

    hr = swap_chain->Present(1, 0);
    assert(SUCCEEDED(hr));

    wait_for_in_flight_frames();
    back_buffer_idx = swap_chain->GetCurrentBackBufferIndex();
    ++frame_count;
  }
}
