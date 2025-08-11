#pragma once
#include <windows.h>
#include <windowsx.h>
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
#include <algorithm>

#include "camera.h"
#include "textures.h"
#include "obj_loader.h"

//externals
#include <d3dx12.h>
#include "DirectXTex.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline constexpr UINT kFrameCount = 2;

extern HWND g_hWnd;
extern ComPtr<IDXGIFactory7>        g_factory;
extern ComPtr<ID3D12Device>         g_device;
extern ComPtr<ID3D12CommandQueue>   g_cmdQueue;
extern ComPtr<IDXGISwapChain3>      g_swapChain;
extern ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
extern UINT                         g_rtvInc;
extern ComPtr<ID3D12Resource>       g_backBuffers[kFrameCount];
extern ComPtr<ID3D12CommandAllocator> g_alloc[kFrameCount];
extern ComPtr<ID3D12GraphicsCommandList> g_cmdList;

extern ComPtr<ID3D12Fence>          g_fence;
extern HANDLE                       g_fenceEvent;
extern UINT64                       g_fenceValue;
extern UINT                         g_frameIndex;

extern ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
extern ComPtr<ID3D12Resource>       g_depthBuffer;
extern DXGI_FORMAT g_backBufferFormat;
extern DXGI_FORMAT g_depthFormat;
extern D3D12_VIEWPORT g_viewport;
extern D3D12_RECT     g_scissor;

extern ComPtr<ID3D12DescriptorHeap> g_srvHeap;
extern ComPtr<ID3D12Resource> g_tex;

extern ComPtr<ID3D12CommandAllocator>     g_uploadAlloc;
extern ComPtr<ID3D12GraphicsCommandList>  g_uploadList;

extern ComPtr<ID3D12Resource> g_vb, g_ib;
extern D3D12_VERTEX_BUFFER_VIEW g_vbv;
extern D3D12_INDEX_BUFFER_VIEW  g_ibv;
extern UINT g_indexCount;

extern ComPtr<ID3D12RootSignature> g_rootSig;
extern ComPtr<ID3D12PipelineState> g_pso;


struct alignas(256) VSConstants {
    XMFLOAT4X4 mvp;
};

extern ComPtr<ID3D12Resource> g_cb;     // upload-ресурс под CB
extern uint8_t* g_cbPtr; // мапнутый указатель
extern float                  g_angle;    // дл€ вращени€

//  амера/проекци€ (храним отдельно, чтобы не пересчитывать каждый кадр)
extern XMFLOAT4X4 g_view, g_proj;
extern Camera g_cam;

extern XMFLOAT3 g_camPos;
extern float g_yaw;   // вращение по оси Y
extern float g_pitch; // вращение по оси X

// Ќастройки
extern bool g_mouseLook;
extern POINT g_lastMouse;
extern bool g_mouseHasPrev;

extern bool g_appActive;   // есть ли фокус у нашего окна

// models
extern MeshGPU g_meshOBJ; // глобально

void InitD3D12(HWND hWnd, UINT width, UINT height);
void RenderFrame();
void WaitForGPU();

void UpdateInput(float dt);

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

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(
    const std::wstring& path,
    const char* entry,
    const char* target);

ScratchImage LoadTextureFile(const std::wstring& filename);