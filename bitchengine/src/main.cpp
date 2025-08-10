#include "someshit.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

	// Back buffers + RTV
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

	// === DSV heap ===
	D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
	dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvDesc.NumDescriptors = 1;
	HR(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)));

	// === Depth ресурс ===
	D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		g_depthFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	D3D12_CLEAR_VALUE depthClear{}; depthClear.Format = g_depthFormat; depthClear.DepthStencil.Depth = 1.0f; depthClear.DepthStencil.Stencil = 0;

	HR(g_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClear,
		IID_PPV_ARGS(&g_depthBuffer)));

	// DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = g_depthFormat; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsv, g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	// Viewport/Scissor
	g_viewport = { 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f };
	g_scissor = { 0, 0, (LONG)width, (LONG)height };

	for (UINT i = 0; i < kFrameCount; ++i)
		g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i]));

	g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[g_frameIndex].Get(), nullptr, IID_PPV_ARGS(&g_cmdList));
	g_cmdList->Close();

	// === Данные куба: позиция (float3) + цвет (float3) ===
	struct Vertex { float px, py, pz; float r, g, b; };
	static const Vertex kVertices[] = {
		// фронт
		{-1,-1, 1, 1,0,0}, {1,-1, 1, 0,1,0}, {1, 1, 1, 0,0,1}, {-1, 1, 1, 1,1,0},
		// бэк
		{-1,-1,-1, 1,0,1}, {1,-1,-1, 0,1,1}, {1, 1,-1, 1,1,1}, {-1, 1,-1, 0.2f,0.2f,0.2f},
	};
	static const uint16_t kIndices[] = {
		// фронт
		0,1,2, 0,2,3,
		// прав
		1,5,6, 1,6,2,
		// зад
		5,4,7, 5,7,6,
		// лев
		4,0,3, 4,3,7,
		// низ
		4,5,1, 4,1,0,
		// верх
		3,2,6, 3,6,7,
	};
	g_indexCount = _countof(kIndices);

	// Вспомогатель: создадим upload‑ресурсы и сразу же DEFAULT‑буферы
	auto CreateDefaultBuffer = [&](const void* data, size_t bytes, ComPtr<ID3D12Resource>& defaultBuf, ComPtr<ID3D12Resource>& uploadBuf) {
		HR(g_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bytes), D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr, IID_PPV_ARGS(&defaultBuf)));
		HR(g_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bytes), D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&uploadBuf)));

		// Копируем в upload и командой переносим в default
		void* mapped = nullptr; CD3DX12_RANGE r(0, 0);
		uploadBuf->Map(0, &r, &mapped); memcpy(mapped, data, bytes); uploadBuf->Unmap(0, nullptr);

		g_cmdList->Reset(g_alloc[g_frameIndex].Get(), nullptr);
		g_cmdList->CopyBufferRegion(defaultBuf.Get(), 0, uploadBuf.Get(), 0, bytes);
		auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		g_cmdList->ResourceBarrier(1, &toRead);
		g_cmdList->Close();
		ID3D12CommandList* lists[] = { g_cmdList.Get() };
		g_cmdQueue->ExecuteCommandLists(1, lists);
		WaitForGPU();
		};

	ComPtr<ID3D12Resource> vbUpload, ibUpload;
	CreateDefaultBuffer(kVertices, sizeof(kVertices), g_vb, vbUpload);
	CreateDefaultBuffer(kIndices, sizeof(kIndices), g_ib, ibUpload);

	// Views
	g_vbv = { g_vb->GetGPUVirtualAddress(), (UINT)sizeof(kVertices), (UINT)sizeof(Vertex) };
	g_ibv = { g_ib->GetGPUVirtualAddress(), (UINT)sizeof(kIndices), DXGI_FORMAT_R16_UINT };

	// === Root Signature (пустая: без ресурсов, только статические настройки) ===
	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> rsBlob, rsErr;
	HR(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr));
	HR(g_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&g_rootSig)));

	// === HLSL (встроенные строки для простоты) ===
	const char* kVS = R"(
struct VSIn { float3 pos: POSITION; float3 col: COLOR; };
struct VSOut{ float4 svpos: SV_Position; float3 col: COLOR; };
cbuffer CB : register(b0) { float4x4 MVP; } // пока не используем, оставим на будущее
VSOut main(VSIn i){ VSOut o; o.svpos = float4(i.pos, 1); o.col=i.col; return o; }
)";
	const char* kPS = R"(
struct PSIn{ float4 svpos: SV_Position; float3 col: COLOR; };
float4 main(PSIn i): SV_Target { return float4(i.col, 1); }
)";

	ComPtr<ID3DBlob> vs, ps, err;
	HR(D3DCompile(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_5_1", 0, 0, &vs, &err));
	HR(D3DCompile(kPS, strlen(kPS), nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0, &ps, &err));

	// === Input Layout ===
	D3D12_INPUT_ELEMENT_DESC il[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// === PSO ===
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = g_rootSig.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.InputLayout = { il, _countof(il) };
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1; pso.RTVFormats[0] = g_backBufferFormat;
	pso.DSVFormat = g_depthFormat;
	pso.SampleDesc.Count = 1;
	HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_pso)));

	// Fence
	g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
	g_fenceValue = 1;
	g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
};

void RenderFrame()
{
	g_alloc[g_frameIndex]->Reset();
	g_cmdList->Reset(g_alloc[g_frameIndex].Get(), g_pso.Get());

	// Барьер backbuffer: PRESENT -> RENDER_TARGET
	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_cmdList->ResourceBarrier(1, &toRT);

	// handles RTV/DSV
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(g_rtvHeap->GetCPUDescriptorHandleForHeapStart(), g_frameIndex, g_rtvInc);
	auto dsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();

	// viewport/scissor
	g_cmdList->RSSetViewports(1, &g_viewport);
	g_cmdList->RSSetScissorRects(1, &g_scissor);

	// очистки
	const FLOAT clear[4] = { 0.10f, 0.18f, 0.35f, 1.0f };
	g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	g_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);
	g_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// IA: буферы и топология
	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_cmdList->IASetVertexBuffers(0, 1, &g_vbv);
	g_cmdList->IASetIndexBuffer(&g_ibv);

	// шейдерные ресурсы пока не нужны (RS без параметров)
	g_cmdList->SetGraphicsRootSignature(g_rootSig.Get());

	// draw
	g_cmdList->DrawIndexedInstanced(g_indexCount, 1, 0, 0, 0);

	// Барьер: RENDER_TARGET -> PRESENT
	auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_cmdList->ResourceBarrier(1, &toPresent);

	g_cmdList->Close();
	ID3D12CommandList* lists[] = { g_cmdList.Get() };
	g_cmdQueue->ExecuteCommandLists(1, lists);

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