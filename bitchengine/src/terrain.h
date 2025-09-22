// terrain.h
#pragma once
#include <DirectXMath.h>
#include <wrl/client.h>
#include <d3d12.h>
#include "mesh.h"
#include "vector"
#include <DirectXCollision.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct CBScene { 
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMFLOAT4X4 view;
};
static_assert(sizeof(CBScene) % 16 == 0);

// ��� ������������� (���������� ����� CB)

// ��������� �� ���� (cbuffer b0)
struct CBTerrainTile {
    XMFLOAT2 tileOrigin;   // world-���������� ������ ����� (x,z)
    float    tileSize;     // ����� ����� � ������
    float    heightScale;  // �� ������� �������� �������� �� heightmap
};
static_assert(sizeof(CBTerrainTile) % 16 == 0, "CB must be 16-byte aligned");

// CPU-��������� �����
struct TerrainTile {
    CBTerrainTile cb;
    ComPtr<ID3D12Resource> cbRes;  // upload CB
    uint8_t* cbPtr = nullptr;
};

// === Tile resources (�������� �����) ===
struct TileRes {
    CBTerrainTile cb;                       // b0: origin/size/heightScale
    D3D12_GPU_DESCRIPTOR_HANDLE heightSrv;  // t0
    D3D12_GPU_DESCRIPTOR_HANDLE diffuseSrv; // t1
    XMFLOAT3 aabbMin, aabbMax;              // � ����
    uint32_t level = 0;                     // LOD-������� (0=�������)
};

// === Quadtree ���� ===
struct QNode {
    uint32_t child[4]{ UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX };
    XMFLOAT2 origin;  // (x,z) ����
    float    size;    // ����� �������
    XMFLOAT3 aabbMin, aabbMax;
    int      tileIndex = -1; // ������ � g_tiles ���� ����
    uint8_t  level = 0;
};


struct Plane { 
    XMFLOAT4 p; 
};

struct SkirtVert { DirectX::XMFLOAT2 uv; float skirtK; };

extern MeshGPU g_terrainSkirt;
extern BoundingFrustum g_frustumProj;                // ������� � view-space
extern std::vector<TileRes> g_tiles;                 // ���� � ����
extern std::vector<QNode>   g_nodes;                 // ���� � ����
extern uint32_t             g_root;

void UpdateTilesHeight(float newScale);

void CreateTerrainGrid(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd, UINT N, ComPtr<ID3D12Resource>& vbUpOut,
    ComPtr<ID3D12Resource>& ibUpOut);

void BuildLeafTilesGrid(uint32_t gridN, float worldSize, float heightScale,
    D3D12_GPU_DESCRIPTOR_HANDLE heightSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE diffuseSrv);

void ExtractFrustum(Plane out[6], FXMMATRIX VP);

void InitTerrainTiling();

float DistanceToAabbHorizontal(const XMFLOAT3& c, const XMFLOAT3& mn, const XMFLOAT3& mx);

static void GatherLeaves(uint32_t id, std::vector<uint32_t>& out);

void RebuildTerrain(uint32_t gridN, float worldSize, float heightScale,
    D3D12_GPU_DESCRIPTOR_HANDLE heightSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE diffuseSrv);

void SelectNodes(uint32_t id,
    const XMFLOAT3& camPos,
    const BoundingFrustum& frWorld,   // ? ������ XMMATRIX VP
    float projScale, float thresholdPx,
    std::vector<uint32_t>& out);

void InitFrustum(const XMMATRIX& P);

bool AabbOutsideFrustumDXC(const BoundingFrustum& frWorld,
    const XMFLOAT3& mn, const XMFLOAT3& mx);

float ProjScaleFrom(const XMMATRIX& P, float viewportHpx);

void CreateTerrainSkirt(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd, UINT N, ComPtr<ID3D12Resource>& vbUpOut,
    ComPtr<ID3D12Resource>& ibUpOut);

static inline void Normalize(Plane& pl) {
    XMVECTOR v = XMLoadFloat4(&pl.p);
    v = XMPlaneNormalize(v);
    XMStoreFloat4(&pl.p, v);
};

bool AabbOutsideByVP(const DirectX::XMMATRIX& VP,
    const DirectX::XMFLOAT3& mn,
    const DirectX::XMFLOAT3& mx);


