#pragma once
#include "DirectXTex.h"
#include <stdexcept>
#include <string>
#include <wrl/client.h>
#include <comdef.h>
#include <sstream>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

ScratchImage LoadTextureFile(const std::wstring& filename);

inline void ThrowIfFailedEx(HRESULT hr, const wchar_t* where)
{
    if (FAILED(hr)) {
        _com_error err(hr);
        std::wstringstream wss;
        wss << L"[HRESULT 0x" << std::hex << hr << L"] " << where
            << L"\n" << err.ErrorMessage() << L"\n";
        OutputDebugStringW(wss.str().c_str());
        throw std::runtime_error("DX call failed");
    }
}