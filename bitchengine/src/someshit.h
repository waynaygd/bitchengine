#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <string>
#include <format> 

using Microsoft::WRL::ComPtr;

static const UINT kFrameCount = 2;

HWND g_hWnd = nullptr;
ComPtr<IDXGIFactory7>        g_factory;
ComPtr<ID3D12Device>         g_device;
ComPtr<ID3D12CommandQueue>   g_cmdQueue;
ComPtr<IDXGISwapChain3>      g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
UINT                         g_rtvInc = 0;
ComPtr<ID3D12Resource>       g_backBuffers[kFrameCount];
ComPtr<ID3D12CommandAllocator> g_alloc[kFrameCount];
ComPtr<ID3D12GraphicsCommandList> g_cmdList;
ComPtr<ID3D12Fence>          g_fence;
HANDLE                       g_fenceEvent = NULL;
UINT64                       g_fenceValue = 0;
UINT                         g_frameIndex = 0;
