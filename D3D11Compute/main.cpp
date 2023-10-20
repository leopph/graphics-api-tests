#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dxgi1_6.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <memory>
#include <type_traits>

#ifndef NDEBUG
#include "shaders/generated/Debug/ComputeShaderBin.h"
#else
#include "shaders/generated/Release/ComputeShaderBin.h"
#endif

using Microsoft::WRL::ComPtr;

constexpr auto WINDOW_WIDTH{1280};
constexpr auto WINDOW_HEIGHT{720};

auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
  if (msg == WM_CLOSE) {
    PostQuitMessage(0);
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

auto WINAPI WinMain(HINSTANCE const hInstance, HINSTANCE const hPrevInstance, PSTR const lpCmdLine,
                    int const nShowCmd) -> int {
  WNDCLASSW const wndClass{
    .style = 0,
    .lpfnWndProc = &WindowProc,
    .cbClsExtra = 0,
    .cbWndExtra = 0,
    .hInstance = hInstance,
    .hIcon = nullptr,
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .hbrBackground = nullptr,
    .lpszMenuName = nullptr,
    .lpszClassName = L"MyWindowClass"
  };

  if (!RegisterClassW(&wndClass)) {
    return -1;
  }

  constexpr auto windowStyle{WS_OVERLAPPEDWINDOW};

  RECT windowRect{0, 0, 1280, 720};
  AdjustWindowRect(&windowRect, windowStyle, FALSE);

  std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) {
    DestroyWindow(hwnd);
  })> const hwnd{
    CreateWindowExW(0, wndClass.lpszClassName, L"D3D11 Compute", windowStyle, 0, 0, windowRect.right - windowRect.left,
                    windowRect.bottom - windowRect.top,
                    nullptr, nullptr, hInstance, nullptr)
  };

  if (!hwnd) {
    return -2;
  }

  ShowWindow(hwnd.get(), nShowCmd);

  UINT d3dFlags{0};

#ifndef NDEBUG
  d3dFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  ComPtr<ID3D11Device> d3dDevice;
  ComPtr<ID3D11DeviceContext> imCtx;

  auto hr{
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, d3dFlags, std::array{D3D_FEATURE_LEVEL_11_0}.data(),
                      1, D3D11_SDK_VERSION, d3dDevice.GetAddressOf(), nullptr, imCtx.GetAddressOf())
  };
  assert(SUCCEEDED(hr));

#ifndef NDEBUG
  {
    ComPtr<ID3D11Debug> d3dDebug;
    hr = d3dDevice.As<ID3D11Debug>(&d3dDebug);
    assert(SUCCEEDED(hr));

    ComPtr<ID3D11InfoQueue> d3dInfoQueue;
    hr = d3dDebug.As<ID3D11InfoQueue>(&d3dInfoQueue);
    assert(SUCCEEDED(hr));

    hr = d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    assert(SUCCEEDED(hr));
    hr = d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
    assert(SUCCEEDED(hr));
  }
#endif

  ComPtr<IDXGIDevice4> dxgiDevice4;
  hr = d3dDevice.As(&dxgiDevice4);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIAdapter> dxgiAdapter;
  hr = dxgiDevice4->GetAdapter(dxgiAdapter.GetAddressOf());
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIFactory6> dxgiFactory6;
  hr = dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory6.GetAddressOf()));
  assert(SUCCEEDED(hr));

  constexpr auto texFormat{DXGI_FORMAT_R8G8B8A8_UNORM};

  DXGI_SWAP_CHAIN_DESC1 constexpr swapChainDesc{
    .Width = 0,
    .Height = 0,
    .Format = texFormat,
    .Stereo = FALSE,
    .SampleDesc{.Count = 1, .Quality = 0},
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = 2,
    .Scaling = DXGI_SCALING_STRETCH,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
    .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
  };

  ComPtr<IDXGISwapChain1> swapChain1;
  hr = dxgiFactory6->CreateSwapChainForHwnd(d3dDevice.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr,
                                            swapChain1.GetAddressOf());
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11Texture2D> backBuf;
  hr = swapChain1->GetBuffer(0, IID_PPV_ARGS(&backBuf));
  assert(SUCCEEDED(hr));

  D3D11_TEXTURE2D_DESC constexpr renderTexDesc{
    .Width = WINDOW_WIDTH,
    .Height = WINDOW_HEIGHT,
    .MipLevels = 1,
    .ArraySize = 1,
    .Format = texFormat,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Usage = D3D11_USAGE_DEFAULT,
    .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
    .CPUAccessFlags = 0,
    .MiscFlags = 0
  };

  ComPtr<ID3D11Texture2D> renderTex;
  hr = d3dDevice->CreateTexture2D(&renderTexDesc, nullptr, &renderTex);
  assert(SUCCEEDED(hr));

  D3D11_UNORDERED_ACCESS_VIEW_DESC constexpr renderTexUavDesc{
    .Format = renderTexDesc.Format,
    .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
    .Texture2D = {.MipSlice = 0}
  };

  ComPtr<ID3D11UnorderedAccessView> renderTexUav;
  hr = d3dDevice->CreateUnorderedAccessView(renderTex.Get(), &renderTexUavDesc, &renderTexUav);
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11ComputeShader> cs;
  hr = d3dDevice->CreateComputeShader(gComputeShaderBin, ARRAYSIZE(gComputeShaderBin), nullptr, &cs);
  assert(SUCCEEDED(hr));

  while (true) {
    MSG msg;

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        return static_cast<int>(msg.wParam);
      }

      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    imCtx->CSSetShader(cs.Get(), nullptr, 0);
    imCtx->CSSetUnorderedAccessViews(0, 1, std::array{renderTexUav.Get()}.data(), nullptr);

    imCtx->ClearUnorderedAccessViewFloat(renderTexUav.Get(), std::array{0.1f, 0.1f, 0.1f, 1.0f}.data());
    imCtx->Dispatch(50, 50, 1);

    imCtx->CopyResource(backBuf.Get(), renderTex.Get());

    hr = swapChain1->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    assert(SUCCEEDED(hr));
  }
}
