#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#ifdef _NDEBUG
#include "generated/VertexShader.h"
#include "generated/PixelShader.h"
#else
#include "generated/VertexShaderDebug.h"
#include "generated/PixelShaderDebug.h"
#endif

#include <chrono>
#include <format>

using Microsoft::WRL::ComPtr;


struct AppData {
	ComPtr<ID3D11Device> d3dDevice;
	ComPtr<ID3D11DeviceContext> immediateContext;
	ComPtr<ID3D11Debug> debug;
	ComPtr<IDXGISwapChain1> swapChain;
	ComPtr<ID3D11RenderTargetView> backBufRtv;
	ComPtr<ID3D11Buffer> cBuf;
	bool minimizeOnFocusLoss{ false };
	bool isBorderless{ false };
};


struct CBufData {
	float offset[2];
};


namespace {
	float constexpr static VERTEX_DATA[]{
		-1.0f, 1.0f,
		-0.9f, 1.0f,
		-0.9f, -1.0f,
		-1.0f, -1.0f
	};
	unsigned constexpr static INDEX_DATA[]{
		0, 1, 2,
		2, 3, 0
	};
	UINT constexpr NUM_INDICES{ ARRAYSIZE(INDEX_DATA) };
	UINT constexpr VERTEX_STRIDE{ 2 * sizeof(float) };
	UINT constexpr VERTEX_OFFSET{ 0 };
	float constexpr MAX_FPS{ 60 };
	float constexpr MIN_FRAME_TIME{ MAX_FPS <= 0 ? 0 : 1.f / MAX_FPS };
	DWORD constexpr WINDOWED_STYLE{ WS_BORDER | WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_OVERLAPPED | WS_SYSMENU };
	DWORD constexpr BORDERLESS_STYLE{ WS_POPUP };


	UINT gSyncInterval{ 0 };
	UINT gSwapChainFlags{ DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT };
	UINT gPresentFlags{ 0 };


	auto SwitchBorderlessState(HWND const hwnd) -> void {
		auto* const appData{ reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) };
		appData->isBorderless = !appData->isBorderless;

		if (!appData->isBorderless) {
			DWORD constexpr style{ WINDOWED_STYLE | WS_VISIBLE };
			SetWindowLongPtrW(hwnd, GWL_STYLE, style);
			RECT rect{
				.left = 0,
				.top = 0,
				.right = 1280,
				.bottom = 720
			};
			AdjustWindowRect(&rect, style & ~WS_SYSMENU, FALSE);
			SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_FRAMECHANGED);
		}
		else {
			auto const monitor{ MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) };
			MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
			if (GetMonitorInfoW(monitor, &monitorInfo)) {
				SetWindowLongPtrW(hwnd, GWL_STYLE, BORDERLESS_STYLE | WS_VISIBLE);
				SetWindowPos(hwnd, nullptr, 0, 0, monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top, SWP_FRAMECHANGED);
			}
		}
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
					if (appData->swapChain) {
						appData->backBufRtv.Reset();
						appData->swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, gSwapChainFlags);
						ComPtr<ID3D11Texture2D> backBuf;
						appData->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuf.GetAddressOf()));
						appData->d3dDevice->CreateRenderTargetView(backBuf.Get(), nullptr, appData->backBufRtv.GetAddressOf());
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


auto main() -> int {
	WNDCLASSEXW const windowClass{
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = &WindowProc,
		.cbClsExtra = 0,
		.cbWndExtra = 0,
		.hInstance = GetModuleHandleW(nullptr),
		.hIcon = LoadIconW(nullptr, IDI_APPLICATION),
		.hCursor = LoadCursorW(nullptr, IDC_ARROW),
		.hbrBackground = nullptr,
		.lpszMenuName = nullptr,
		.lpszClassName = L"MyWindowClass",
		.hIconSm = LoadIconW(nullptr, IDI_APPLICATION)
	};
	RegisterClassExW(&windowClass);

	auto const hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"MyWindow", WINDOWED_STYLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, windowClass.hInstance, nullptr);
	ShowWindow(hwnd, SW_NORMAL);

	SwitchBorderlessState(hwnd);

	auto* const appData = reinterpret_cast<AppData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	UINT flags = 0;
#ifndef NDEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL constexpr featureLevels[]{ D3D_FEATURE_LEVEL_11_0 };
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels, 1, D3D11_SDK_VERSION, appData->d3dDevice.GetAddressOf(), nullptr, appData->immediateContext.GetAddressOf());

	ComPtr<IDXGIDevice2> dxgiDevice2;
	appData->d3dDevice.As(&dxgiDevice2);

	ComPtr<IDXGIAdapter> dxgiAdapter;
	dxgiDevice2->GetAdapter(dxgiAdapter.GetAddressOf());

	ComPtr<IDXGIFactory2> dxgiFactory2;
	dxgiAdapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(dxgiFactory2.GetAddressOf()));

	BOOL allowTearing{};

	if (ComPtr<IDXGIFactory5> dxgiFactory5; SUCCEEDED(dxgiAdapter->GetParent(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(dxgiFactory5.GetAddressOf())))) {
		dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof allowTearing);
	}

	if (allowTearing) {
		gSwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		gPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
	}
	else {
		MessageBoxW(hwnd, L"Tearing in windowed mode is not supported.", L"Warning", MB_ICONEXCLAMATION | MB_OK);
	}

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

	ComPtr<IDXGISwapChain2> swapChain2;
	appData->swapChain.As(&swapChain2);
	swapChain2->SetMaximumFrameLatency(2);
	auto const frameLatencyWaitable = swapChain2->GetFrameLatencyWaitableObject();

#ifndef NDEBUG
	appData->d3dDevice.As<ID3D11Debug>(&appData->debug);
	ComPtr<ID3D11InfoQueue> infoQueue;
	appData->debug.As<ID3D11InfoQueue>(&infoQueue);
	infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
	infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif

	{
		ComPtr<ID3D11Texture2D> backBuf;
		appData->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuf.GetAddressOf()));
		appData->d3dDevice->CreateRenderTargetView(backBuf.Get(), nullptr, appData->backBufRtv.ReleaseAndGetAddressOf());
	}

	ComPtr<ID3D11VertexShader> vertexShader;
	appData->d3dDevice->CreateVertexShader(gVsBytes, ARRAYSIZE(gVsBytes), nullptr, vertexShader.GetAddressOf());

	ComPtr<ID3D11PixelShader> pixelShader;
	appData->d3dDevice->CreatePixelShader(gPsBytes, ARRAYSIZE(gPsBytes), nullptr, pixelShader.GetAddressOf());

	D3D11_INPUT_ELEMENT_DESC constexpr inputElementDesc{
		.SemanticName = "POS" ,
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

	D3D11_BUFFER_DESC constexpr cBufDesc{
		.ByteWidth = sizeof(CBufData) + 16 - sizeof(CBufData) % 16,
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};
	appData->d3dDevice->CreateBuffer(&cBufDesc, nullptr, appData->cBuf.GetAddressOf());

	auto const begin = std::chrono::steady_clock::now();
	auto lastFrameTime = begin;
	float timeSinceLastFrame = 0;

	while (true) {
		auto const now = std::chrono::steady_clock::now();
		timeSinceLastFrame += std::chrono::duration<float>{now - lastFrameTime}.count();
		lastFrameTime = now;

		if (timeSinceLastFrame < MIN_FRAME_TIME) {
			continue;
		}
		timeSinceLastFrame = 0;

		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				CloseHandle(frameLatencyWaitable);
				return static_cast<int>(msg.wParam);
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		WaitForSingleObjectEx(frameLatencyWaitable, INFINITE, true);

		appData->immediateContext->VSSetShader(vertexShader.Get(), nullptr, 0);
		appData->immediateContext->PSSetShader(pixelShader.Get(), nullptr, 0);
		appData->immediateContext->IASetInputLayout(inputLayout.Get());
		appData->immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		appData->immediateContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &VERTEX_STRIDE, &VERTEX_OFFSET);
		appData->immediateContext->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		appData->immediateContext->VSSetConstantBuffers(0, 1, appData->cBuf.GetAddressOf());

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

		D3D11_MAPPED_SUBRESOURCE mappedSubResource;
		appData->immediateContext->Map(appData->cBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubResource);
		auto* const contants = static_cast<CBufData*>(mappedSubResource.pData);

		auto const timeDiff = std::chrono::duration<float>{ now - begin }.count() / 6.f;
		contants->offset[0] = std::abs(0.5f - (timeDiff - static_cast<int>(timeDiff))) * 3.8f;
		contants->offset[1] = 0;

		appData->immediateContext->Unmap(appData->cBuf.Get(), 0);

		FLOAT constexpr clearColor[]{ 0.2f, 0.2f, 0.2f, 1 };
		appData->immediateContext->ClearRenderTargetView(appData->backBufRtv.Get(), clearColor);
		appData->immediateContext->OMSetRenderTargets(1, appData->backBufRtv.GetAddressOf(), nullptr);
		appData->immediateContext->DrawIndexed(NUM_INDICES, 0, 0);

		appData->swapChain->Present(gSyncInterval, gSyncInterval == 0 ? gPresentFlags : gPresentFlags & ~DXGI_PRESENT_ALLOW_TEARING);
	}
}