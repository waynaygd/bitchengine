#pragma once
#include <windows.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <wrl.h> // Microsoft::WRL::ComPtr


using Microsoft::WRL::ComPtr;

ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<IDXGISwapChain3> swapChain;
ComPtr<ID3D12DescriptorHeap> rtvHeap;
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12GraphicsCommandList> commandList;
ComPtr<ID3D12Resource> backBuffer;

UINT rtvDescriptorSize;
