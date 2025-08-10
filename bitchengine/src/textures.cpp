#include <DirectXTex.h>
#include <comdef.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <windows.h>

using namespace DirectX;

inline void ThrowIfFailedEx(HRESULT hr, const wchar_t* where) {
    if (FAILED(hr)) {
        _com_error err(hr);
        std::wstringstream wss;
        wss << L"[HRESULT 0x" << std::hex << hr << L"] " << where
            << L"\n" << err.ErrorMessage() << L"\n";
        OutputDebugStringW(wss.str().c_str());
        throw std::runtime_error("DX call failed");
    }
}

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
