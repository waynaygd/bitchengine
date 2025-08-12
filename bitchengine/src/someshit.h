#pragma once
#define NOMINMAX
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
#include "sceneloadsave.h"

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
    float uvMul;
    float _pad[3];          
};

extern ComPtr<ID3D12Resource> g_cb;     // upload-������ ��� CB
extern uint8_t* g_cbPtr; // �������� ���������
extern float                  g_angle;    // ��� ��������

// ������/�������� (������ ��������, ����� �� ������������� ������ ����)
extern XMFLOAT4X4 g_view, g_proj;
extern Camera g_cam;

extern XMFLOAT3 g_camPos;
extern float g_yaw;   // �������� �� ��� Y
extern float g_pitch; // �������� �� ��� X

// ���������
extern bool g_mouseLook;
extern POINT g_lastMouse;
extern bool g_mouseHasPrev;

extern bool g_appActive;   // ���� �� ����� � ������ ����

extern ComPtr<ID3D12DescriptorHeap> g_imguiHeap;

constexpr UINT MAX_OBJECTS = 1024;
constexpr UINT CB_ALIGN = 256;
constexpr UINT CB_SIZE_ALIGNED = (sizeof(VSConstants) + (CB_ALIGN - 1)) & ~(CB_ALIGN - 1);

extern bool g_dxReady;
extern UINT g_pendingW;
extern UINT g_pendingH;

// --- �������� ---
struct TextureGPU {
    ComPtr<ID3D12Resource> res;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    UINT heapIndex = UINT(-1);
};

extern UINT g_srvInc;                     // increment SRV heap
extern std::vector<TextureGPU> g_textures;

extern std::vector<MeshGPU> g_meshes;

// --- �������� ����� ---
struct Entity {
    UINT meshId = 0;
    UINT texId = 0;
    DirectX::XMFLOAT3 pos{ 0,0,0 };
    DirectX::XMFLOAT3 rotDeg{ 0,0,0 }; // pitch,yaw,roll � ��������
    DirectX::XMFLOAT3 scale{ 1,1,1 };
};
extern std::vector<Entity> g_entities;

static UINT g_width = 1280, g_height = 720;

extern ComPtr<ID3D12DescriptorHeap> g_sampHeap;
extern UINT                         g_sampInc; // ���

// ����� ������������ (��� UI)
extern int g_uiAddrMode;   // 0..4 (Wrap, Mirror, Clamp, Border, MirrorOnce)
extern int g_uiFilter;   // 0..2 (Point, Linear, Anisotropic)
extern int g_uiAniso;   // 1..16 (������������, ���� Anisotropic)

extern float g_uvMul;

// ������������
UINT RegisterTextureFromFile(const std::wstring& path); // ���������� texId
UINT RegisterOBJ(const std::wstring& path);             // ���������� meshId
UINT CreateCubeMeshGPU();                                // meshId ��� ���������


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

inline UINT Scene_AddEntity(UINT meshId, UINT texId,
    XMFLOAT3 pos = { 0,0,0 }, XMFLOAT3 rotDeg = { 0,0,0 }, XMFLOAT3 scale = { 1,1,1 })
{
    Entity e; e.meshId = meshId; e.texId = texId; e.pos = pos; e.rotDeg = rotDeg; e.scale = scale;
    g_entities.push_back(e);
    return (UINT)g_entities.size() - 1;
}


#define HR(x) ThrowIfFailed((x), #x, __FILE__, __LINE__)

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(
    const std::wstring& path,
    const char* entry,
    const char* target);

bool LoadOBJToGPU(const std::wstring& path,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    MeshGPU& outMesh);

ScratchImage LoadTextureFile(const std::wstring& filename);