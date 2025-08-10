#include "textures.h"

ScratchImage LoadTextureFile(const std::wstring& filename)
{
    ScratchImage image;

    // DDS
    if (filename.size() >= 4 && _wcsicmp(filename.c_str() + filename.size() - 4, L".dds") == 0)
    {
        ThrowIfFailed(LoadFromDDSFile(filename.c_str(), DDS_FLAGS_NONE, nullptr, image));
    }
    else
    {
        // WIC (png, jpg, bmp, tiff и др.)
        ScratchImage tmp;
        ThrowIfFailed(LoadFromWICFile(filename.c_str(), WIC_FLAGS_NONE, nullptr, tmp));

        // Преобразуем в RGBA8 (чтобы точно был совместим с SRV)
        ThrowIfFailed(Convert(tmp.GetImages(), tmp.GetImageCount(), tmp.GetMetadata(),
            DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, 0.5f, image));
    }

    return image;
}
