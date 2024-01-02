#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3dcompiler.h>

#ifndef NDEBUG
#include <dxgidebug.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <format>
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

#include MAKE_SHADER_INCLUDE_PATH(ps)
#include MAKE_SHADER_INCLUDE_PATH(vs)

using Microsoft::WRL::ComPtr;


#define USE_WAITABLE_SWAPCHAIN 0


enum class DisplayMode {
  ExclusiveFullscreen,
  WindowedBorderless,
  Windowed
};


struct AppData {
  ComPtr<ID3D11Device5> d3dDevice;
  ComPtr<IDXGISwapChain4> swapChain;
  ComPtr<ID3D11RenderTargetView> backBufRtv;

  DisplayMode displayMode{DisplayMode::Windowed};
  bool minimizeBorderlessOnFocusLoss{false};
  RECT windowedRect{};

  bool allowTearing{false};
  bool supportsMultiplaneOverlays{false};

  UINT syncInterval{0};
  UINT swapChainFlags{0};
  UINT presentFlags{0};
};


struct ColorCBufData {
  float objectColor[4];
};


struct OffsetCBufData {
  float offsetX;
};


namespace {
  auto constexpr MAX_FPS{0};
  UINT constexpr MAX_FRAMES_IN_FLIGHT{16};
  auto constexpr DEFAULT_DISPLAY_MODE{DisplayMode::WindowedBorderless};
  auto constexpr NUM_DRAW_CALLS_PER_FRAME{1};
  auto constexpr NUM_INSTANCES_PER_DRAW_CALL{1};


  auto constexpr MIN_FRAME_TIME{
    [] {
      if constexpr (MAX_FPS <= 0) {
        return std::chrono::nanoseconds::zero();
      }
      else {
        auto constexpr secondInNanos{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1})};
        return std::chrono::nanoseconds{(secondInNanos.count() + MAX_FPS - 1) / MAX_FPS};
      }
    }()
  };
  DWORD constexpr WINDOWED_STYLE{WS_OVERLAPPEDWINDOW};
  DWORD constexpr BORDERLESS_STYLE{WS_OVERLAPPED};
  FLOAT constexpr CLEAR_COLOR[]{0.21f, 0.27f, 0.31f, 1};
  FLOAT constexpr OVERLAY_SUPPORT_COLOR[]{0.16f, 0.67f, 0.53f, 1};
  FLOAT constexpr NO_OVERLAY_SUPPORT_COLOR[]{0.89f, 0.14f, 0.17f, 1};


  auto RecreateSwapChainRTV(AppData& appData) -> void {
    ComPtr<ID3D11Texture2D> backBuf;
    auto hr{appData.swapChain->GetBuffer(0, IID_PPV_ARGS(backBuf.GetAddressOf()))};
    assert(SUCCEEDED(hr));

    D3D11_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
      .Texture2D = {
        .MipSlice = 0
      }
    };

    hr = appData.d3dDevice->CreateRenderTargetView(backBuf.Get(), &rtvDesc, appData.backBufRtv.ReleaseAndGetAddressOf());
    assert(SUCCEEDED(hr));
  }


  auto ResizeSwapChain(AppData& appData) -> void {
    auto hr{appData.backBufRtv.Reset()};
    assert(SUCCEEDED(hr));

    hr = appData.swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, appData.swapChainFlags);
    assert(SUCCEEDED(hr));

    RecreateSwapChainRTV(appData);
  }


  auto ChangeDisplayMode(HWND const hwnd, DisplayMode const displayMode) -> void {
    auto* const appData{reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))};

    if (appData->displayMode == DisplayMode::Windowed) {
      GetWindowRect(hwnd, &appData->windowedRect);
    }

    appData->displayMode = displayMode;

    switch (displayMode) {
    case DisplayMode::ExclusiveFullscreen: {
      BOOL fullscreenState;
      auto hr{appData->swapChain->GetFullscreenState(&fullscreenState, nullptr)};
      assert(SUCCEEDED(hr));

      if (!fullscreenState) {
        hr = appData->swapChain->SetFullscreenState(true, nullptr);
        assert(SUCCEEDED(hr));
      }

      appData->presentFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
      break;
    }

    case DisplayMode::WindowedBorderless: {
      BOOL fullscreenState;
      auto hr{appData->swapChain->GetFullscreenState(&fullscreenState, nullptr)};
      assert(SUCCEEDED(hr));

      if (fullscreenState) {
        hr = appData->swapChain->SetFullscreenState(false, nullptr);
        assert(SUCCEEDED(hr));
      }

      if (appData->allowTearing) {
        appData->presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
      }

      SetWindowLongPtrW(hwnd, GWL_STYLE, BORDERLESS_STYLE);

      MONITORINFO monitorInfo{.cbSize = sizeof(MONITORINFO)};
      GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);

      auto const width{monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left};
      auto const height{monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top};
      SetWindowPos(hwnd, nullptr, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      break;
    }

    case DisplayMode::Windowed: {
      BOOL fullscreenState;
      auto hr{appData->swapChain->GetFullscreenState(&fullscreenState, nullptr)};
      assert(SUCCEEDED(hr));

      if (fullscreenState) {
        hr = appData->swapChain->SetFullscreenState(false, nullptr);
        assert(SUCCEEDED(hr));
      }

      if (appData->allowTearing) {
        appData->presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
      }

      SetWindowLongPtrW(hwnd, GWL_STYLE, WINDOWED_STYLE);
      auto const width{appData->windowedRect.right - appData->windowedRect.left};
      auto const height{appData->windowedRect.bottom - appData->windowedRect.top};
      SetWindowPos(hwnd, nullptr, appData->windowedRect.left, appData->windowedRect.top, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      break;
    }
    }

    ResizeSwapChain(*appData);
  }


  auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
    auto* appData = reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
      appData = new AppData;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(appData));
      return 0;
    }

    case WM_DESTROY: {
      BOOL fullscreenState;
      auto hr{appData->swapChain->GetFullscreenState(&fullscreenState, nullptr)};
      assert(SUCCEEDED(hr));

      if (fullscreenState) {
        hr = appData->swapChain->SetFullscreenState(false, nullptr);
        assert(SUCCEEDED(hr));
      }

      delete appData;
      return 0;
    }

    case WM_SIZE: {
      auto const width = LOWORD(lparam);
      auto const height = HIWORD(lparam);

      if (width && height) {
        if (appData->swapChain && appData->backBufRtv && appData->d3dDevice) {
          ResizeSwapChain(*appData);
        }
      }

      return 0;
    }

    case WM_SYSCOMMAND: {
      if (wparam == SC_KEYMENU) {
        return 0;
      }

      break;
    }

    case WM_KEYDOWN: {
      auto const keyFlags{HIWORD(lparam)};

      if (!(keyFlags & KF_REPEAT)) {
        if (wparam == 'F') {
          ChangeDisplayMode(hwnd, DisplayMode::ExclusiveFullscreen);
        }
        else if (wparam == 'B') {
          ChangeDisplayMode(hwnd, DisplayMode::WindowedBorderless);
        }
        else if (wparam == 'W') {
          ChangeDisplayMode(hwnd, DisplayMode::Windowed);
        }
        else if (wparam == 'M') {
          appData->minimizeBorderlessOnFocusLoss = !appData->minimizeBorderlessOnFocusLoss;
        }
        else if (wparam == 'V') {
          appData->syncInterval = 1 - appData->syncInterval;
        }
      }

      break;
    }

    case WM_ACTIVATEAPP: {
      if (wparam == TRUE && appData->displayMode == DisplayMode::ExclusiveFullscreen) {
        ChangeDisplayMode(hwnd, DisplayMode::ExclusiveFullscreen);
      }
      else if (wparam == FALSE && appData->displayMode == DisplayMode::WindowedBorderless && appData->minimizeBorderlessOnFocusLoss) {
        ShowWindow(hwnd, SW_MINIMIZE);
        return 0;
      }

      break;
    }

    case WM_CLOSE: {
      DestroyWindow(hwnd);
      PostQuitMessage(0);
      return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}


auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance, [[maybe_unused]] _In_ PWSTR const pCmdLine, _In_ int const nCmdShow) -> int {
  WNDCLASSW const windowClass{
    .style = CS_HREDRAW | CS_VREDRAW,
    .lpfnWndProc = &WindowProc,
    .hInstance = hInstance,
    .hIcon = LoadIconW(nullptr, IDI_APPLICATION),
    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
    .lpszClassName = L"MyWindowClass"
  };
  RegisterClassW(&windowClass);

  auto const hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"MyWindow", WINDOWED_STYLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, windowClass.hInstance, nullptr);
  ShowWindow(hwnd, nCmdShow);

  auto* const appData = reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  UINT deviceFlags = 0;
#ifndef NDEBUG
  deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  HRESULT hr{};
  ComPtr<ID3D11DeviceContext4> imCtx;

  {
    ComPtr<ID3D11Device> tmpD3dDevice;
    ComPtr<ID3D11DeviceContext> tmpImCtx;

    D3D_FEATURE_LEVEL constexpr featureLevels[]{D3D_FEATURE_LEVEL_11_0};
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, featureLevels, 1, D3D11_SDK_VERSION, tmpD3dDevice.GetAddressOf(), nullptr, tmpImCtx.GetAddressOf());
    assert(SUCCEEDED(hr));

    hr = tmpD3dDevice.As(&appData->d3dDevice);
    assert(SUCCEEDED(hr));
    hr = tmpImCtx.As(&imCtx);
    assert(SUCCEEDED(hr));
  }


#ifndef NDEBUG
  {
    ComPtr<ID3D11Debug> d3dDebug;
    hr = appData->d3dDevice.As<ID3D11Debug>(&d3dDebug);
    assert(SUCCEEDED(hr));

    ComPtr<ID3D11InfoQueue> d3dInfoQueue;
    hr = d3dDebug.As<ID3D11InfoQueue>(&d3dInfoQueue);
    assert(SUCCEEDED(hr));

    hr = d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    assert(SUCCEEDED(hr));
    hr = d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
    assert(SUCCEEDED(hr));

    ComPtr<IDXGIDebug1> dxgiDebug;
    hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiDebug.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
    hr = dxgiDebug.As(&dxgiInfoQueue);
    assert(SUCCEEDED(hr));

    hr = dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    assert(SUCCEEDED(hr));
    hr = dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
    assert(SUCCEEDED(hr));
  }

#endif

  ComPtr<ID3D11DeviceContext4> dCtx;

  {
    ComPtr<ID3D11DeviceContext3> tmpDCtx;
    hr = appData->d3dDevice->CreateDeferredContext3(0, tmpDCtx.GetAddressOf());
    assert(SUCCEEDED(hr));

    hr = tmpDCtx.As(&dCtx);
    assert(SUCCEEDED(hr));
  }

  ComPtr<IDXGIDevice4> dxgiDevice;
  hr = appData->d3dDevice.As(&dxgiDevice);
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIAdapter> dxgiAdapter;
  hr = dxgiDevice->GetAdapter(dxgiAdapter.GetAddressOf());
  assert(SUCCEEDED(hr));

  ComPtr<IDXGIFactory6> dxgiFactory;
  hr = dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
  assert(SUCCEEDED(hr));

  BOOL allowTearing{};
  hr = dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof allowTearing);
  assert(SUCCEEDED(hr));

  OutputDebugStringW(std::format(L"Tearing in windowed mode is {}supported.\r\n", allowTearing ? L"" : L"not ").c_str());

  appData->allowTearing = allowTearing;

  if (allowTearing) {
    appData->swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    appData->presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
  }

#if USE_WAITABLE_SWAPCHAIN
  appData->swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
#endif

  {
    DXGI_SWAP_CHAIN_DESC1 const swapChainDesc{
      .Width = 0,
      .Height = 0,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .Stereo = FALSE,
      .SampleDesc{
        .Count = 1,
        .Quality = 0
      },
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .Scaling = DXGI_SCALING_STRETCH,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
      .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
      .Flags = appData->swapChainFlags
    };

    ComPtr<IDXGISwapChain1> tmpSwapChain;
    hr = dxgiFactory->CreateSwapChainForHwnd(appData->d3dDevice.Get(), hwnd, &swapChainDesc, nullptr, nullptr, tmpSwapChain.GetAddressOf());
    assert(SUCCEEDED(hr));

    hr = tmpSwapChain.As(&appData->swapChain);
    assert(SUCCEEDED(hr));
  }

  hr = dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES);
  assert(SUCCEEDED(hr));

#if USE_WAITABLE_SWAPCHAIN
  auto const frameLatencyWaitableObject{appData->swapChain->GetFrameLatencyWaitableObject()};
  hr = appData->swapChain->SetMaximumFrameLatency(MAX_FRAMES_IN_FLIGHT);
#else
  hr = dxgiDevice->SetMaximumFrameLatency(MAX_FRAMES_IN_FLIGHT);
#endif
  assert(SUCCEEDED(hr));

  RecreateSwapChainRTV(*appData);

  if (ComPtr<IDXGIOutput> output; SUCCEEDED(appData->swapChain->GetContainingOutput(output.GetAddressOf()))) {
    if (ComPtr<IDXGIOutput2> output2; SUCCEEDED(output.As(&output2))) {
      appData->supportsMultiplaneOverlays = output2->SupportsOverlays();
    }

    if (ComPtr<IDXGIOutput6> output6; SUCCEEDED(output.As(&output6))) {
      if (UINT supportFlags; SUCCEEDED(output6->CheckHardwareCompositionSupport(&supportFlags))) {
        OutputDebugStringW(std::format(L"Fullscreen hardware composition is {}supported.\r\n", supportFlags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_FULLSCREEN ? L"" : L"not ").c_str());
        OutputDebugStringW(std::format(L"Windowed hardware composition is {}supported.\r\n", supportFlags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED ? L"" : L"not ").c_str());
      }
    }
  }

  ChangeDisplayMode(hwnd, DEFAULT_DISPLAY_MODE);

  ComPtr<ID3D11VertexShader> vertexShader;
  hr = appData->d3dDevice->CreateVertexShader(kvsBin, ARRAYSIZE(kvsBin), nullptr, vertexShader.GetAddressOf());
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11PixelShader> pixelShader;
  hr = appData->d3dDevice->CreatePixelShader(kpsBin, ARRAYSIZE(kpsBin), nullptr, pixelShader.GetAddressOf());
  assert(SUCCEEDED(hr));

  ComPtr<ID3D11ShaderReflection> vsReflect;
  hr = D3DReflect(kvsBin, ARRAYSIZE(kvsBin), IID_PPV_ARGS(&vsReflect));
  assert(SUCCEEDED(hr));

  D3D11_SHADER_DESC vsDesc;
  hr = vsReflect->GetDesc(&vsDesc);
  assert(SUCCEEDED(hr));

  std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
  inputElements.reserve(vsDesc.InputParameters);

  for (UINT i{0}; i < vsDesc.InputParameters; i++) {
    D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
    hr = vsReflect->GetInputParameterDesc(i, &paramDesc);
    assert(SUCCEEDED(hr));

    inputElements.emplace_back(paramDesc.SemanticName, paramDesc.SemanticIndex, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0);
  }

  ComPtr<ID3D11InputLayout> inputLayout;
  hr = appData->d3dDevice->CreateInputLayout(inputElements.data(), static_cast<UINT>(inputElements.size()), kvsBin, ARRAYSIZE(kvsBin), inputLayout.GetAddressOf());
  assert(SUCCEEDED(hr));

  std::array constexpr VERTEX_DATA{
    -0.05f, 1.0f,
    0.05f, 1.0f,
    0.05f, -1.0f,
    -0.05f, -1.0f
  };

  D3D11_BUFFER_DESC constexpr vertexBufferDesc{
    .ByteWidth = sizeof VERTEX_DATA,
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_VERTEX_BUFFER,
    .CPUAccessFlags = 0,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  D3D11_SUBRESOURCE_DATA const vertexSubresourceData{
    .pSysMem = VERTEX_DATA.data(),
    .SysMemPitch = 0,
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Buffer> vertexBuffer;
  hr = appData->d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexSubresourceData, vertexBuffer.GetAddressOf());
  assert(SUCCEEDED(hr));

  std::array constexpr INDEX_DATA{
    0u, 1u, 2u,
    2u, 3u, 0u
  };

  D3D11_BUFFER_DESC constexpr indexBufferDesc{
    .ByteWidth = sizeof INDEX_DATA,
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_INDEX_BUFFER,
    .CPUAccessFlags = 0,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  D3D11_SUBRESOURCE_DATA const subresourceData{
    .pSysMem = INDEX_DATA.data(),
    .SysMemPitch = 0,
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Buffer> indexBuffer;
  hr = appData->d3dDevice->CreateBuffer(&indexBufferDesc, &subresourceData, indexBuffer.GetAddressOf());
  assert(SUCCEEDED(hr));

  D3D11_BUFFER_DESC constexpr colorCBufDesc{
    .ByteWidth = sizeof(ColorCBufData) + 16 - sizeof(ColorCBufData) % 16,
    .Usage = D3D11_USAGE_IMMUTABLE,
    .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    .CPUAccessFlags = 0,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  D3D11_SUBRESOURCE_DATA const colorCBufData{
    .pSysMem = appData->supportsMultiplaneOverlays ? OVERLAY_SUPPORT_COLOR : NO_OVERLAY_SUPPORT_COLOR,
    .SysMemPitch = 0,
    .SysMemSlicePitch = 0
  };

  ComPtr<ID3D11Buffer> colorCBuf;
  hr = appData->d3dDevice->CreateBuffer(&colorCBufDesc, &colorCBufData, colorCBuf.GetAddressOf());
  assert(SUCCEEDED(hr));

  D3D11_BUFFER_DESC constexpr offsetCBufDesc{
    .ByteWidth = sizeof(OffsetCBufData) + 16 - sizeof(OffsetCBufData) % 16,
    .Usage = D3D11_USAGE_DYNAMIC,
    .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
    .MiscFlags = 0,
    .StructureByteStride = 0
  };

  ComPtr<ID3D11Buffer> offsetCBuf;
  hr = appData->d3dDevice->CreateBuffer(&offsetCBufDesc, nullptr, offsetCBuf.GetAddressOf());
  assert(SUCCEEDED(hr));

  auto const startTimePoint{std::chrono::steady_clock::now()};
  auto lastFrameTimePoint{startTimePoint};

  while (true) {
#if USE_WAITABLE_SWAPCHAIN
    WaitForSingleObjectEx(frameLatencyWaitableObject, 1000, true);
#endif

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        return static_cast<int>(msg.wParam);
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    UINT constexpr VERTEX_STRIDE{2 * sizeof(float)};
    UINT constexpr VERTEX_OFFSET{0};
    dCtx->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &VERTEX_STRIDE, &VERTEX_OFFSET);
    dCtx->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dCtx->IASetInputLayout(inputLayout.Get());

    dCtx->VSSetShader(vertexShader.Get(), nullptr, 0);
    dCtx->VSSetConstantBuffers(0, 1, offsetCBuf.GetAddressOf());

    dCtx->PSSetShader(pixelShader.Get(), nullptr, 0);
    dCtx->PSSetConstantBuffers(0, 1, colorCBuf.GetAddressOf());

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    D3D11_VIEWPORT const viewport{
      .TopLeftX = 0,
      .TopLeftY = 0,
      .Width = static_cast<FLOAT>(clientRect.right - clientRect.left),
      .Height = static_cast<FLOAT>(clientRect.bottom - clientRect.top),
      .MinDepth = 0,
      .MaxDepth = 1
    };

    dCtx->RSSetViewports(1, &viewport);

    D3D11_MAPPED_SUBRESOURCE mappedOffsetCBuf;
    hr = dCtx->Map(offsetCBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedOffsetCBuf);
    assert(SUCCEEDED(hr));

    auto* const offsetCBufData{static_cast<OffsetCBufData*>(mappedOffsetCBuf.pData)};
    auto const totalElapsedTimeSeconds{std::chrono::duration<float>{lastFrameTimePoint - startTimePoint}.count() / 6.f};
    offsetCBufData->offsetX = (std::abs(totalElapsedTimeSeconds - static_cast<float>(static_cast<int>(totalElapsedTimeSeconds)) - 0.5f) - 0.25f) * 3.8f;

    dCtx->Unmap(offsetCBuf.Get(), 0);

    dCtx->OMSetRenderTargets(1, appData->backBufRtv.GetAddressOf(), nullptr);
    dCtx->ClearRenderTargetView(appData->backBufRtv.Get(), CLEAR_COLOR);

    for (int i = 0; i < NUM_DRAW_CALLS_PER_FRAME; i++) {
      dCtx->DrawIndexedInstanced(static_cast<UINT>(std::size(INDEX_DATA)), NUM_INSTANCES_PER_DRAW_CALL, 0, 0, 0);
    }

    ComPtr<ID3D11CommandList> cmdList;
    hr = dCtx->FinishCommandList(FALSE, cmdList.GetAddressOf());
    assert(SUCCEEDED(hr));

    imCtx->ExecuteCommandList(cmdList.Get(), FALSE);

    hr = appData->swapChain->Present(appData->syncInterval, appData->syncInterval == 0 ? appData->presentFlags : appData->presentFlags & ~DXGI_PRESENT_ALLOW_TEARING);
    assert(SUCCEEDED(hr));

    while (true) {
      if (auto const deltaTime{std::chrono::steady_clock::now() - lastFrameTimePoint}; deltaTime >= MIN_FRAME_TIME) {
        lastFrameTimePoint += deltaTime;
        break;
      }
    }
  }
}
