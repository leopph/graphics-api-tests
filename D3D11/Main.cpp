#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#ifdef _NDEBUG
#include "shaders/generated/VertexShader.h"
#include "shaders/generated/PixelShader.h"
#else
#include "shaders/generated/VertexShaderDebug.h"
#include "shaders/generated/PixelShaderDebug.h"
#endif

#include <chrono>
#include <cmath>
#include <format>


#define USE_WAITABLE_SWAPCHAIN 0
#define USE_FENCE 1

namespace {
	auto constexpr MAX_FPS{ 0 };
	UINT constexpr NUM_FRAMES_IN_FLIGHT{ 1 };
	auto constexpr START_BORDERLESS{ true };
}


using Microsoft::WRL::ComPtr;


struct AppData {
	ComPtr<ID3D11Device> d3dDevice;
	ComPtr<ID3D11DeviceContext> immediateContext;
	ComPtr<ID3D11Debug> debug;
	ComPtr<IDXGISwapChain1> swapChain;
	ComPtr<ID3D11RenderTargetView> backBufRtv;
	ComPtr<ID3D11Buffer> colorCBuf;
	ComPtr<ID3D11Buffer> offsetCBuf;
	bool minimizeOnFocusLoss{ false };
	bool isBorderless{ false };
	RECT windowedRect;
};


struct ColorCBufData {
	float objectColor[4];
};


struct OffsetCBufData {
	float offsetX;
};


namespace {
	float constexpr VERTEX_DATA[]{
		-0.05f, 1.0f,
		0.05f, 1.0f,
		0.05f, -1.0f,
		-0.05f, -1.0f
	};
	unsigned constexpr INDEX_DATA[]{
		0, 1, 2,
		2, 3, 0
	};
	UINT constexpr NUM_INDICES{ ARRAYSIZE(INDEX_DATA) };
	UINT constexpr VERTEX_STRIDE{ 2 * sizeof(float) };
	UINT constexpr VERTEX_OFFSET{ 0 };
	auto constexpr MIN_FRAME_TIME{ MAX_FPS <= 0 ? std::chrono::nanoseconds::zero() : std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{ 1 }) / MAX_FPS };
	DWORD constexpr WINDOWED_STYLE{ WS_OVERLAPPEDWINDOW };
	DWORD constexpr BORDERLESS_STYLE{ WS_OVERLAPPED };
	FLOAT constexpr CLEAR_COLOR[]{ 0.21f, 0.27f, 0.31f, 1 };
	FLOAT constexpr OVERLAY_SUPPORT_COLOR[]{ 0.16f, 0.67f, 0.53f, 1 };
	FLOAT constexpr NO_OVERLAY_SUPPORT_COLOR[]{ 0.89f, 0.14f, 0.17f, 1 };


	UINT gSyncInterval{ 0 };
	UINT gSwapChainFlags{ 0 };
	UINT gPresentFlags{ 0 };
	FLOAT const* gObjectColor{ NO_OVERLAY_SUPPORT_COLOR };


	auto CreateBackBufRTV(AppData& appData) -> void {
		appData.backBufRtv.Reset();
		appData.swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, gSwapChainFlags);

		ComPtr<ID3D11Texture2D> backBuf;
		appData.swapChain->GetBuffer(0, IID_PPV_ARGS(backBuf.ReleaseAndGetAddressOf()));

		D3D11_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MipSlice = 0
			}
		};
		appData.d3dDevice->CreateRenderTargetView(backBuf.Get(), &rtvDesc, appData.backBufRtv.ReleaseAndGetAddressOf());
	}


	auto SwitchBorderlessState(HWND const hwnd) -> void {
		auto* const appData{ reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) };

		if (appData->isBorderless) {
			SetWindowLongPtrW(hwnd, GWL_STYLE, WINDOWED_STYLE);

			auto const width{ appData->windowedRect.right - appData->windowedRect.left};
			auto const height{ appData->windowedRect.bottom - appData->windowedRect.top };
			SetWindowPos(hwnd, nullptr, appData->windowedRect.left, appData->windowedRect.top, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		}
		else {
			GetWindowRect(hwnd, &appData->windowedRect);

			SetWindowLongPtrW(hwnd, GWL_STYLE, BORDERLESS_STYLE);

			MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
			GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
			auto const width{ monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left };
			auto const height{ monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top };
			SetWindowPos(hwnd, nullptr, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		}

		appData->isBorderless = !appData->isBorderless;
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
				delete appData;
				return 0;
			}
			case WM_SIZE: {
				auto const width = LOWORD(lparam);
				auto const height = HIWORD(lparam);
				if (width && height) {
					if (appData->swapChain && appData->backBufRtv && appData->d3dDevice) {
						CreateBackBufRTV(*appData);
					}
					if (appData->immediateContext) {
						D3D11_VIEWPORT const viewport{
							.TopLeftX = 0,
							.TopLeftY = 0,
							.Width = static_cast<FLOAT>(width),
							.Height = static_cast<FLOAT>(height),
							.MinDepth = 0,
							.MaxDepth = 1
						};
						appData->immediateContext->RSSetViewports(1, &viewport);
					}
				}
				return 0;
			}
			case WM_SYSKEYDOWN: {
				if (wparam == VK_RETURN && (lparam & 0x60000000) == 0x20000000) {
					SwitchBorderlessState(hwnd);
					return 0;
				}
				break;
			}
			case WM_SYSCOMMAND: {
				if (wparam == SC_KEYMENU) {
					return 0;
				}
				break;
			}
			case WM_KEYDOWN: {
				if (wparam == 0x4D && !(lparam & 0x40000000)) {
					appData->minimizeOnFocusLoss = !appData->minimizeOnFocusLoss;
					return 0;
				}
				if (wparam == 0x46 && !(lparam & 0x40000000)) {
					SwitchBorderlessState(hwnd);
					return 0;
				}
				break;
			}
			case WM_ACTIVATEAPP: {
				if (appData->isBorderless && appData->minimizeOnFocusLoss && wparam == FALSE) {
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


auto WINAPI wWinMain(_In_ HINSTANCE hInstance, [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance, [[maybe_unused]] _In_ PWSTR pCmdLine, _In_ int nCmdShow) -> int {
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

	if constexpr (START_BORDERLESS) {
		SwitchBorderlessState(hwnd);
	}

	auto* const appData = reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	UINT deviceFlags = 0;
#ifndef NDEBUG
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL constexpr featureLevels[]{ D3D_FEATURE_LEVEL_11_0 };
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, featureLevels, 1, D3D11_SDK_VERSION, appData->d3dDevice.GetAddressOf(), nullptr, appData->immediateContext.GetAddressOf());

#ifndef NDEBUG
	appData->d3dDevice.As<ID3D11Debug>(&appData->debug);
	ComPtr<ID3D11InfoQueue> infoQueue;
	appData->debug.As<ID3D11InfoQueue>(&infoQueue);
	infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
	infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif

#if USE_FENCE
	ComPtr<ID3D11Device5> d3dDevice5;
	appData->d3dDevice.As(&d3dDevice5);

	ComPtr<ID3D11DeviceContext4> context4;
	appData->immediateContext.As(&context4);

	UINT64 fenceValue{ 0 };
	auto const fenceEvent{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };

	ComPtr<ID3D11Fence> fence;
	d3dDevice5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
#endif

	ComPtr<IDXGIDevice2> dxgiDevice2;
	appData->d3dDevice.As(&dxgiDevice2);

	ComPtr<IDXGIAdapter> dxgiAdapter;
	dxgiDevice2->GetAdapter(dxgiAdapter.GetAddressOf());

	ComPtr<IDXGIFactory2> dxgiFactory2;
	dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory2.GetAddressOf()));

	BOOL allowTearing{};

	if (ComPtr<IDXGIFactory5> dxgiFactory5; SUCCEEDED(dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory5.GetAddressOf())))) {
		dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof allowTearing);
	}

	OutputDebugStringW(std::format(L"Tearing in windowed mode is {}supported.\r\n", allowTearing ? L"" : L"not ").c_str());

	if (allowTearing) {
		gSwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		gPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
	}

#if USE_WAITABLE_SWAPCHAIN
	gSwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
#endif

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
		.Flags = gSwapChainFlags
	};
	dxgiFactory2->CreateSwapChainForHwnd(appData->d3dDevice.Get(), hwnd, &swapChainDesc, nullptr, nullptr, appData->swapChain.ReleaseAndGetAddressOf());
	dxgiFactory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES);


#if USE_WAITABLE_SWAPCHAIN
	ComPtr<IDXGISwapChain2> swapChain2;
	appData->swapChain.As(&swapChain2);
	auto const frameLatencyWaitableObject{ swapChain2->GetFrameLatencyWaitableObject() };
	swapChain2->SetMaximumFrameLatency(NUM_FRAMES_IN_FLIGHT);
#else
	dxgiDevice2->SetMaximumFrameLatency(NUM_FRAMES_IN_FLIGHT);
#endif

	CreateBackBufRTV(*appData);

	ComPtr<IDXGIOutput> output;
	appData->swapChain->GetContainingOutput(output.GetAddressOf());
	ComPtr<IDXGIOutput2> output2;
	output.As(&output2);
	if (output2 && output2->SupportsOverlays()) {
		gObjectColor = OVERLAY_SUPPORT_COLOR;
	}
	ComPtr<IDXGIOutput6> output6;
	output.As(&output6);
	if (output6) {
		UINT supportFlags;
		output6->CheckHardwareCompositionSupport(&supportFlags);
		OutputDebugStringW(std::format(L"Fullscreen hardware composition is {}supported.\r\n", supportFlags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_FULLSCREEN ? L"" : L"not ").c_str());
		OutputDebugStringW(std::format(L"Windowed hardware composition is {}supported.\r\n", supportFlags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED ? L"" : L"not ").c_str());
	}

	ComPtr<ID3D11VertexShader> vertexShader;
	appData->d3dDevice->CreateVertexShader(gVsBytes, ARRAYSIZE(gVsBytes), nullptr, vertexShader.GetAddressOf());

	ComPtr<ID3D11PixelShader> pixelShader;
	appData->d3dDevice->CreatePixelShader(gPsBytes, ARRAYSIZE(gPsBytes), nullptr, pixelShader.GetAddressOf());

	D3D11_INPUT_ELEMENT_DESC constexpr inputElementDesc{
		.SemanticName = "POS",
		.SemanticIndex = 0,
		.Format = DXGI_FORMAT_R32G32_FLOAT,
		.InputSlot = 0,
		.AlignedByteOffset = 0,
		.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
		.InstanceDataStepRate = 0
	};
	ComPtr<ID3D11InputLayout> inputLayout;
	appData->d3dDevice->CreateInputLayout(&inputElementDesc, 1, gVsBytes, ARRAYSIZE(gVsBytes), inputLayout.GetAddressOf());

	D3D11_BUFFER_DESC constexpr vertexBufferDesc{
		.ByteWidth = sizeof VERTEX_DATA,
		.Usage = D3D11_USAGE_IMMUTABLE,
		.BindFlags = D3D11_BIND_VERTEX_BUFFER,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};
	D3D11_SUBRESOURCE_DATA constexpr vertexSubresourceData{
		.pSysMem = VERTEX_DATA,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0
	};
	ComPtr<ID3D11Buffer> vertexBuffer;
	appData->d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexSubresourceData, vertexBuffer.GetAddressOf());

	D3D11_BUFFER_DESC constexpr indexBufferDesc{
		.ByteWidth = sizeof INDEX_DATA,
		.Usage = D3D11_USAGE_IMMUTABLE,
		.BindFlags = D3D11_BIND_INDEX_BUFFER,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};
	D3D11_SUBRESOURCE_DATA constexpr subresourceData{
		.pSysMem = INDEX_DATA,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0
	};
	ComPtr<ID3D11Buffer> indexBuffer;
	appData->d3dDevice->CreateBuffer(&indexBufferDesc, &subresourceData, indexBuffer.GetAddressOf());

	D3D11_BUFFER_DESC constexpr colorCBufDesc{
		.ByteWidth = sizeof(ColorCBufData) + 16 - sizeof(ColorCBufData) % 16,
		.Usage = D3D11_USAGE_IMMUTABLE,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};
	D3D11_SUBRESOURCE_DATA const colorCBufData{
		.pSysMem = gObjectColor,
		.SysMemPitch = 0,
		.SysMemSlicePitch = 0
	};
	appData->d3dDevice->CreateBuffer(&colorCBufDesc, &colorCBufData, appData->colorCBuf.GetAddressOf());

	D3D11_BUFFER_DESC constexpr offsetCBufDesc{
		.ByteWidth = sizeof(OffsetCBufData) + 16 - sizeof(OffsetCBufData) % 16,
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};
	appData->d3dDevice->CreateBuffer(&offsetCBufDesc, nullptr, appData->offsetCBuf.GetAddressOf());

	appData->immediateContext->VSSetShader(vertexShader.Get(), nullptr, 0);
	appData->immediateContext->PSSetShader(pixelShader.Get(), nullptr, 0);
	appData->immediateContext->IASetInputLayout(inputLayout.Get());
	appData->immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	appData->immediateContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &VERTEX_STRIDE, &VERTEX_OFFSET);
	appData->immediateContext->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	appData->immediateContext->VSSetConstantBuffers(0, 1, appData->offsetCBuf.GetAddressOf());
	appData->immediateContext->PSSetConstantBuffers(0, 1, appData->colorCBuf.GetAddressOf());
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
	appData->immediateContext->RSSetViewports(1, &viewport);

	auto const startTimePoint{ std::chrono::steady_clock::now() };
	auto lastFrameTimePoint{ startTimePoint };
	auto deltaTime{ std::chrono::nanoseconds::zero() };

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

		D3D11_MAPPED_SUBRESOURCE mappedOffsetCBuf;
		appData->immediateContext->Map(appData->offsetCBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedOffsetCBuf);
		auto* const offsetCBufData{ static_cast<OffsetCBufData*>(mappedOffsetCBuf.pData) };
		auto const totalElapsedTimeSeconds{ std::chrono::duration<float>{ lastFrameTimePoint - startTimePoint }.count() / 6.f };
		offsetCBufData->offsetX = (std::abs(totalElapsedTimeSeconds - static_cast<float>(static_cast<int>(totalElapsedTimeSeconds)) - 0.5f) - 0.25f) * 3.8f;
		appData->immediateContext->Unmap(appData->offsetCBuf.Get(), 0);

		appData->immediateContext->OMSetRenderTargets(1, appData->backBufRtv.GetAddressOf(), nullptr);
		appData->immediateContext->ClearRenderTargetView(appData->backBufRtv.Get(), CLEAR_COLOR);

		for (int i = 0; i < 100; i++) {
			appData->immediateContext->DrawIndexedInstanced(NUM_INDICES, 100, 0, 0, 0);
		}

		appData->swapChain->Present(gSyncInterval, gSyncInterval == 0 ? gPresentFlags : gPresentFlags & ~DXGI_PRESENT_ALLOW_TEARING);

#if USE_FENCE
		auto const currentFenceValue{ fenceValue };
		context4->Signal(fence.Get(), currentFenceValue);
		++fenceValue;

		if (fence->GetCompletedValue() < currentFenceValue) {
			fence->SetEventOnCompletion(currentFenceValue, fenceEvent);
			WaitForSingleObject(fenceEvent, INFINITE);
		}
#endif

		do {
			deltaTime = std::chrono::steady_clock::now() - lastFrameTimePoint;
		}
		while (deltaTime < MIN_FRAME_TIME);

		lastFrameTimePoint += deltaTime;
	}
}