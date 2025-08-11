// obj_loader.cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"   // только здесь!

#include "obj_loader.h"
#include "d3d_init.h"
#include <gpu_upload.h>

static std::string Narrow(const std::wstring& w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

bool LoadOBJToGPU(const std::wstring& pathW, ID3D12Device* device, ID3D12GraphicsCommandList* uploadCmd, MeshGPU& out)
{
    // 1) tinyobj: читаем и триангулируем
    tinyobj::ObjReaderConfig cfg;
    cfg.mtl_search_path = ""; // можно задать папку, если будешь тянуть материалы
    cfg.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(Narrow(pathW), cfg)) {
        OutputDebugStringA(reader.Error().c_str());
        return false;
    }
    if (!reader.Warning().empty()) {
        OutputDebugStringA(reader.Warning().c_str());
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    // 2) Собираем уникальные вершины по ключу (v, vt, vn)
    std::vector<VertexOBJ>   vertices;
    std::vector<uint32_t>    indices32; // соберём в 32-bit, потом решим формат

    struct Key { int v, vt, vn; };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            // простенький hash
            return (size_t)k.v * 73856093u ^ (size_t)k.vt * 19349663u ^ (size_t)k.vn * 83492791u;
        }
    };
    struct KeyEq {
        bool operator()(const Key& a, const Key& b) const noexcept {
            return a.v == b.v && a.vt == b.vt && a.vn == b.vn;
        }
    };

    std::unordered_map<Key, uint32_t, KeyHash, KeyEq> remap;
    remap.reserve(65536);

    auto addVertex = [&](const tinyobj::index_t& idx)->uint32_t {
        Key k{ idx.vertex_index, idx.texcoord_index, idx.normal_index };
        auto it = remap.find(k);
        if (it != remap.end()) return it->second;

        VertexOBJ v{};
        // position (обязательно есть в obj)
        v.px = attrib.vertices[3 * idx.vertex_index + 0];
        v.py = attrib.vertices[3 * idx.vertex_index + 1];
        v.pz = attrib.vertices[3 * idx.vertex_index + 2];

        // color = белый (если хочешь — парси tinyobj::attrib.colors)
        v.r = v.g = v.b = 1.0f;

        // uv (если были)
        if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
            v.u = attrib.texcoords[2 * idx.texcoord_index + 0];
            v.v = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]; // V-флип под D3D
        }
        else {
            v.u = v.v = 0.0f;
        }

        uint32_t newIndex = (uint32_t)vertices.size();
        vertices.push_back(v);
        remap.emplace(k, newIndex);
        return newIndex;
        };

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            indices32.push_back(addVertex(idx));
        }
    }

    // 3) Выбираем формат индексов
    bool use32 = (vertices.size() > 65535);
    out.indexFormat = use32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    // 4) Готовим CPU-буферы
    std::vector<uint16_t> indices16;
    indices16.reserve(indices32.size());
    if (!use32) {
        for (auto i : indices32) indices16.push_back((uint16_t)i);
    }

    // 5) Пишем в GPU (через твой uploadCmd)
    ComPtr<ID3D12Resource> vbUpload, ibUpload;
    CreateDefaultBuffer(device, uploadCmd,
        vertices.data(), vertices.size() * sizeof(VertexOBJ),
        out.vb, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    if (use32) {
        CreateDefaultBuffer(device, uploadCmd,
            indices32.data(), indices32.size() * sizeof(uint32_t),
            out.ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    else {
        CreateDefaultBuffer(device, uploadCmd,
            indices16.data(), indices16.size() * sizeof(uint16_t),
            out.ib, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }

    g_uploadKeepAlive.push_back(vbUpload);
    g_uploadKeepAlive.push_back(ibUpload);

    // 6) Views
    out.vbv.BufferLocation = out.vb->GetGPUVirtualAddress();
    out.vbv.StrideInBytes = sizeof(VertexOBJ);
    out.vbv.SizeInBytes = (UINT)(vertices.size() * sizeof(VertexOBJ));

    out.ibv.BufferLocation = out.ib->GetGPUVirtualAddress();
    out.ibv.Format = out.indexFormat;
    out.ibv.SizeInBytes = (UINT)((use32 ? indices32.size() * sizeof(uint32_t)
        : indices16.size() * sizeof(uint16_t)));

    out.indexCount = (UINT)indices32.size();
    return true;
}

UINT RegisterOBJ(const std::wstring& path)
{
    MeshGPU m{};
    DX_BeginUpload();
    if (!LoadOBJToGPU(path.c_str(), g_device.Get(), g_uploadList.Get(), m)) {
        DX_EndUploadAndFlush();
        throw std::runtime_error("OBJ load failed");
    }
    DX_EndUploadAndFlush();

    UINT id = (UINT)g_meshes.size();
    g_meshes.push_back(std::move(m));
    return id; // meshId
}

UINT CreateCubeMeshGPU()
{
    return 0;
}

