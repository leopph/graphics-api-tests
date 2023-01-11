#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <stdexcept>
#include <memory>
#include <type_traits>
#include <array>

#ifndef NDEBUG
#include "shaders/generated/VSBinDebug.h"
#include "shaders/generated/PSBinDebug.h"
#else
#include "shaders/generated/VSBin.h"
#include "shaders/generated/PSBin.h"
#endif

using Microsoft::WRL::ComPtr;


auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
	if (msg == WM_CLOSE) {
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance, [[maybe_unused]] _In_ wchar_t* const lpCmdLine, _In_ int const nShowCmd) -> int {
	try {
		WNDCLASSW const windowClass{
			.lpfnWndProc = &WindowProc,
			.hInstance = hInstance,
			.hCursor = LoadCursorW(nullptr, IDC_ARROW),
			.lpszClassName = L"MyWindowClass",
		};

		if (!RegisterClassW(&windowClass)) {
			throw std::runtime_error{ "Failed to create window class." };
		}

		std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) { if (hwnd) { DestroyWindow(hwnd); } })> const hwnd{ CreateWindowExW(0, windowClass.lpszClassName, L"MyWindow", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInstance, nullptr) };

		if (!hwnd) {
			throw std::runtime_error{ "Failed to create window." };
		}

		ShowWindow(hwnd.get(), nShowCmd);

		ComPtr<ID3D11Device> d3dDevice;
		ComPtr<ID3D11DeviceContext> d3dContext;
		UINT deviceCreationFlags{ 0 };
#ifndef NDEBUG
		deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceCreationFlags, std::array{ D3D_FEATURE_LEVEL_11_0 }.data(), 1, D3D11_SDK_VERSION, d3dDevice.GetAddressOf(), nullptr, d3dContext.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create d3ddevice." };
		}

		ComPtr<IDXGIDevice4> dxgiDevice4;
		if (FAILED(d3dDevice.As(&dxgiDevice4))) {
			throw std::runtime_error{ "Failed to get dxgidevice4." };
		}

		ComPtr<IDXGIAdapter> dxgiAdapter;
		if (FAILED(dxgiDevice4->GetAdapter(dxgiAdapter.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to get dxgiadapter." };
		}

		ComPtr<IDXGIFactory5> dxgiFactory5;
		if (FAILED(dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory5.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to get dxgifactory5." };
		}

		BOOL tearingSupported{ false };

		if (FAILED(dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof tearingSupported))) {
			throw std::runtime_error{ "Failed to check tearing support." };
		}

		if (!tearingSupported) {
			throw std::runtime_error{ "Tearing is not supported." };
		}

		DXGI_SWAP_CHAIN_DESC1 constexpr swapChainDesc{
			.Width = 0,
			.Height = 0,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.Stereo = FALSE,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_NONE,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
			.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		};

		ComPtr<IDXGISwapChain1> swapChain1;
		if (FAILED(dxgiFactory5->CreateSwapChainForHwnd(d3dDevice.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr, swapChain1.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create swapchain." };
		}

		ComPtr<ID3D11Texture2D> backBuf;
		if (FAILED(swapChain1->GetBuffer(0, IID_PPV_ARGS(backBuf.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to get backbuffer." };
		}

		D3D11_RENDER_TARGET_VIEW_DESC constexpr backBufRtvDesc{
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		ComPtr<ID3D11RenderTargetView> backBufRtv;
		if (FAILED(d3dDevice->CreateRenderTargetView(backBuf.Get(), &backBufRtvDesc, backBufRtv.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create backbuffer rtv." };
		}

		ComPtr<ID3D11VertexShader> vertexShader;
		if (FAILED(d3dDevice->CreateVertexShader(gVSBin, ARRAYSIZE(gVSBin), nullptr, vertexShader.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create vertex shader." };
		}

		ComPtr<ID3D11PixelShader> pixelShader;
		if (FAILED(d3dDevice->CreatePixelShader(gPSBin, ARRAYSIZE(gPSBin), nullptr, pixelShader.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create pixel shader." };
		}

		D3D11_INPUT_ELEMENT_DESC constexpr inputElementDesc{
			.SemanticName = "VERTEXPOS",
			.SemanticIndex = 0,
			.Format = DXGI_FORMAT_R32G32_FLOAT,
			.InputSlot = 0,
			.AlignedByteOffset = 0,
			.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
			.InstanceDataStepRate = 0
		};

		ComPtr<ID3D11InputLayout> inputLayout;
		if (FAILED(d3dDevice->CreateInputLayout(&inputElementDesc, 1, gVSBin, ARRAYSIZE(gVSBin), inputLayout.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create input layout." };
		}

		using Vec2 = float[2];
		Vec2 constexpr static vertices[3]{ { 0, 0.5f }, { 0.5f, -0.5f }, { -0.5f, -0.5f } };

		D3D11_BUFFER_DESC constexpr vertBufDesc{
			.ByteWidth = sizeof vertices,
			.Usage = D3D11_USAGE_IMMUTABLE,
			.BindFlags = D3D11_BIND_VERTEX_BUFFER,
			.CPUAccessFlags = 0,
			.MiscFlags = 0,
			.StructureByteStride = 0
		};

		D3D11_SUBRESOURCE_DATA constexpr vertBufInitData{
			.pSysMem = &vertices,
			.SysMemPitch = 0,
			.SysMemSlicePitch = 0
		};

		ComPtr<ID3D11Buffer> vertexBuffer;
		if (FAILED(d3dDevice->CreateBuffer(&vertBufDesc, &vertBufInitData, vertexBuffer.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create vertex buffer." };
		}

		MSG msg{};

		while (msg.message != WM_QUIT) {
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			d3dContext->IASetInputLayout(inputLayout.Get());
			d3dContext->VSSetShader(vertexShader.Get(), nullptr, 0);
			d3dContext->PSSetShader(pixelShader.Get(), nullptr, 0);
			d3dContext->OMSetRenderTargets(1, backBufRtv.GetAddressOf(), nullptr);

			RECT clientRect;
			GetClientRect(hwnd.get(), &clientRect);

			D3D11_VIEWPORT const viewPort{
				.TopLeftX = 0,
				.TopLeftY = 0,
				.Width = static_cast<FLOAT>(clientRect.right - clientRect.left),
				.Height = static_cast<FLOAT>(clientRect.bottom - clientRect.top),
				.MinDepth = 0,
				.MaxDepth = 1
			};
			d3dContext->RSSetViewports(1, &viewPort);

			float constexpr clearColor[]{ 0.2f, 0.3f, 0.3f, 1.f };
			d3dContext->ClearRenderTargetView(backBufRtv.Get(), clearColor);
			d3dContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			UINT stride{ sizeof(Vec2) }, offset{ 0 };
			d3dContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
			d3dContext->DrawInstanced(3, 1, 0, 0);

			if (FAILED(swapChain1->Present(0, DXGI_PRESENT_ALLOW_TEARING))) {
				throw std::runtime_error{ "Failed to present." };
			}
		}

		return static_cast<int>(msg.wParam);
	}
	catch (std::exception const& ex) {
		MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR);
		return -1;
	}
}
