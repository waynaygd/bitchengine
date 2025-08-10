#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <comdef.h>
#include <d3dx12.h>

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

#define HR(x) ThrowIfFailed((x), #x, __FILE__, __LINE__)

inline void CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    const void* data, size_t bytes,
    ComPtr<ID3D12Resource>& defaultBuf,
    ComPtr<ID3D12Resource>& uploadBuf,
    D3D12_RESOURCE_STATES finalState)
{
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);

    // DEFAULT Ч из COMMON
    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    HR(device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(defaultBuf.ReleaseAndGetAddressOf())));

    // UPLOAD
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    HR(device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(uploadBuf.ReleaseAndGetAddressOf())));

    // map & memcpy
    void* mapped = nullptr; CD3DX12_RANGE noRead(0, 0);
    HR(uploadBuf->Map(0, &noRead, &mapped));
    std::memcpy(mapped, data, bytes);
    uploadBuf->Unmap(0, nullptr);

    // COMMON -> COPY_DEST
    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuf.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1, &toCopy);

    // copy
    cmd->CopyBufferRegion(defaultBuf.Get(), 0, uploadBuf.Get(), 0, bytes);

    // COPY_DEST -> final
    auto toFinal = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    cmd->ResourceBarrier(1, &toFinal);
}
