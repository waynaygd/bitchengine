// d3d_init.cpp
#include "d3d_init.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

// ────────────────────────────────────────────────────────────────────────────
// НИЗКОУРОВНЕВЫЕ ШАГИ
// ────────────────────────────────────────────────────────────────────────────

void DX_CreateDeviceAndQueue()
{
#if defined(_DEBUG)
    if (ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif

    HR(CreateDXGIFactory1(IID_PPV_ARGS(&g_factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; g_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
    }
    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_device)));

    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(g_device->CreateCommandQueue(&q, IID_PPV_ARGS(&g_cmdQueue)));
}

void DX_CreateFenceAndUploadList()
{
    HR(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceValue = 1;
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    HR(g_fenceEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

    HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAlloc)));
    HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAlloc.Get(), nullptr,
        IID_PPV_ARGS(&g_uploadList)));
    HR(g_uploadList->Close());
}

void DX_CreateSwapchain(HWND hWnd, UINT w, UINT h)
{
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.BufferCount = kFrameCount;
    sc.Width = w; sc.Height = h;
    sc.Format = g_backBufferFormat;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.SampleDesc = { 1,0 };

    ComPtr<IDXGISwapChain1> sc1;
    HR(g_factory->CreateSwapChainForHwnd(g_cmdQueue.Get(), hWnd, &sc, nullptr, nullptr, &sc1));
    HR(sc1.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

void DX_CreateRTVs()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFrameCount;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE start(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kFrameCount; ++i) {
        HR(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])));
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(start, i, g_rtvInc);
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, h);
    }
}

void DX_CreateDepth(UINT w, UINT h)
{
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; dsvDesc.NumDescriptors = 1;
    HR(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)));

    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        g_depthFormat, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clear{};
    clear.Format = g_depthFormat; clear.DepthStencil = { 1.0f, 0 };

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&g_depthBuffer)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = g_depthFormat; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsv, g_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void DX_CreateSRVHeap(UINT numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC h{};
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    h.NumDescriptors = numDescriptors;            // например 128
    h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(g_device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&g_srvHeap)));

    g_srvInc = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DX_CreateFrameCmdObjects()
{
    for (UINT i = 0; i < kFrameCount; ++i)
        HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i])));

    HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0].Get(), nullptr,
        IID_PPV_ARGS(&g_cmdList)));
    HR(g_cmdList->Close());
}

// ────────────────────────────────────────────────────────────────────────────
// UPLOAD-ФАЗА
// ────────────────────────────────────────────────────────────────────────────

void DX_BeginUpload()
{
    HR(g_uploadAlloc->Reset());
    HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));
}

void DX_EndUploadAndFlush()
{
    HR(g_uploadList->Close());
    ID3D12CommandList* lists[]{ g_uploadList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();
    g_uploadKeepAlive.clear(); // если ты их используешь
}

// ────────────────────────────────────────────────────────────────────────────
// ПАЙПЛАЙН И КАМЕРА
// ────────────────────────────────────────────────────────────────────────────

void DX_CreateRootSigAndPSO()
{
    // Root: b0 (VS CBV), t0 (PS SRV), s0 (PS sampler)
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER rp[2]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rootSig)));

    // шейдеры и PSO (путь к твоим .hlsl)
    auto vs = CompileShaderFromFile(L"shaders\\cube_vs.hlsl", "main", "vs_5_1");
    auto ps = CompileShaderFromFile(L"shaders\\cube_ps.hlsl", "main", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { il, _countof(il) };
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    //pso.RasterizerState.FrontCounterClockwise = TRUE;
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = g_backBufferFormat;
    pso.DSVFormat = g_depthFormat;
    pso.SampleDesc = { 1,0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_pso)));
}

void DX_InitCamera(UINT w, UINT h)
{
    g_viewport = { 0.f,0.f,(float)w,(float)h,0.f,1.f };
    g_scissor = { 0,0,(LONG)w,(LONG)h };
    g_cam.pos = { 0,0,-5 };
    g_cam.yaw = 0.f; g_cam.pitch = 0.f;
    g_cam.SetLens(XM_PIDIV4, float(w) / float(h), 0.1f, 100.f);
    g_cam.UpdateView();
}

auto dump = [](UINT id) {
    const auto& T = g_textures[id];
    OutputDebugStringW((std::wstring(L"SRV idx=") + std::to_wstring(T.heapIndex) +
        L" gpu.ptr=" + std::to_wstring(T.gpu.ptr) + L"\n").c_str());
    };

// ────────────────────────────────────────────────────────────────────────────
// КРЮЧОК ДЛЯ АССЕТОВ (пока пустой, чтобы проект собрался)
// ────────────────────────────────────────────────────────────────────────────
void DX_LoadAssets()
{
    // ТЕКСТУРЫ
    UINT texZagar = RegisterTextureFromFile(L"assets\\textures\\zagarskih_normal.dds");
    dump(texZagar);
    UINT texMinecraft = RegisterTextureFromFile(L"assets\\textures\\Mineways2Skfb-RGBA.png");
    dump(texMinecraft);

    // МЕШИ
    UINT meshZagarskih = RegisterOBJ(L"assets\\models\\zagarskih.obj");
    UINT meshMinecraft = RegisterOBJ(L"assets\\models\\Mineways2Skfb.obj");

    // СЦЕНА
    Scene_AddEntity(meshZagarskih, texZagar, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
    Scene_AddEntity(meshMinecraft, texMinecraft, { 2,0,0 }, { 0,30,0 }, { 1,1,1 });
    // Scene_AddEntity(meshCube, texCrate, {-2,0,0}, {0,0,0}, {1,1,1});
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

// ────────────────────────────────────────────────────────────────────────────
// ГЛАВНЫЙ ОРКЕСТРАТОР
// ────────────────────────────────────────────────────────────────────────────
void InitD3D12(HWND hWnd, UINT w, UINT h)
{
    g_hWnd = hWnd;

    DX_CreateDeviceAndQueue();
    DX_CreateFenceAndUploadList();
    DX_CreateSwapchain(hWnd, w, h);
    DX_CreateRTVs();
    DX_CreateDepth(w, h);
    DX_CreateSRVHeap(128);
    DX_CreateFrameCmdObjects();

    CreateCB();

    DX_LoadAssets();    
    DX_CreateRootSigAndPSO();
    DX_InitCamera(w, h);
}
