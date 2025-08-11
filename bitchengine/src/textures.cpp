#include "d3d_init.h"
#include "textures.h"

using namespace DirectX;

static void DebugFormat(const TexMetadata& m, const std::wstring& file) {
    std::wstringstream wss;
    wss << L"WIC loaded: " << file
        << L"\n  size = " << m.width << L"x" << m.height
        << L", mips = " << m.mipLevels
        << L", array = " << m.arraySize
        << L", fmt = " << (int)m.format << L"\n";
    OutputDebugStringW(wss.str().c_str());
}

ScratchImage LoadTextureFile(const std::wstring& filename)
{
    // 0) файл существует?
    if (GetFileAttributesW(filename.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wstringstream wss;
        wss << L"File not found: " << filename << L"\nWorking dir: " << cwd << L"\n";
        OutputDebugStringW(wss.str().c_str());
        throw std::runtime_error("Texture file not found");
    }

    ScratchImage out;

    // DDS — грузим напрямую
    if (filename.size() >= 4 &&
        _wcsicmp(filename.c_str() + filename.size() - 4, L".dds") == 0)
    {
        ThrowIfFailedEx(LoadFromDDSFile(filename.c_str(), DDS_FLAGS_NONE, nullptr, out),
            L"LoadFromDDSFile");
        return out;
    }

    // 1) WIC: PNG/JPG/BMP/.. без принудительных флагов
    ScratchImage wic;
    ThrowIfFailedEx(LoadFromWICFile(filename.c_str(), WIC_FLAGS_NONE, nullptr, wic),
        L"LoadFromWICFile");

    const TexMetadata meta = wic.GetMetadata();
    DebugFormat(meta, filename);

    // 2) Если уже 32‑бит — оставляем как есть
    switch (meta.format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        out = std::move(wic);
        return out;
    default:
        break;
    }

    // 3) Пытаемся конвертировать в RGBA8, если не вышло — в BGRA8
    HRESULT hr = Convert(wic.GetImages(), wic.GetImageCount(), meta,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        TEX_FILTER_DEFAULT, 0.5f, out);
    if (FAILED(hr)) {
        OutputDebugStringW(L"Convert -> RGBA8 failed, try BGRA8...\n");
        ThrowIfFailedEx(Convert(wic.GetImages(), wic.GetImageCount(), meta,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            TEX_FILTER_DEFAULT, 0.5f, out),
            L"Convert->BGRA8");
    }
    return out;
}

static D3D12_CPU_DESCRIPTOR_HANDLE SRV_CPU(UINT index) {
    auto base = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    base.ptr += SIZE_T(index) * g_srvInc;
    return base;
}
static D3D12_GPU_DESCRIPTOR_HANDLE SRV_GPU(UINT index) {
    auto base = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    base.ptr += UINT64(index) * g_srvInc;
    return base;
}

UINT RegisterTextureFromFile(const std::wstring& path)
{
    // 1) загрузка через DirectXTex
    ScratchImage img = LoadTextureFile(path);
    const TexMetadata& meta = img.GetMetadata();

    // 2) создаём DEFAULT ресурс под текстуру
    ComPtr<ID3D12Resource> tex;
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            meta.format, meta.width, (UINT)meta.height, (UINT16)meta.arraySize, (UINT16)meta.mipLevels);
        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        HR(g_device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex)));
    }

    // 3) upload + копирование всех мипов
    {
        UINT subCount = (UINT)img.GetImageCount();
        UINT64 uploadSize = GetRequiredIntermediateSize(tex.Get(), 0, subCount);
        ComPtr<ID3D12Resource> up;

        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize); // локальная переменная

        HR(g_device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE,
            &bufDesc,  // теперь можно передать адрес
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&up)));

        DX_BeginUpload();
        auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            tex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        g_uploadList->ResourceBarrier(1, &toCopy);

        std::vector<D3D12_SUBRESOURCE_DATA> subs;
        PrepareUpload(g_device.Get(), img.GetImages(), subCount, meta, subs);
        UpdateSubresources(g_uploadList.Get(), tex.Get(), up.Get(), 0, 0, subCount, subs.data());

        auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
            tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        g_uploadList->ResourceBarrier(1, &toSRV);
        DX_EndUploadAndFlush();
    }

    // 4) разместим SRV в куче (следующий свободный индекс)
    UINT idx = (UINT)g_textures.size();     // 0 для первой, 1 для второй и т.д.
    TextureGPU t{};
    t.res = tex;
    t.heapIndex = idx;
    t.cpu = SRV_CPU(idx);                   // baseCPU + idx * g_srvInc
    t.gpu = SRV_GPU(idx);                   // baseGPU + idx * g_srvInc

    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = meta.format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = (UINT)meta.mipLevels;

    g_device->CreateShaderResourceView(tex.Get(), &sd, t.cpu);
    g_textures.push_back(std::move(t));
    return idx; // texId
}
