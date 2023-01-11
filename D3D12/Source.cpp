#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3dx12.h>

#include <stdexcept>
#include <format>
#include <string_view>
#include <cstring>
#include <vector>
#include <memory>
#include <type_traits>

#ifndef NDEBUG
#include "shaders/generated/VSBinDebug.h"
#include "shaders/generated/PSBinDebug.h"
#else
#include "shaders/generated/VSBin.h"
#include "shaders/generated/PSBin.h"
#endif

using Microsoft::WRL::ComPtr;


auto CALLBACK WindowProc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) -> LRESULT {
	switch (msg) {
		case WM_CLOSE: {
			PostQuitMessage(0);
			return 0;
		}
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

auto WINAPI wWinMain(_In_ HINSTANCE const hInstance, [[maybe_unused]] _In_opt_ HINSTANCE const hPrevInstance, [[maybe_unused]] _In_ wchar_t* const lpCmdLine, _In_ int const nShowCmd) -> int {
	try {
		WNDCLASSW const windowClass{
			.lpfnWndProc = &WindowProc,
			.hInstance = hInstance,
			.hCursor = LoadCursorW(nullptr, IDC_ARROW),
			.lpszClassName = L"MyClass"
		};

		if (!RegisterClassW(&windowClass)) {
			throw std::runtime_error{ "Failed to register window class." };
		}

		std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) { if (hwnd) DestroyWindow(hwnd); })> const hwnd{ CreateWindowExW(0, windowClass.lpszClassName, L"MyWindow", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInstance, nullptr) };

		if (!hwnd) {
			throw std::runtime_error{ "Failed to create window." };
		}

		ShowWindow(hwnd.get(), nShowCmd);

#ifndef NDEBUG
		ComPtr<ID3D12Debug> debug;
		if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to get debug interface." };
		}
		debug->EnableDebugLayer();
#endif

		ComPtr<IDXGIFactory1> factory1;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory1.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create dxgi factory 1." };
		}

		ComPtr<ID3D12Device> d3dDevice;
		if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(d3dDevice.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create d3d device." };
		}

		D3D12_COMMAND_QUEUE_DESC constexpr commandQueueDesc{
			.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
			.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
			.NodeMask = 0
		};

		ComPtr<ID3D12CommandQueue> commandQueue;
		if (FAILED(d3dDevice->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create command queue." };
		}

		ComPtr<IDXGIFactory5> factory5;
		if (FAILED(factory1.As(&factory5))) {
			throw std::runtime_error{ "Failed to cast dxgifactory1 to dxgifactory5" };
		}

		BOOL isTearingSupported{ false };

		if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &isTearingSupported, sizeof isTearingSupported))) {
			throw std::runtime_error{ "Failed to check tearing support." };
		}

		if (!isTearingSupported) {
			throw std::runtime_error{ "Tearing is unsupported." };
		}

		DXGI_SWAP_CHAIN_DESC1 constexpr swapChainDesc{
			.Width = 0,
			.Height = 0,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.Stereo = FALSE,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_NONE,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
			.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		};

		ComPtr<IDXGISwapChain1> swapChain1;
		if (FAILED(factory5->CreateSwapChainForHwnd(commandQueue.Get(), hwnd.get(), &swapChainDesc, nullptr, nullptr, swapChain1.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create swapchain." };
		}

		ComPtr<IDXGISwapChain3> swapChain3;
		if (FAILED(swapChain1.As(&swapChain3))) {
			throw std::runtime_error{ "Failed to cast dxgiswapchain1 to dxgiswapchain3." };
		}

		auto backBufInd{ swapChain3->GetCurrentBackBufferIndex() };

		D3D12_DESCRIPTOR_HEAP_DESC constexpr rtvHeapDesc{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = 2,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask = 0
		};

		ComPtr<ID3D12DescriptorHeap> rtvHeap;
		if (FAILED(d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create rtv heap." };
		}

		auto const rtvHeapIncrementSize{ d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) };

		auto const rtvHeapCpuDescHandle{ rtvHeap->GetCPUDescriptorHandleForHeapStart() };

		std::vector<ComPtr<ID3D12Resource>> backBuffers;

		for (UINT i = 0; i < swapChainDesc.BufferCount; i++) {
			auto& backBuf{ backBuffers.emplace_back() };

			if (FAILED(swapChain3->GetBuffer(i, IID_PPV_ARGS(backBuf.GetAddressOf())))) {
				throw std::runtime_error{ std::format("Failed to get backbuffer {} from swapchain.", i) };
			}

			D3D12_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
				.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
				.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MipSlice = 0,
					.PlaneSlice = 0
				}
			};

			d3dDevice->CreateRenderTargetView(backBuf.Get(), &rtvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE{ rtvHeapCpuDescHandle, static_cast<INT>(i), rtvHeapIncrementSize });
		}

		ComPtr<ID3D12CommandAllocator> commandAllocator;
		if (FAILED(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create command allocator." };
		}

		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{ 0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
		ComPtr<ID3DBlob> serializedRootSignature;
		ComPtr<ID3DBlob> rootSignatureSerializerError;
		if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, serializedRootSignature.GetAddressOf(), rootSignatureSerializerError.GetAddressOf()))) {
			throw std::runtime_error{ std::format("Failed to serialize root signature: {}.", std::string_view{ static_cast<char const*>(rootSignatureSerializerError->GetBufferPointer()), rootSignatureSerializerError->GetBufferSize() }) };
		}

		ComPtr<ID3D12RootSignature> rootSignature;
		if (FAILED(d3dDevice->CreateRootSignature(0, serializedRootSignature->GetBufferPointer(), serializedRootSignature->GetBufferSize(), IID_PPV_ARGS(rootSignature.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create root signature." };
		}

		D3D12_INPUT_ELEMENT_DESC constexpr inputElementDesc{
			.SemanticName = "VERTEXPOS",
			.SemanticIndex = 0,
			.Format = DXGI_FORMAT_R32G32_FLOAT,
			.InputSlot = 0,
			.AlignedByteOffset = 0,
			.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			.InstanceDataStepRate = 0
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC const psoDesc{
			.pRootSignature = rootSignature.Get(),
			.VS = { .pShaderBytecode = gVSBin, .BytecodeLength = ARRAYSIZE(gVSBin) },
			.PS = { .pShaderBytecode = gPSBin, .BytecodeLength = ARRAYSIZE(gPSBin) },
			.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT },
			.SampleMask = UINT_MAX,
			.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT },
			.InputLayout = { .pInputElementDescs = &inputElementDesc, .NumElements = 1 },
			.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
			.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			.NumRenderTargets = 1,
			.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.NodeMask = 0,
			.CachedPSO = { nullptr, 0 },
			.Flags = D3D12_PIPELINE_STATE_FLAG_NONE
		};

		ComPtr<ID3D12PipelineState> pso;
		if (FAILED(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create pso." };
		}

		ComPtr<ID3D12GraphicsCommandList> commandList;
		if (FAILED(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), pso.Get(), IID_PPV_ARGS(commandList.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create command list." };
		}

		if (FAILED(commandList->Close())) {
			throw std::runtime_error{ "Failed to close command list." };
		}

		using Vec2 = float[2];
		Vec2 constexpr vertices[3]{ { 0, 0.5f }, { 0.5f, -0.5f }, { -0.5f, -0.5f } };

		CD3DX12_HEAP_PROPERTIES const vertBufHeapProps{ D3D12_HEAP_TYPE_UPLOAD };
		auto const verBufResDesc{ CD3DX12_RESOURCE_DESC::Buffer(sizeof vertices) };

		ComPtr<ID3D12Resource> vertexBuffer;
		if (FAILED(d3dDevice->CreateCommittedResource(&vertBufHeapProps, D3D12_HEAP_FLAG_NONE, &verBufResDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(vertexBuffer.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create vertex buffer." };
		}

		D3D12_RANGE constexpr range{ 0, 0 };
		void* mappedVertexBuffer;

		if (FAILED(vertexBuffer->Map(0, &range, &mappedVertexBuffer))) {
			throw std::runtime_error{ "Failed to map vertex buffer." };
		}

		std::memcpy(mappedVertexBuffer, vertices, sizeof vertices);
		vertexBuffer->Unmap(0, nullptr);

		D3D12_VERTEX_BUFFER_VIEW const vertexBufferView{
			.BufferLocation = vertexBuffer->GetGPUVirtualAddress(),
			.SizeInBytes = sizeof vertices,
			.StrideInBytes = sizeof(Vec2)
		};

		ComPtr<ID3D12Fence> fence;
		if (FAILED(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to create fence." };
		}

		UINT64 fenceValue{ 1 };
		std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype([](HANDLE const handle) { if (handle) CloseHandle(handle); })> const fenceEvent{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
		if (!fenceEvent) {
			throw std::runtime_error{ "Failed to create fence event." };
		}

		auto const waitForGpu{
			[&] {
				auto const currentFenceValue{ fenceValue };

				if (FAILED(commandQueue->Signal(fence.Get(), currentFenceValue))) {
					throw std::runtime_error{ "Failed to signal gpu fence." };
				}

				++fenceValue;

				if (fence->GetCompletedValue() < currentFenceValue) {
					if (FAILED(fence->SetEventOnCompletion(currentFenceValue, fenceEvent.get()))) {
						throw std::runtime_error{ "Failed to set on-completion event for gpu fence." };
					}
					WaitForSingleObject(fenceEvent.get(), INFINITE);
				}

				backBufInd = swapChain3->GetCurrentBackBufferIndex();
			}
		};

		waitForGpu();

		while (true) {
			MSG msg;

			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) {
					waitForGpu();
					return static_cast<int>(msg.wParam);
				}

				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			if (FAILED(commandAllocator->Reset())) {
				throw std::runtime_error{ "Failed to reset command allocator." };
			}

			if (FAILED(commandList->Reset(commandAllocator.Get(), pso.Get()))) {
				throw std::runtime_error{ "Failed to reset command list." };
			}

			commandList->SetGraphicsRootSignature(rootSignature.Get());

			RECT clientRect;
			GetClientRect(hwnd.get(), &clientRect);

			D3D12_VIEWPORT const viewPort{
				.TopLeftX = 0,
				.TopLeftY = 0,
				.Width = static_cast<FLOAT>(clientRect.right - clientRect.left),
				.Height = static_cast<FLOAT>(clientRect.bottom - clientRect.top),
				.MinDepth = 0,
				.MaxDepth = 1
			};
			commandList->RSSetViewports(1, &viewPort);

			D3D12_RECT const scissorRect{
				.left = 0,
				.top = 0,
				.right = static_cast<LONG>(viewPort.Width),
				.bottom = static_cast<LONG>(viewPort.Height)
			};
			commandList->RSSetScissorRects(1, &scissorRect);

			auto const rtvBarrier{ CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufInd].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET) };
			commandList->ResourceBarrier(1, &rtvBarrier);

			CD3DX12_CPU_DESCRIPTOR_HANDLE const rtvHandle{ rtvHeapCpuDescHandle, static_cast<INT>(backBufInd), rtvHeapIncrementSize };
			commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

			float constexpr clearColor[]{ 0.2f, 0.3f, 0.3f, 1.f };
			commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->DrawInstanced(3, 1, 0, 0);

			auto const presentBarrier{ CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[backBufInd].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT) };
			commandList->ResourceBarrier(1, &presentBarrier);

			if (FAILED(commandList->Close())) {
				throw std::runtime_error{ "Failed to close command list in render loop." };
			}

			ID3D12CommandList* const commandLists{ commandList.Get() };
			commandQueue->ExecuteCommandLists(1, &commandLists);

			if (FAILED(swapChain1->Present(0, DXGI_PRESENT_ALLOW_TEARING))) {
				throw std::runtime_error{ "Failed to present." };
			}

			waitForGpu();
		}
	}
	catch (std::exception const& ex) {
		MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR);
		return -1;
	}
}
