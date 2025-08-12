#include "someshit.h"

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

ComPtr<ID3D12Resource> g_cb;     // upload-ресурс под CB
uint8_t* g_cbPtr = nullptr; // мапнутый указатель
float                  g_angle = 0.0f;    // дл€ вращени€

//  амера/проекци€ (храним отдельно, чтобы не пересчитывать каждый кадр)
XMFLOAT4X4 g_view, g_proj;
Camera g_cam;

XMFLOAT3 g_camPos = { 0.0f, 0.0f, -5.0f };
float g_yaw = 0.0f;   // вращение по оси Y
float g_pitch = 0.0f; // вращение по оси X

// Ќастройки
bool g_mouseLook = false;
POINT g_lastMouse = { 0, 0 };
bool g_mouseHasPrev = false;

bool g_appActive = false;   // есть ли фокус у нашего окна

std::vector<ComPtr<ID3D12Resource>> g_uploadKeepAlive;
ComPtr<ID3D12DescriptorHeap> g_imguiHeap;

bool g_dxReady = false;
UINT g_pendingW = 0, g_pendingH = 0;

UINT g_srvInc = 0;
std::vector<TextureGPU> g_textures;
std::vector<MeshGPU>    g_meshes;
std::vector<Entity>     g_entities;

ComPtr<ID3D12DescriptorHeap> g_sampHeap;
UINT                         g_sampInc = 0; // шаг

// ¬ыбор пользовател€ (дл€ UI)
int g_uiAddrMode = 0;   // 0..4 (Wrap, Mirror, Clamp, Border, MirrorOnce)
int g_uiFilter = 1;   // 0..2 (Point, Linear, Anisotropic)
int g_uiAniso = 8;   // 1..16 (используетс€, если Anisotropic)

float g_uvMul = 1.0f;