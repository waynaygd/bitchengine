// d3d_init.cpp
#include "d3d_init.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <imgui/imgui_internal.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

// ────────────────────────────────────────────────────────────────────────────
// IMGUI
// ────────────────────────────────────────────────────────────────────────────
void InitImGui(HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_Init(
        g_device.Get(),
        kFrameCount,                      
        DXGI_FORMAT_R8G8B8A8_UNORM,         
        g_imguiHeap.Get(),                  
        g_imguiHeap->GetCPUDescriptorHandleForHeapStart(),
        g_imguiHeap->GetGPUDescriptorHandleForHeapStart()
    );

    io.Fonts->AddFontDefault();   
    bool okBuild = io.Fonts->Build();
    IM_ASSERT(okBuild && "Font atlas build failed (STB truetype disabled?)");

    ImGui_ImplDX12_CreateDeviceObjects();
}

void DX_CreateImGuiHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    HR(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_imguiHeap)));
}

void ShutdownImGui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

int g_selectedEntity = -1;

void BuildEditorUI()
{
    if (ImGui::Begin("Scene"))
    {
        if (ImGui::Button("Save Scene")) {
            if (!SaveScene(L"assets\\scenes\\autosave.scene"))
                OutputDebugStringA("SaveScene failed\n");
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Scene")) {
            if (!LoadScene(L"assets\\scenes\\autosave.scene"))
                OutputDebugStringA("LoadScene failed\n");
        }

        ImGui::Separator();

        // список сущностей
        for (int i = 0; i < (int)g_entities.size(); ++i) {
            std::string label = "Entity " + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), g_selectedEntity == i))
                g_selectedEntity = i;
        }

        ImGui::Separator();
        if (ImGui::Button("Add Cube")) {
            UINT cube = CreateCubeMeshGPU();
            UINT tex = g_textures.empty() ? 0u : 2u; // назначь первую текстуру или сделай выбор в UI
            Scene_AddEntity(cube, tex, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
        }

        if (g_selectedEntity >= 0 && g_selectedEntity < (int)g_entities.size()) {
            Entity& e = g_entities[g_selectedEntity];
            ImGui::Text("Transform");
            ImGui::DragFloat3("Position", &e.pos.x, 0.01f);
            ImGui::DragFloat3("Rotation (deg)", &e.rotDeg.x, 0.1f);
            ImGui::DragFloat3("Scale", &e.scale.x, 0.01f, 0.01f, 100.0f);

            // выбор mesh/texture
            if (ImGui::TreeNode("Bindings")) {
                if (ImGui::BeginListBox("Mesh")) {
                    for (UINT i = 0; i < g_meshes.size(); ++i) {
                        bool sel = (e.meshId == i);
                        char buf[32]; sprintf_s(buf, "mesh %u", i);
                        if (ImGui::Selectable(buf, sel)) e.meshId = i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndListBox();
                }
                if (ImGui::BeginListBox("Texture")) {
                    for (UINT i = 0; i < g_textures.size(); ++i) {
                        bool sel = (e.texId == i);
                        char buf[32]; sprintf_s(buf, "tex %u", i);
                        if (ImGui::Selectable(buf, sel)) e.texId = i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndListBox();
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::End();

    if (ImGui::Begin("Sampling"))
    {
        static const char* addrNames[] = { "Wrap","Mirror","Clamp","Border","MirrorOnce" };
        static const char* filtNames[] = { "Point","Linear","Anisotropic" };

        ImGui::Text("Address Mode:");
        ImGui::Combo("##addr", &g_uiAddrMode, addrNames, IM_ARRAYSIZE(addrNames));

        ImGui::Text("Filter:");
        ImGui::Combo("##filter", &g_uiFilter, filtNames, IM_ARRAYSIZE(filtNames));

        if (g_uiFilter == 2) {
            int prev = g_uiAniso;
            ImGui::SliderInt("Anisotropy", &g_uiAniso, 1, 16);
            if (g_uiAniso != prev) {
                // Перезапишем 15 дескрипторов с новым уровнем aniso
                DX_FillSamplers();
            }
        }

        ImGui::Text("Sampler idx = %d", g_uiAddrMode * 3 + g_uiFilter);
        ImGui::SliderFloat("UV multiplier", &g_uvMul, 1.0f, 16.0f, "%.1f");
    }
    ImGui::End();
}

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
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0; // t0

    // SAMPLER table (s0)
    D3D12_DESCRIPTOR_RANGE sampRange{};
    sampRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    sampRange.NumDescriptors = 1;
    sampRange.BaseShaderRegister = 0; // s0

    D3D12_ROOT_PARAMETER rp[3]{};

    // 0: CBV b0 (VS)
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // 1: SRV table (t0) для PS
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 2: SAMPLER table (s0) для PS
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable.NumDescriptorRanges = 1;
    rp[2].DescriptorTable.pDescriptorRanges = &sampRange;
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 0;  
    rs.pStaticSamplers = nullptr;
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

    auto blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto& rt0 = blend.RenderTarget[0];
    rt0.BlendEnable = TRUE;
    rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState = blend;

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
    UINT texMinecraft = RegisterTextureFromFile(L"assets\\textures\\Mineways2Skfb-RGBA.png");
    UINT texDefault = RegisterTextureFromFile(L"assets\\textures\\default_white.png");

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
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   desc = CD3DX12_RESOURCE_DESC::Buffer(UINT64(CB_SIZE_ALIGNED) * MAX_OBJECTS);

    HR(g_device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cb)));

    CD3DX12_RANGE range(0, 0);
    HR(g_cb->Map(0, &range, reinterpret_cast<void**>(&g_cbPtr)));
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
    DX_CreateSamplerHeap();
    DX_FillSamplers();

    DX_CreateImGuiHeap();
    InitImGui(g_hWnd);

    DX_CreateFrameCmdObjects();

    CreateCB();

    DX_LoadAssets();    
    DX_CreateRootSigAndPSO();
    DX_InitCamera(w, h);

    g_dxReady = true;

    // если во время CreateWindow пришёл WM_SIZE — применим отложенный ресайз
    if (g_pendingW && g_pendingH &&
        (g_pendingW != g_width || g_pendingH != g_height))
    {
        DX_Resize(g_pendingW, g_pendingH);
        g_pendingW = g_pendingH = 0;
    }
}

void DX_Resize(UINT w, UINT h)
{
    if (!g_device || !g_swapChain) return; // ещё рано
    if (w == 0 || h == 0) return;
    if (w == g_width && h == g_height) return;

    g_width = w; g_height = h;

    WaitForGPU();

    // release старые RT
    for (UINT i = 0; i < kFrameCount; ++i) g_backBuffers[i].Reset();
    g_rtvHeap.Reset();

    // resize swapchain
    HR(g_swapChain->ResizeBuffers(kFrameCount, w, h, g_backBufferFormat, 0));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    // создать RTV heap и RTV
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFrameCount;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kFrameCount; ++i) {
        HR(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])));
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtvStart, i, g_rtvInc);
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, h);
    }

    // depth заново
    g_depthBuffer.Reset();
    D3D12_CLEAR_VALUE clear{}; clear.Format = g_depthFormat; clear.DepthStencil.Depth = 1.0f;
    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(g_depthFormat, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&g_depthBuffer)));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = g_depthFormat; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsv, g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    // viewport / scissor / камера
    g_viewport = { 0.f, 0.f, float(w), float(h), 0.f, 1.f };
    g_scissor = { 0, 0, (LONG)w, (LONG)h };
    g_cam.SetLens(g_cam.fovY, float(w) / float(h), g_cam.zn, g_cam.zf);
}


void DX_Shutdown()
{
    WaitForGPU();

    // 1) ImGui
    ShutdownImGui();                // ImGui_ImplDX12_Shutdown/Win32_Shutdown + DestroyContext
    g_imguiHeap.Reset();

    // 2) сцена/ресурсы
    g_cb.Reset();
    g_tex.Reset();
    for (auto& m : g_meshes) { /* если есть отдельные ресурсы — Reset */ }
    g_meshes.clear(); g_textures.clear(); g_entities.clear();

    // 3) свопчейн/RTV/DSV/командные объекты
    for (auto& bb : g_backBuffers) bb.Reset();
    g_depthBuffer.Reset();
    g_rtvHeap.Reset();
    g_dsvHeap.Reset();
    g_srvHeap.Reset();

    g_cmdList.Reset();
    for (auto& a : g_alloc) a.Reset();
    g_uploadList.Reset();
    g_uploadAlloc.Reset();

    // 4) синхронизация/очередь/девайс
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    g_fence.Reset();
    g_cmdQueue.Reset();
    g_swapChain.Reset();
    g_device.Reset();

#if defined(_DEBUG)
    if (ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif
}

static D3D12_FILTER ToFilter(int uiFilter)
{
    switch (uiFilter) {
    case 0: return D3D12_FILTER_MIN_MAG_MIP_POINT;
    case 1: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    case 2: return D3D12_FILTER_ANISOTROPIC;
    default: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    }
}

static D3D12_TEXTURE_ADDRESS_MODE ToAddress(int uiAddr)
{
    switch (uiAddr) {
    case 0: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case 1: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case 2: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case 3: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case 4: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

void DX_CreateSamplerHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC d{};
    d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    d.NumDescriptors = 32; // запас
    d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_sampHeap)));
    g_sampInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

// 5 address × 3 filters = 15 сэмплеров подряд: индекс = addr*3 + filter
void DX_FillSamplers()
{
    auto cpu = g_sampHeap->GetCPUDescriptorHandleForHeapStart();

    for (int addr = 0; addr < 5; ++addr)
        for (int fil = 0; fil < 3; ++fil)
        {
            D3D12_SAMPLER_DESC s{};
            s.Filter = ToFilter(fil);
            s.AddressU = s.AddressV = s.AddressW = ToAddress(addr);
            s.MipLODBias = 0.0f;
            s.MaxAnisotropy = (fil == 2) ? g_uiAniso : 1;
            s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            s.MinLOD = 0.0f;
            s.MaxLOD = D3D12_FLOAT32_MAX;

            if (s.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER
                || s.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER
                || s.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
            {
                s.BorderColor[0] = 0; s.BorderColor[1] = 0; s.BorderColor[2] = 0; s.BorderColor[3] = 1; // чёрная рамка
            }

            auto dst = CD3DX12_CPU_DESCRIPTOR_HANDLE(cpu, addr * 3 + fil, g_sampInc);
            g_device->CreateSampler(&s, dst);
        }
}

// Удобный геттер GPU-хэндла выбранного сэмплера
D3D12_GPU_DESCRIPTOR_HANDLE DX_GetSamplerHandle(int addrMode, int filterMode)
{
    UINT idx = (UINT)(addrMode * 3 + filterMode);
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        g_sampHeap->GetGPUDescriptorHandleForHeapStart(), idx, g_sampInc);
}
