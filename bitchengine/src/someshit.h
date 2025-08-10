#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <string>
#include <format> 
#include <d3dcompiler.h>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <comdef.h>
#include <DirectXMath.h>

//externals
#include <d3dx12.h>
#include "DirectXTex.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static const UINT kFrameCount = 2;

HWND g_hWnd = nullptr;
ComPtr<IDXGIFactory7>        g_factory;
ComPtr<ID3D12Device>         g_device;
ComPtr<ID3D12CommandQueue>   g_cmdQueue;
ComPtr<IDXGISwapChain3>      g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
UINT                         g_rtvInc = 0;
ComPtr<ID3D12Resource>       g_backBuffers[kFrameCount];
ComPtr<ID3D12CommandAllocator> g_alloc[kFrameCount];
ComPtr<ID3D12GraphicsCommandList> g_cmdList;

ComPtr<ID3D12Fence>          g_fence;
HANDLE                       g_fenceEvent = nullptr;
UINT64                       g_fenceValue = 0;
UINT                         g_frameIndex = 0;

ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
ComPtr<ID3D12Resource>       g_depthBuffer;
DXGI_FORMAT g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
DXGI_FORMAT g_depthFormat = DXGI_FORMAT_D32_FLOAT;
D3D12_VIEWPORT g_viewport;
D3D12_RECT     g_scissor;

ComPtr<ID3D12DescriptorHeap> g_srvHeap;
ComPtr<ID3D12Resource> g_tex;

ComPtr<ID3D12CommandAllocator>     g_uploadAlloc;
ComPtr<ID3D12GraphicsCommandList>  g_uploadList;

ComPtr<ID3D12Resource> g_vb, g_ib;
D3D12_VERTEX_BUFFER_VIEW g_vbv{};
D3D12_INDEX_BUFFER_VIEW  g_ibv{};
UINT g_indexCount = 0;

ComPtr<ID3D12RootSignature> g_rootSig;
ComPtr<ID3D12PipelineState> g_pso;

struct alignas(256) VSConstants {
    XMFLOAT4X4 mvp;
};

ComPtr<ID3D12Resource> g_cb;     // upload-ресурс под CB
uint8_t* g_cbPtr = nullptr; // мапнутый указатель
float                  g_angle = 0.0f;    // дл€ вращени€

//  амера/проекци€ (храним отдельно, чтобы не пересчитывать каждый кадр)
XMFLOAT4X4 g_view, g_proj;

void InitD3D12(HWND hWnd, UINT width, UINT height);
void RenderFrame();
void WaitForGPU();

inline void ThrowIfFailed(HRESULT hr, const char* expr, const char* file, int line) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::wstringstream wss;
        wss << L"D3D12 call failed: " << expr << L"\nHR=0x"
            << std::hex << hr << L"\n" << err.ErrorMessage()
            << L"\n" << file << L":" << line;
        MessageBoxW(nullptr, wss.str().c_str(), L"DX12 Error", MB_ICONERROR);
        throw std::runtime_error("D3D12 call failed");
    }
}
#define HR(x) ThrowIfFailed((x), #x, __FILE__, __LINE__)

auto CompileShaderFromFile = [](const std::wstring& path,
    const char* entry,
    const char* target) -> ComPtr<ID3DBlob>
    {
        ComPtr<ID3DBlob> shader, error;
        HRESULT hr = D3DCompileFromFile(
            path.c_str(),
            nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, target,
            D3DCOMPILE_ENABLE_STRICTNESS, 0,
            &shader, &error
        );
        if (FAILED(hr))
        {
            if (error)
                OutputDebugStringA((char*)error->GetBufferPointer());
            throw std::runtime_error("Shader compilation failed");
        }
        return shader;
    };