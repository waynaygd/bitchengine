#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <string>
#include <format> 
#include <d3dcompiler.h>
#include <stdexcept>
#include <sstream>
#include <comdef.h> // для _com_error

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
HANDLE                       g_fenceEvent = NULL;
UINT64                       g_fenceValue = 0;
UINT                         g_frameIndex = 0;

ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
ComPtr<ID3D12Resource>       g_depthBuffer;
DXGI_FORMAT g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
DXGI_FORMAT g_depthFormat = DXGI_FORMAT_D32_FLOAT;
D3D12_VIEWPORT g_viewport;
D3D12_RECT     g_scissor;

ComPtr<ID3D12Resource> g_vb, g_ib;
D3D12_VERTEX_BUFFER_VIEW g_vbv{};
D3D12_INDEX_BUFFER_VIEW  g_ibv{};
UINT g_indexCount = 0;

ComPtr<ID3D12RootSignature> g_rootSig;
ComPtr<ID3D12PipelineState> g_pso;

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
