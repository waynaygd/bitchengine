#pragma once
#include "someshit.h"

void CreateDeviceAndQueue();
void CreateFenceAndUploadList();
void CreateSwapChainAndRTVs(HWND hWnd, UINT width, UINT height);
void CreateDepthAndDSV(UINT width, UINT height);
void CreateSRVHeap(UINT num = 1);
void CreateFrameCommandLists();
void CreateCB();

void BeginUpload();        // Reset upload allocator + list
void EndUploadAndFlush();  // Close + Execute + Wait + clear keep-alive

void UploadOBJ(const std::wstring& path, MeshGPU& out);
void UploadTexture(const std::wstring& path, Microsoft::WRL::ComPtr<ID3D12Resource>& outTex);
void CreateSRVForTexture(ID3D12Resource* tex);

void CreateRootSigAndPSO();
void InitCamera(UINT width, UINT height);

void InitD3D12(HWND hWnd, UINT width, UINT height);

inline void KeepAlive(const Microsoft::WRL::ComPtr<ID3D12Resource>& r)
{
    if (r) g_uploadKeepAlive.push_back(r);
}

void CreateDefaultBufferUpload(
    ID3D12Device* dev, ID3D12GraphicsCommandList* cmd,
    const void* data, size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outDefault,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outUpload,
    D3D12_RESOURCE_STATES finalState);

