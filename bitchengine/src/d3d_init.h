// d3d_init.h
#pragma once
#include "someshit.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

// ─── imgui ──────────────────────────────────────────────────────────────────
void InitImGui(HWND hWnd);
void ShutdownImGui();
void BuildEditorUI();

// ─── низкоуровневые шаги ────────────────────────────────────────────────────
void DX_CreateDeviceAndQueue();
void DX_CreateFenceAndUploadList();
void DX_CreateSwapchain(HWND hWnd, UINT w, UINT h);
void DX_CreateRTVs();
void DX_CreateDepth(UINT w, UINT h);
void DX_CreateSRVHeap(UINT numDescriptors = 1);
void DX_CreateImGuiHeap();
void DX_CreateFrameCmdObjects();
void CreateCB();

// ─── upload-фаза (один список команд) ───────────────────────────────────────
void DX_BeginUpload();
void DX_EndUploadAndFlush();

// ─── пайплайн и камера ──────────────────────────────────────────────────────
void DX_CreateRootSigAndPSO();     // твой текущий RS/PSO
void DX_InitCamera(UINT w, UINT h);

// ─── «крючки» для ассетов (пока пусто) ──────────────────────────────────────
void DX_LoadAssets();  // здесь позже вызовешь UploadOBJ/UploadTexture/…
// (сейчас можно оставить пустым — всё соберётся)

// ─── главный оркестратор ────────────────────────────────────────────────────
void InitD3D12(HWND hWnd, UINT w, UINT h);

void DX_Resize(UINT w, UINT h);
void DX_Shutdown();

void DX_CreateSamplerHeap();
static D3D12_FILTER ToFilter(int uiFilter);
static D3D12_TEXTURE_ADDRESS_MODE ToAddress(int uiAddr);
void DX_FillSamplers();
