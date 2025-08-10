#include "someshit.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_DESTROY: PostQuitMessage(0); return 0;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) { DestroyWindow(hWnd); }
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void InitD3D12(HWND hWnd, UINT width, UINT height) {
	#if defined(_DEBUG)
	if (ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
		dbg->EnableDebugLayer();
	#endif

	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&g_factory)))) throw std::runtime_error("DXGI factory failed");

	ComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; g_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
		if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
	}
	if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_device))))
		throw std::runtime_error("D3D12 device failed");

	D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	g_device->CreateCommandQueue(&q, IID_PPV_ARGS(&g_cmdQueue));

	DXGI_SWAP_CHAIN_DESC1 sc{};
	sc.BufferCount = kFrameCount;
	sc.Width = width; sc.Height = height;
	sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> sc1;
	g_factory->CreateSwapChainForHwnd(g_cmdQueue.Get(), hWnd, &sc, nullptr, nullptr, &sc1);
	sc1.As(&g_swapChain);
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = kFrameCount;
	g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap));
	g_rtvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < kFrameCount; ++i) {
		g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i]));
		CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtvStart, i, g_rtvInc);
		g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, h);
	}

	for (UINT i = 0; i < kFrameCount; ++i)
		g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i]));

	g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[g_frameIndex].Get(), nullptr, IID_PPV_ARGS(&g_cmdList));
	g_cmdList->Close();

	// Fence
	g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
	g_fenceValue = 1;
	g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
};

void RenderFrame() {
	g_alloc[g_frameIndex]->Reset();
	g_cmdList->Reset(g_alloc[g_frameIndex].Get(), nullptr);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_cmdList->ResourceBarrier(1, &barrier);

	g_cmdList->Close();
	ID3D12CommandList* list[] = { g_cmdList.Get() };
	g_cmdQueue->ExecuteCommandLists(1, list);

	g_swapChain->Present(1, 0);

	const UINT64 fenceToWait = g_fenceValue++;
	g_cmdQueue->Signal(g_fence.Get(), fenceToWait);
	if (g_fence->GetCompletedValue() < fenceToWait) {
		g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent);
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

void WaitForGPU() {
	const UINT64 fenceToWait = g_fenceValue++;
	g_cmdQueue->Signal(g_fence.Get(), fenceToWait);
	g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent);
	WaitForSingleObject(g_fenceEvent, INFINITE);
};

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
	const wchar_t* kClassName = L"BitchEngine";
	wchar_t window_name[25] = L"bitchengine FPS: ";

	WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClassName;
	RegisterClassExW(&wc);

	RECT rc{ 0,0,1280,720 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

	g_hWnd = CreateWindowExW(
		0, kClassName, window_name,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hInst, nullptr);

	ShowWindow(g_hWnd, nCmdShow);
	InitD3D12(g_hWnd, 1280, 720);

	LARGE_INTEGER freq{};
	QueryPerformanceFrequency(&freq);

	LARGE_INTEGER prev{};
	QueryPerformanceCounter(&prev);

	double accTitle = 0.0;
	int frameCounter = 0;
	double fpsShown = 0.0;

	MSG msg{};
	for (;;) {
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) return (int)msg.wParam;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		double dt = double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart);
		prev = now;

		accTitle += dt;
		frameCounter++;

		if (accTitle >= 0.5) {
			fpsShown = frameCounter / accTitle;
			frameCounter = 0;
			accTitle = 0.0;

			std::wstring title = std::format(L"bitchengine FPS: {}", (int)std::round(fpsShown));
			SetWindowTextW(g_hWnd, title.c_str());
		}
		RenderFrame();
		Sleep(1);
	}
}