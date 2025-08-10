#include "someshit.h"
#include "textures.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

void CreateCB()
{
	const UINT cbSize = (sizeof(VSConstants) + 255) & ~255u;
	CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC   desc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
	HR(g_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cb)));
	HR(g_cb->Map(0, nullptr, reinterpret_cast<void**>(&g_cbPtr)));
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

	HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAlloc)));
	HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAlloc.Get(), nullptr,
		IID_PPV_ARGS(&g_uploadList)));
	HR(g_uploadList->Close());

	HR(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
	g_fenceValue = 1; // начальное значение

	g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!g_fenceEvent) {
		HR(HRESULT_FROM_WIN32(GetLastError())); // пробросим как HRESULT
	}

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

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	HR(g_device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClear,
		IID_PPV_ARGS(&g_depthBuffer)));

	// DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = g_depthFormat; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsv, g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_DESCRIPTOR_HEAP_DESC h{};
	h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	h.NumDescriptors = 1;
	h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	HR(g_device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&g_srvHeap)));

	// Viewport/Scissor
	g_viewport = { 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f };
	g_scissor = { 0, 0, (LONG)width, (LONG)height };

	for (UINT i = 0; i < kFrameCount; ++i)
		g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i]));

	HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		g_alloc[0].Get(), nullptr, IID_PPV_ARGS(&g_cmdList)));
	HR(g_cmdList->Close());

	// === Данные куба: позиция (float3) + цвет (float3) ===
	CreateCB();

	struct Vertex { float px, py, pz; float r, g, b; float u, v; }; // + uv

	static const Vertex kVertices[] = {
		// +Z (front)
		{-1,-1, 1, 1,0,0,  0,1}, { 1,-1, 1, 0,1,0,  1,1}, { 1, 1, 1, 0,0,1,  1,0}, { -1, 1, 1, 1,1,0,  0,0},
		// -Z (back)
		{ 1,-1,-1, 1,0,1,  0,1}, {-1,-1,-1, 0,1,1,  1,1}, {-1, 1,-1, 1,1,1,  1,0}, {  1, 1,-1, 0.3f,0.3f,0.3f,  0,0},
		// +X (right)
		{ 1,-1, 1, 1,0,0,  0,1}, { 1,-1,-1, 0,1,0,  1,1}, { 1, 1,-1, 0,0,1,  1,0}, {  1, 1, 1, 1,1,0,  0,0},
		// -X (left)
		{-1,-1,-1, 1,0,1,  0,1}, {-1,-1, 1, 0,1,1,  1,1}, {-1, 1, 1, 1,1,1,  1,0}, { -1, 1,-1, 0.3f,0.3f,0.3f,  0,0},
		// +Y (top)
		{-1, 1, 1, 1,0,0,  0,1}, { 1, 1, 1, 0,1,0,  1,1}, { 1, 1,-1, 0,0,1,  1,0}, { -1, 1,-1, 1,1,0,  0,0},
		// -Y (bottom)
		{-1,-1,-1, 1,0,1,  0,1}, { 1,-1,-1, 0,1,1,  1,1}, { 1,-1, 1, 1,1,1,  1,0}, { -1,-1, 1, 0.3f,0.3f,0.3f,  0,0},
	};

	static const uint16_t kIndices[] = {
		// front
		0,1,2, 0,2,3,
		// back
		4,5,6, 4,6,7,
		// right
		8,9,10, 8,10,11,
		// left
		12,13,14, 12,14,15,
		// top
		16,17,18, 16,18,19,
		// bottom
		20,21,22, 20,22,23,
	};

	g_indexCount = (UINT)_countof(kIndices);

	auto CreateDefaultBuffer = [&](ID3D12GraphicsCommandList* cmd,
		const void* data, size_t bytes,
		ComPtr<ID3D12Resource>& defaultBuf,
		ComPtr<ID3D12Resource>& uploadBuf,
		D3D12_RESOURCE_STATES finalState)
		{
			CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);

			// DEFAULT (GPU) — стартуем из COMMON (без ворнинга)
			CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
			HR(g_device->CreateCommittedResource(
				&heapDefault, D3D12_HEAP_FLAG_NONE,
				&bufDesc, D3D12_RESOURCE_STATE_COMMON,
				nullptr, IID_PPV_ARGS(defaultBuf.ReleaseAndGetAddressOf())));

			// UPLOAD (CPU->GPU)
			CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
			HR(g_device->CreateCommittedResource(
				&heapUpload, D3D12_HEAP_FLAG_NONE,
				&bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr, IID_PPV_ARGS(uploadBuf.ReleaseAndGetAddressOf())));

			// map & copy
			void* mapped = nullptr; CD3DX12_RANGE noRead(0, 0);
			HR(uploadBuf->Map(0, &noRead, &mapped));
			std::memcpy(mapped, data, bytes);
			uploadBuf->Unmap(0, nullptr);

			// COMMON -> COPY_DEST
			auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
				defaultBuf.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			cmd->ResourceBarrier(1, &toCopy);

			// Copy
			cmd->CopyBufferRegion(defaultBuf.Get(), 0, uploadBuf.Get(), 0, bytes);

			// COPY_DEST -> finalState (VB/IB)
			auto toFinal = CD3DX12_RESOURCE_BARRIER::Transition(
				defaultBuf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
			cmd->ResourceBarrier(1, &toFinal);
		};

	// === 2) Открываем upload-список ОДИН РАЗ под всё копирование (VB/IB + текстура) ===
	HR(g_uploadAlloc->Reset());
	HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));

	// ---- VB/IB ----
	ComPtr<ID3D12Resource> vbUpload, ibUpload;
	CreateDefaultBuffer(g_uploadList.Get(),
		kVertices, sizeof(kVertices),
		g_vb, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	CreateDefaultBuffer(g_uploadList.Get(),
		kIndices, sizeof(kIndices),
		g_ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);

	ScratchImage img = LoadTextureFile(L"assets\\textures\\negrosuke.png"); // твоя функция
	const TexMetadata& meta = img.GetMetadata();

	// ресурс текстуры (DEFAULT) — старт из COMMON
	ComPtr<ID3D12Resource> texResource;
	{
		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			meta.format, meta.width, (UINT)meta.height,
			(UINT16)meta.arraySize, (UINT16)meta.mipLevels);

		HR(g_device->CreateCommittedResource(
			&heapDefault, D3D12_HEAP_FLAG_NONE,
			&texDesc, D3D12_RESOURCE_STATE_COMMON,
			nullptr, IID_PPV_ARGS(&texResource)));
	}

	// upload ресурс под все мипы
	ComPtr<ID3D12Resource> texUpload;
	{
		UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, (UINT)img.GetImageCount());
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
		auto upDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		HR(g_device->CreateCommittedResource(
			&heapUpload, D3D12_HEAP_FLAG_NONE,
			&upDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&texUpload)));
	}

	// COMMON -> COPY_DEST (до UpdateSubresources)
	{
		auto toCopyTex = CD3DX12_RESOURCE_BARRIER::Transition(
			texResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		g_uploadList->ResourceBarrier(1, &toCopyTex);
	}

	// формируем подресурсы и копируем
	{
		std::vector<D3D12_SUBRESOURCE_DATA> subs;
		PrepareUpload(g_device.Get(), img.GetImages(), img.GetImageCount(), meta, subs);

		UpdateSubresources(g_uploadList.Get(), texResource.Get(), texUpload.Get(),
			0, 0, (UINT)subs.size(), subs.data());
	}

	// COPY_DEST -> PIXEL_SHADER_RESOURCE
	{
		auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
			texResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		g_uploadList->ResourceBarrier(1, &toSRV);
	}

	// закрываем upload‑список, выполняем и ждём
	HR(g_uploadList->Close());
	{
		ID3D12CommandList* lists[] = { g_uploadList.Get() };
		g_cmdQueue->ExecuteCommandLists(1, lists);
	}
	WaitForGPU();

	// === 4) создаём SRV (t0) в g_srvHeap ===
	D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
	sd.Format = meta.format;
	sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sd.Texture2D.MipLevels = (UINT)meta.mipLevels;

	auto cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
	g_device->CreateShaderResourceView(texResource.Get(), &sd, cpu);

	// (если хочешь хранить текстуру глобально)
	g_tex = texResource;

	// === 5) IA views для рендера ===
	g_vbv.BufferLocation = g_vb->GetGPUVirtualAddress();
	g_vbv.StrideInBytes = sizeof(Vertex);
	g_vbv.SizeInBytes = (UINT)sizeof(kVertices);

	g_ibv.BufferLocation = g_ib->GetGPUVirtualAddress();
	g_ibv.Format = DXGI_FORMAT_R16_UINT;
	g_ibv.SizeInBytes = (UINT)sizeof(kIndices);

	// Descriptor range для SRV (t0..t0)
	D3D12_DESCRIPTOR_RANGE range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 0; // t0
	range.RegisterSpace = 0;
	range.OffsetInDescriptorsFromTableStart = 0;

	// Параметр 0: CBV b0 (VS)
	D3D12_ROOT_PARAMETER rp[2]{};
	rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rp[0].Descriptor.ShaderRegister = 0; // b0
	rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// Параметр 1: таблица SRV (t0) для PS
	rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[1].DescriptorTable.NumDescriptorRanges = 1;
	rp[1].DescriptorTable.pDescriptorRanges = &range;
	rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Статический сэмплер s0
	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp.ShaderRegister = 0; // s0
	samp.RegisterSpace = 0;
	samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Сборка RS
	D3D12_ROOT_SIGNATURE_DESC rs{};
	rs.NumParameters = _countof(rp);
	rs.pParameters = rp;
	rs.NumStaticSamplers = 1;
	rs.pStaticSamplers = &samp;
	rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	ComPtr<ID3DBlob> sig, err_rs;
	HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err_rs));
	HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&g_rootSig)));

	auto vs = CompileShaderFromFile(L"shaders\\cube_vs.hlsl", "main", "vs_5_1");
	auto ps = CompileShaderFromFile(L"shaders\\cube_ps.hlsl", "main", "ps_5_1");

	// === Input Layout ===
	D3D12_INPUT_ELEMENT_DESC inputElems[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = g_rootSig.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.SampleMask = UINT_MAX;
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // ВАЖНО
	pso.InputLayout = { inputElems, _countof(inputElems) };
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = g_backBufferFormat;
	pso.DSVFormat = g_depthFormat;   // ВАЖНО
	pso.SampleDesc = { 1, 0 };

	auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rast.CullMode = D3D12_CULL_MODE_FRONT;
	rast.FrontCounterClockwise = TRUE; // если индексы CCW
	pso.RasterizerState = rast;

	HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_pso)));

	XMVECTOR eye = XMVectorSet(0, 0, -5, 1), at = XMVectorSet(0, 0, 0, 1), up = XMVectorSet(0, 1, 0, 0);
	XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
	float aspect = float(width) / float(height);
	XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 100.0f);
	XMStoreFloat4x4(&g_view, V);
	XMStoreFloat4x4(&g_proj, P);
};

void RenderFrame()
{
	HR(g_alloc[g_frameIndex]->Reset());
	HR(g_cmdList->Reset(g_alloc[g_frameIndex].Get(), g_pso.Get()));

	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_cmdList->ResourceBarrier(1, &toRT);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		g_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		g_frameIndex, g_rtvInc);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	g_cmdList->RSSetViewports(1, &g_viewport);
	g_cmdList->RSSetScissorRects(1, &g_scissor);

	const FLOAT clear[4] = { 0.78f, 0.949f, 0.996f, 1.0f };
	g_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);
	g_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	static LARGE_INTEGER f = []() { LARGE_INTEGER t; QueryPerformanceFrequency(&t); return t; }();
	static LARGE_INTEGER p = []() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t; }();
	LARGE_INTEGER n; QueryPerformanceCounter(&n);
	float dt = float(double(n.QuadPart - p.QuadPart) / double(f.QuadPart));
	p = n;
	g_angle += dt * DirectX::XM_PIDIV4;

	XMMATRIX M = XMMatrixRotationY(g_angle) * XMMatrixRotationX(g_angle * 0.5f);
	XMMATRIX V = XMLoadFloat4x4(&g_view);
	XMMATRIX P = XMLoadFloat4x4(&g_proj);
	XMMATRIX MVP = XMMatrixTranspose(M * V * P);

	VSConstants c{}; XMStoreFloat4x4(&c.mvp, MVP);
	std::memcpy(g_cbPtr, &c, sizeof(c));

	g_cmdList->SetGraphicsRootSignature(g_rootSig.Get());
	g_cmdList->SetPipelineState(g_pso.Get());
	g_cmdList->SetGraphicsRootConstantBufferView(0, g_cb->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
	g_cmdList->SetDescriptorHeaps(1, heaps);
	g_cmdList->SetGraphicsRootDescriptorTable(1, g_srvHeap->GetGPUDescriptorHandleForHeapStart());

	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_cmdList->IASetVertexBuffers(0, 1, &g_vbv);
	g_cmdList->IASetIndexBuffer(&g_ibv);

	g_cmdList->DrawIndexedInstanced(g_indexCount, 1, 0, 0, 0);

	auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_cmdList->ResourceBarrier(1, &toPresent);

	HR(g_cmdList->Close());

	// submit + present + sync
	ID3D12CommandList* lists[] = { g_cmdList.Get() };
	g_cmdQueue->ExecuteCommandLists(1, lists);
	HR(g_swapChain->Present(1, 0));

	const UINT64 fenceToWait = g_fenceValue++;
	HR(g_cmdQueue->Signal(g_fence.Get(), fenceToWait));
	if (g_fence->GetCompletedValue() < fenceToWait) {
		HR(g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent));
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

void WaitForGPU() {
	if (!g_fence || !g_cmdQueue) return;

	const UINT64 fenceToWait = g_fenceValue++;
	HR(g_cmdQueue->Signal(g_fence.Get(), fenceToWait));

	// Event должен существовать
	if (!g_fenceEvent) {
		g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!g_fenceEvent) {
			HR(HRESULT_FROM_WIN32(GetLastError()));
		}
	}

	HR(g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent));
	WaitForSingleObject(g_fenceEvent, INFINITE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_DESTROY: PostQuitMessage(0); return 0;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) { DestroyWindow(hWnd); }
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
	const wchar_t* kClassName = L"BitchEngine";
	wchar_t window_name[25] = L"bitchengine FPS: ";

	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

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

	CoUninitialize();

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