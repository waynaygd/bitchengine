#include "d3dinit.h"

// ===== 1) Низкий уровень =====
void CreateDeviceAndQueue() {
    HR(CreateDXGIFactory1(IID_PPV_ARGS(&g_factory)));

#if defined(_DEBUG)
    if (Microsoft::WRL::ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; g_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
    }
    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_device)));

    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(g_device->CreateCommandQueue(&q, IID_PPV_ARGS(&g_cmdQueue)));
}

void CreateFenceAndUploadList() {
    HR(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceValue = 1;
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    HR(g_fenceEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

    HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAlloc)));
    HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAlloc.Get(), nullptr, IID_PPV_ARGS(&g_uploadList)));
    HR(g_uploadList->Close());
}

void CreateSwapChainAndRTVs(HWND hWnd, UINT width, UINT height) {
    DXGI_SWAP_CHAIN_DESC1 sc{}; sc.BufferCount = kFrameCount;
    sc.Width = width; sc.Height = height;
    sc.Format = g_backBufferFormat;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    HR(g_factory->CreateSwapChainForHwnd(g_cmdQueue.Get(), hWnd, &sc, nullptr, nullptr, &sc1));
    HR(sc1.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{}; rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtvDesc.NumDescriptors = kFrameCount;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kFrameCount; ++i) {
        HR(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])));
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtvStart, i, g_rtvInc);
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, h);
    }
}

void CreateDepthAndDSV(UINT width, UINT height) {
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{}; dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; dsvDesc.NumDescriptors = 1;
    HR(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)));

    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        g_depthFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{}; clear.Format = g_depthFormat; clear.DepthStencil = { 1.0f, 0 };

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&g_depthBuffer)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = g_depthFormat; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsv, g_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void CreateSRVHeap(UINT num) {
    D3D12_DESCRIPTOR_HEAP_DESC h{}; h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; h.NumDescriptors = num;
    h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(g_device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&g_srvHeap)));
}

void CreateFrameCommandLists() {
    for (UINT i = 0; i < kFrameCount; ++i)
        HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i])));
    HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0].Get(), nullptr, IID_PPV_ARGS(&g_cmdList)));
    HR(g_cmdList->Close());
    // viewport/scissor подставишь в InitD3D12
}

void CreateCB()
{
    const UINT cbSize = (sizeof(VSConstants) + 255) & ~255u;
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   desc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
    HR(g_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cb)));
    HR(g_cb->Map(0, nullptr, reinterpret_cast<void**>(&g_cbPtr)));
}

void BeginUpload() {
    HR(g_uploadAlloc->Reset());
    HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));
}
void EndUploadAndFlush() {
    HR(g_uploadList->Close());
    ID3D12CommandList* lists[]{ g_uploadList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();
    g_uploadKeepAlive.clear();
}

void UploadOBJ(const std::wstring& path, MeshGPU& out) {
    // внутри LoadOBJToGPU ты создаёшь DEFAULT+UPLOAD для VB/IB, пишешь Copy/Barriers в g_uploadList
    // и делаешь KeepAlive(uploadBuf’ы)
    HR(LoadOBJToGPU(path.c_str(), g_device.Get(), g_uploadList.Get(), out) ? S_OK : E_FAIL);
}

void UploadTexture(const std::wstring& path, Microsoft::WRL::ComPtr<ID3D12Resource>& outTex) {
    ScratchImage img = LoadTextureFile(path);
    const TexMetadata& meta = img.GetMetadata();

    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        meta.format, meta.width, (UINT)meta.height, (UINT16)meta.arraySize, (UINT16)meta.mipLevels);

    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outTex)));

    // upload buffer
    UINT subCount = (UINT)img.GetImageCount();
    UINT64 uploadSize = GetRequiredIntermediateSize(outTex.Get(), 0, subCount);
    Microsoft::WRL::ComPtr<ID3D12Resource> texUpload;
    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE,
        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texUpload)));
    KeepAlive(texUpload);

    // barriers + copy
    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(outTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    g_uploadList->ResourceBarrier(1, &toCopy);

    std::vector<D3D12_SUBRESOURCE_DATA> subs;
    PrepareUpload(g_device.Get(), img.GetImages(), subCount, meta, subs);
    UpdateSubresources(g_uploadList.Get(), outTex.Get(), texUpload.Get(), 0, 0, subCount, subs.data());

    auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(outTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    g_uploadList->ResourceBarrier(1, &toSRV);
}

void CreateSRVForTexture(ID3D12Resource* tex)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    const auto& desc = tex->GetDesc();
    sd.Format = desc.Format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = desc.MipLevels;

    auto cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    g_device->CreateShaderResourceView(tex, &sd, cpu);
}

void CreateRootSigAndPSO()
{
    // RS: b0 (VS CBV), t0 (PS SRV), s0 (PS sampler)
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0; // t0

    D3D12_ROOT_PARAMETER rp[2]{};
    // CBV b0 (VS)
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // SRV table t0 (PS)
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ShaderRegister = 0; // s0
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
        | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
        | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rootSig)));

    // Шейдеры (используем уже имеющийся CompileShaderFromFile)
    auto vs = CompileShaderFromFile(L"shaders\\cube_vs.hlsl", "main", "vs_5_1");
    auto ps = CompileShaderFromFile(L"shaders\\cube_ps.hlsl", "main", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { inputElems, _countof(inputElems) };
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = TRUE; // если у твоих моделей CCW
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = g_backBufferFormat;
    pso.DSVFormat = g_depthFormat;
    pso.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_pso)));
}

void InitCamera(UINT width, UINT height)
{
    g_cam.pos = { 0, 0, -5 };
    g_cam.yaw = 0.0f; g_cam.pitch = 0.0f;
    g_cam.SetLens(XM_PIDIV4, float(width) / float(height), 0.1f, 100.0f);
    g_cam.UpdateView();

    // viewport/scissor (если ещё не выставлял)
    g_viewport = { 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f };
    g_scissor = { 0, 0, (LONG)width, (LONG)height };
}

void InitD3D12(HWND hWnd, UINT width, UINT height)
{
    // 1) Низкоуровневые шаги
    CreateDeviceAndQueue();
    CreateFenceAndUploadList();
    CreateSwapChainAndRTVs(hWnd, width, height);
    CreateDepthAndDSV(width, height);
    CreateSRVHeap(1);
    CreateFrameCommandLists();
    CreateCB(); // гарантирует g_cb / g_cbPtr

    // 2) Upload-фаза: OBJ + текстура в один командный список
    BeginUpload();
    UploadOBJ(L"assets\\models\\zagarskih.obj", g_meshOBJ);
    UploadTexture(L"assets\\textures\\zagarskih_normal.dds", g_tex);
    EndUploadAndFlush(); // Close + Execute + Wait + g_uploadKeepAlive.clear()

    // 3) SRV для текстуры
    CreateSRVForTexture(g_tex.Get());

    // 4) Рут-сигнатура и PSO
    CreateRootSigAndPSO();

    // 5) Камера/вьюпорт
    InitCamera(width, height);
}

void CreateDefaultBufferUpload(
    ID3D12Device* dev, ID3D12GraphicsCommandList* cmd,
    const void* data, size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outDefault,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outUpload,
    D3D12_RESOURCE_STATES finalState)
{
    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);

    // DEFAULT
    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    HR(dev->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE,
        &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(outDefault.ReleaseAndGetAddressOf())));

    // UPLOAD
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    HR(dev->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE,
        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(outUpload.ReleaseAndGetAddressOf())));

    // map & copy
    void* mapped = nullptr; CD3DX12_RANGE noRead(0, 0);
    HR(outUpload->Map(0, &noRead, &mapped));
    std::memcpy(mapped, data, bytes);
    outUpload->Unmap(0, nullptr);

    // barriers + copy
    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(outDefault.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1, &toCopy);
    cmd->CopyBufferRegion(outDefault.Get(), 0, outUpload.Get(), 0, bytes);
    auto toFinal = CD3DX12_RESOURCE_BARRIER::Transition(outDefault.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    cmd->ResourceBarrier(1, &toFinal);

    // держим upload живым до Execute+Wait
    KeepAlive(outUpload);
}