#pragma once
#include "DirectXTex.h"
#include <stdexcept>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

ScratchImage LoadTextureFile(const std::wstring& filename);

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("DX call failed");
}