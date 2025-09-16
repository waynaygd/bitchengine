﻿// d3d_init.cpp
#include "d3d_init.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <imgui/imgui_internal.h>
#include <cassert>

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

            ImGui::SliderFloat("UV multiplier", &e.uvMul, 0.1f, 32.0f, "%.2f");

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
            if (ImGui::SliderInt("Anisotropy", &g_uiAniso, 1, 16)) {
                DX_FillSamplers(); // пересоздаём все 15 сэмплеров с новым MaxAnisotropy
            }
        }
        ImGui::Text("Sampler idx = %d", g_uiAddrMode * 3 + g_uiFilter);
    }
    ImGui::End();

    if (ImGui::Begin("GBuffer Debug"))
    {
        static const char* modes[] = { "Lighting", "Albedo", "Normal", "Position" };
        ImGui::Combo("View", &g_gbufDebugMode, modes, IM_ARRAYSIZE(modes));
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

void DX_CreateGBuffer(UINT w, UINT h)
{
    // 0) RTV heap на 3 дескриптора
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 3;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_gbufRTVHeap)));
    g_gbufRTVInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 1) Форматы
    DXGI_FORMAT fmtAlbedo = DXGI_FORMAT_R8G8B8A8_UNORM;           // ресурс в UNORM
    DXGI_FORMAT fmtNormal = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT fmtPosition = DXGI_FORMAT_R16G16B16A16_FLOAT;

    // 2) Создаём 3 текстуры (RTV) сразу в состоянии RENDER_TARGET
    auto makeRT = [&](DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& out, const FLOAT clear[4])
        {
            CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
                fmt, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            D3D12_CLEAR_VALUE cv{}; cv.Format = fmt;
            cv.Color[0] = clear[0]; cv.Color[1] = clear[1]; cv.Color[2] = clear[2]; cv.Color[3] = clear[3];

            HR(g_device->CreateCommittedResource(
                &heapDefault, D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,   // ← сразу RT
                &cv,
                IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
        };

    const FLOAT clrA[4] = { 0,0,0,1 };
    const FLOAT clrN[4] = { 0,0,1,1 };
    const FLOAT clrP[4] = { 0,0,0,0 };

    makeRT(fmtAlbedo, g_gbufAlbedo, clrA);
    makeRT(fmtNormal, g_gbufNormal, clrN);
    makeRT(fmtPosition, g_gbufPosition, clrP);

    // 3) Синхронизируем с массивами, чтобы RenderFrame мог работать по индексу
    g_gbuf[0] = g_gbufAlbedo;
    g_gbuf[1] = g_gbufNormal;
    g_gbuf[2] = g_gbufPosition;

    g_gbufState[0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_gbufState[1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_gbufState[2] = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // 4) RTV дескрипторы
    auto rtvStart = g_gbufRTVHeap->GetCPUDescriptorHandleForHeapStart();
    g_gbufRTV[0] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 0, g_gbufRTVInc);
    g_gbufRTV[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 1, g_gbufRTVInc);
    g_gbufRTV[2] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 2, g_gbufRTVInc);

    g_device->CreateRenderTargetView(g_gbufAlbedo.Get(), nullptr, g_gbufRTV[0]);
    g_device->CreateRenderTargetView(g_gbufNormal.Get(), nullptr, g_gbufRTV[1]);
    g_device->CreateRenderTargetView(g_gbufPosition.Get(), nullptr, g_gbufRTV[2]);

    // 5) SRV’шки (в общую g_srvHeap) — нужные для lighting/отладки
    // ВАЖНО: сделать тремя подряд, чтобы в lighting-pass привязать их одним table (t0..t2).
    auto makeSRV2D = [&](ID3D12Resource* res, DXGI_FORMAT fmt, UINT slot)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = fmt;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            g_device->CreateShaderResourceView(res, &sd, SRV_CPU(slot));
        };

    // Выделяем подряд 4 слота
    UINT base = SRV_Alloc();                  // t0
    /* проверим, что хватает места под ещё 3 подряд */
    assert(base + 3 < g_srvCapacity);

    // Привязываем индексы
    g_gbufAlbedoSRV = base + 0;
    g_gbufNormalSRV = base + 1;
    g_gbufPositionSRV = base + 2;
    g_gbufDepthSRV = base + 3;

    // Создаём SRV
    makeSRV2D(g_gbufAlbedo.Get(), fmtAlbedo, g_gbufAlbedoSRV);
    makeSRV2D(g_gbufNormal.Get(), fmtNormal, g_gbufNormalSRV);
    makeSRV2D(g_gbufPosition.Get(), fmtPosition, g_gbufPositionSRV);

    // ВАЖНО: depth-ресурс уже создан в DX_CreateDepth → делаем SRV здесь
    makeSRV2D(g_depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT, g_gbufDepthSRV);

    // На всякий случай оставим контроль
    assert(g_gbufNormalSRV == g_gbufAlbedoSRV + 1);
    assert(g_gbufPositionSRV == g_gbufAlbedoSRV + 2);
    assert(g_gbufDepthSRV == g_gbufAlbedoSRV + 3);

    // если в lighting-pass ты делаешь SetGraphicsRootDescriptorTable(0, SRV_GPU(g_gbufAlbedoSRV)),
    // то убедись, что g_gbufNormalSRV == g_gbufAlbedoSRV + 1, а g_gbufPositionSRV == +2.
    // Если твой аллокатор уже выдаёт подряд — отлично. Если нет, лучше сделать отдельный SRV-пул для GBuffer
    // и выдавать ручками «базу» и «база + i».
}


void CreateGBufferRSandPSO()
{
    // Root params
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;      // одна текстура-альбедо на объект
    range.BaseShaderRegister = 0;  // t0
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rp[2]{};
    // b0: CBPerObject
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // t0: texture
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // static sampler s0
    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_ANISOTROPIC;
    samp.MaxAnisotropy = 8;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samp.ShaderRegister = 0; // s0
    samp.RegisterSpace = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;
    rs.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rsGBuffer)));

    auto vs = CompileShaderFromFile(L"shaders\\gbuf_vs.hlsl", "main", "vs_5_1");
    auto ps = CompileShaderFromFile(L"shaders\\gbuf_ps.hlsl", "main", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      { "POSITION",0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "NORMAL",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "TEXCOORD",0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rsGBuffer.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.InputLayout = { inputElems, _countof(inputElems) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = g_depthFormat;
    pso.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_psoGBuffer)));
}

void CreateLightingRSandPSO()
{
    // t0..t2
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 4;       // Albedo, Normal, Position
    range.BaseShaderRegister = 0;   // t0
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rp[2]{};
    // Таблица SRV gbuffer
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // b1: CBLighting
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[1].Descriptor.ShaderRegister = 1;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Самплер (линейный, clamp)
    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samp.ShaderRegister = 0; // s0
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;
    rs.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS; // VS не нужен для full-screen tri

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rsLighting)));

    auto lvs = CompileShaderFromFile(L"shaders\\light_vs.hlsl", "main", "vs_5_1");
    auto lps = CompileShaderFromFile(L"shaders\\light_ps.hlsl", "main", "ps_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rsLighting.Get();
    pso.VS = { lvs->GetBufferPointer(), lvs->GetBufferSize() };
    pso.PS = { lps->GetBufferPointer(), lps->GetBufferSize() };
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    // depth не нужен
    auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rast.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState = rast;
    D3D12_DEPTH_STENCIL_DESC ds{}; // OFF
    ds.DepthEnable = FALSE; ds.StencilEnable = FALSE;
    pso.DepthStencilState = ds;
    pso.InputLayout = { nullptr, 0 }; // без вершинного буфера
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = g_backBufferFormat;
    pso.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_psoLighting)));
}

const DXGI_FORMAT DEPTH_RES_FMT = DXGI_FORMAT_R32_TYPELESS; // ресурс
const DXGI_FORMAT DEPTH_DSV_FMT = DXGI_FORMAT_D32_FLOAT;    // DSV
const DXGI_FORMAT DEPTH_SRV_FMT = DXGI_FORMAT_R32_FLOAT;    // SRV

void DX_CreateDepth(UINT w, UINT h)
{
    // 0) Форматы (ресурс/DSV/SRV)
    const DXGI_FORMAT DepthResFmt = DXGI_FORMAT_R32_TYPELESS; // ресурс
    const DXGI_FORMAT DepthDSVFmt = DXGI_FORMAT_D32_FLOAT;    // DSV view
    const DXGI_FORMAT DepthSRVFmt = DXGI_FORMAT_R32_FLOAT;    // SRV view

    // Если где-то используешь g_depthFormat как "формат DSV" — зададим его так:
    g_depthFormat = DepthDSVFmt;

    // 1) DSV heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    HR(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)));

    // 2) Ресурс depth с ALLOW_DEPTH_STENCIL
    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DepthResFmt, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DepthDSVFmt;                 // ВАЖНО: формат clear-value = формат DSV
    clear.DepthStencil = { 1.0f, 0 };

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,        // стартуем в режиме записи
        &clear,
        IID_PPV_ARGS(&g_depthBuffer)));

    // 3) Создаём DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DepthDSVFmt;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(
        g_depthBuffer.Get(), &dsv,
        g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    // 4) Создаём SRV в общем SRV heap (для чтения глубины в lighting)
    //    ВАЖНО: положить его подряд после G0 (albedo) и G1 (normal),
    //    чтобы t0..t3 шли одним диапазоном.
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DepthSRVFmt;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;

    UINT slot = SRV_Alloc();
    g_device->CreateShaderResourceView(g_depthBuffer.Get(), &sd, SRV_CPU(slot));
    g_gbufDepthSRV = slot; // сохрани, чтобы потом проверить непрерывность t0..t3
}

void DX_CreateSRVHeap(UINT numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;

    HR(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_srvHeap)));

    g_srvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srvCapacity = numDescriptors;
    g_srvNext = 0;
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
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;      // t0
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 1;      // s0
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rp[3]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;               // b0
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;  // t0
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;  // s0
    rp[2].DescriptorTable.NumDescriptorRanges = 1;
    rp[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 0;            // <‑‑ нет статических!
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
    UINT texError = RegisterTextureFromFile(L"assets\\textures\\default_white.png");
    UINT texDefault = RegisterTextureFromFile(L"assets\\textures\\error_tex.png");
    UINT texZagar = RegisterTextureFromFile(L"assets\\textures\\zagarskih_normal.dds");

    // МЕШИ
    UINT meshZagarskih = RegisterOBJ(L"assets\\models\\zagarskih.obj");

    // СЦЕНА
    Scene_AddEntity(meshZagarskih, texZagar, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
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

void CreatePerObjectCB(UINT maxPerFrame)
{
    g_cbMaxPerFrame = maxPerFrame;
    g_cbStride = (UINT)((sizeof(CBPerObject) + 255) & ~255u); // align 256
    const UINT totalSize = g_cbStride * g_cbMaxPerFrame * kFrameCount;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

    HR(g_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&g_cbPerObject)));

    // Мапим один раз на весь срок жизни
    HR(g_cbPerObject->Map(0, nullptr, reinterpret_cast<void**>(&g_cbPerObjectPtr)));
}

template<class T>
static void CreateUploadCB(ComPtr<ID3D12Resource>& cb, uint8_t*& mappedPtr) {
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(T) + 255) & ~255);
    HR(g_device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&cb)));
    HR(cb->Map(0, nullptr, reinterpret_cast<void**>(&mappedPtr)));
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

    DX_CreateSRVHeap(256);
    DX_CreateSamplerHeap();
    DX_FillSamplers();

    DX_CreateDepth(w, h);
    DX_CreateGBuffer(w, h);
    CreateGBufferRSandPSO();

    CreateLightingRSandPSO();

    CreatePerObjectCB(1024);
    CreateUploadCB<CBLighting>(g_cbLighting, g_cbLightingPtr);

    DX_CreateImGuiHeap();
    InitImGui(g_hWnd);

    DX_CreateFrameCmdObjects();

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

    DX_DestroyGBuffer();
    DX_CreateGBuffer(w, h);

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

inline D3D12_TEXTURE_ADDRESS_MODE ToAddress(int a)
{
    switch (a) {
    default:
    case ADDR_WRAP:        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case ADDR_MIRROR:      return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case ADDR_CLAMP:       return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case ADDR_BORDER:      return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case ADDR_MIRROR_ONCE: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    }
}

void DX_CreateSamplerHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC d{};
    d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    d.NumDescriptors = ADDR_COUNT * FILT_COUNT; // 15
    d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // ВАЖНО
    HR(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_sampHeap)));
    g_sampInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void DX_FillSamplers()
{
    auto cpu0 = g_sampHeap->GetCPUDescriptorHandleForHeapStart();

    for (int addr = 0; addr < ADDR_COUNT; ++addr)
    {
        for (int fil = 0; fil < FILT_COUNT; ++fil)
        {
            D3D12_SAMPLER_DESC s{};
            // === ФИЛЬТР ===
            if (fil == FILT_POINT) {
                s.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                s.MaxAnisotropy = 1;
            }
            else if (fil == FILT_LINEAR) {
                s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                s.MaxAnisotropy = 1;
            }
            else { // FILT_ANISO
                s.Filter = D3D12_FILTER_ANISOTROPIC;
                s.MaxAnisotropy = std::clamp(g_uiAniso, 1, 16);
            }

            // === АДРЕСА ===
            D3D12_TEXTURE_ADDRESS_MODE am = ToAddress(addr);
            s.AddressU = s.AddressV = s.AddressW = am;

            // НЕ задаём s.ComparisonFunc для обычных сэмплеров (иначе ворнинги)
            s.MipLODBias = 0.0f;
            s.MinLOD = 0.0f;
            s.MaxLOD = D3D12_FLOAT32_MAX;

            // Если Border — зададим цвет рамки (например, чёрная)
            if (am == D3D12_TEXTURE_ADDRESS_MODE_BORDER) {
                s.BorderColor[0] = 0.0f;
                s.BorderColor[1] = 0.0f;
                s.BorderColor[2] = 0.0f;
                s.BorderColor[3] = 1.0f;
            }

            // Пишем сэмплер по индексу: idx = addr*3 + fil
            int idx = addr * FILT_COUNT + fil;
            auto dst = CD3DX12_CPU_DESCRIPTOR_HANDLE(cpu0, idx, g_sampInc);
            g_device->CreateSampler(&s, dst);
        }
    }
}

// Удобный геттер GPU-хэндла выбранного сэмплера
D3D12_GPU_DESCRIPTOR_HANDLE DX_GetSamplerHandle(int addrMode, int filterMode)
{
    UINT idx = (UINT)(addrMode * 3 + filterMode);
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        g_sampHeap->GetGPUDescriptorHandleForHeapStart(), idx, g_sampInc);
}

UINT SRV_Alloc()
{
    if (g_srvNext >= g_srvCapacity)
    {
        MessageBoxA(nullptr, "SRV heap exhausted! Increase heap size.", "SRV_Alloc Error", MB_ICONERROR);
        throw std::runtime_error("SRV heap exhausted");
    }
    return g_srvNext++;
}

D3D12_CPU_DESCRIPTOR_HANDLE SRV_CPU(UINT index)
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        index, g_srvInc
    );
}

D3D12_GPU_DESCRIPTOR_HANDLE SRV_GPU(UINT index)
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        g_srvHeap->GetGPUDescriptorHandleForHeapStart(),
        index, g_srvInc
    );
}

void GBuffer_GetRTVs(D3D12_CPU_DESCRIPTOR_HANDLE out[3])
{
    auto start = g_gbufRTVHeap->GetCPUDescriptorHandleForHeapStart();
    out[0] = CD3DX12_CPU_DESCRIPTOR_HANDLE(start, 0, g_gbufRTVInc); // Albedo
    out[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE(start, 1, g_gbufRTVInc); // Normal
    out[2] = CD3DX12_CPU_DESCRIPTOR_HANDLE(start, 2, g_gbufRTVInc); // Position
}

void DX_DestroyGBuffer()
{
    g_gbufAlbedo.Reset();
    g_gbufNormal.Reset();
    g_gbufPosition.Reset();
    g_gbufRTVHeap.Reset();
    g_gbufAlbedoSRV = g_gbufNormalSRV = g_gbufPositionSRV = UINT_MAX;
}

void Transition(ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* res,
    D3D12_RESOURCE_STATES& stateVar,
    D3D12_RESOURCE_STATES newState)
{
    if (stateVar == newState) return;
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(res, stateVar, newState);
    cmd->ResourceBarrier(1, &b);
    stateVar = newState;
}
