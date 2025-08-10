#pragma once
#include <vector>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <d3d12.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct VertexOBJ {
    float px, py, pz;   // POSITION
    float r, g, b;      // COLOR
    float u, v;         // TEXCOORD
};

struct MeshGPU {
    ComPtr<ID3D12Resource> vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT indexCount = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
};
