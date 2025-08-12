#include "d3d_init.h"
#include "uploader.h"

void RenderFrame()
{
	IM_ASSERT(ImGui::GetCurrentContext() != nullptr);
	HR(g_alloc[g_frameIndex]->Reset());
	HR(g_cmdList->Reset(g_alloc[g_frameIndex].Get(), g_pso.Get()));

	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_cmdList->ResourceBarrier(1, &toRT);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		g_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		g_frameIndex, g_rtvInc);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();

	g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	g_cmdList->RSSetViewports(1, &g_viewport);
	g_cmdList->RSSetScissorRects(1, &g_scissor);

	const FLOAT clear[4] = { 0.78f, 0.949f, 0.996f, 1.0f };
	g_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);
	g_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// dt + ввод
	static LARGE_INTEGER s_freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	static LARGE_INTEGER s_prev = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t; }();
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	float dt = float(double(now.QuadPart - s_prev.QuadPart) / double(s_freq.QuadPart));
	s_prev = now;
	UpdateInput(dt);
	g_cam.UpdateView();

	// ВАЖНО: сначала RS, затем привязка shader-visible heaps
	g_cmdList->SetGraphicsRootSignature(g_rootSig.Get());

	ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
	g_cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

	g_cmdList->SetPipelineState(g_pso.Get());
	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (size_t i = 0; i < g_entities.size(); ++i)
	{
		const Entity& e = g_entities[i];
		const MeshGPU& m = g_meshes[e.meshId];
		const TextureGPU& t = g_textures[e.texId];

		// world
		XMMATRIX S = XMMatrixScaling(e.scale.x, e.scale.y, e.scale.z);
		XMMATRIX Rx = XMMatrixRotationX(XMConvertToRadians(e.rotDeg.x));
		XMMATRIX Ry = XMMatrixRotationY(XMConvertToRadians(e.rotDeg.y));
		XMMATRIX Rz = XMMatrixRotationZ(XMConvertToRadians(e.rotDeg.z));
		XMMATRIX T = XMMatrixTranslation(e.pos.x, e.pos.y, e.pos.z);
		XMMATRIX M = S * Rx * Ry * Rz * T;

		VSConstants c{};
		XMStoreFloat4x4(&c.mvp, XMMatrixTranspose(M * g_cam.View() * g_cam.Proj()));

		// свой слот в CB (256‑byte aligned)
		UINT offset = UINT(i) * CB_SIZE_ALIGNED;
		std::memcpy(g_cbPtr + offset, &c, sizeof(c));
		g_cmdList->SetGraphicsRootConstantBufferView(0, g_cb->GetGPUVirtualAddress() + offset);

		// текстура
		g_cmdList->SetGraphicsRootDescriptorTable(1, t.gpu);

		g_cmdList->IASetVertexBuffers(0, 1, &m.vbv);
		g_cmdList->IASetIndexBuffer(&m.ibv);
		g_cmdList->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);
	}

	// SAMPLER (s0) — выбираем из 15 пресетов
	int idx = g_uiAddrMode * 3 + g_uiFilter;
	CD3DX12_GPU_DESCRIPTOR_HANDLE sampGpu(
		g_sampHeap->GetGPUDescriptorHandleForHeapStart(),
		idx, g_sampInc);
	g_cmdList->SetGraphicsRootDescriptorTable(2, sampGpu);


	// --- ImGui frame begin ---
	ImGui_ImplWin32_NewFrame();     // платформа
	ImGui_ImplDX12_NewFrame();      // РЕНДЕРЕР ← обязательно до ImGui::NewFrame()
	ImGui::NewFrame();

	// твой UI
	BuildEditorUI();

	ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		SaveScene(L"assets\\scenes\\autosave.scene");
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
		LoadScene(L"assets\\scenes\\autosave.scene");
	ImGui::Render();

	// рендер UI (включаем imgui-heap)
	ID3D12DescriptorHeap* imguiHeaps[] = { g_imguiHeap.Get() };
	g_cmdList->SetDescriptorHeaps(1, imguiHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList.Get());

	// вернуть свою кучу, если нужно
	ID3D12DescriptorHeap* sceneHeaps[] = { g_srvHeap.Get() };
	g_cmdList->SetDescriptorHeaps(1, sceneHeaps);


	auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_cmdList->ResourceBarrier(1, &toPresent);

	HR(g_cmdList->Close());

	ID3D12CommandList* lists[] = { g_cmdList.Get() };
	g_cmdQueue->ExecuteCommandLists(1, lists);
	HR(g_swapChain->Present(1, 0));

	const UINT64 fenceToWait = g_fenceValue++;
	HR(g_cmdQueue->Signal(g_fence.Get(), fenceToWait));
	if (g_fence->GetCompletedValue() < fenceToWait) {
		HR(g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent));
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}


void WaitForGPU() {
	if (!g_fence || !g_cmdQueue) return;

	const UINT64 fenceToWait = g_fenceValue++;
	HR(g_cmdQueue->Signal(g_fence.Get(), fenceToWait));

	// Event должен существовать
	if (!g_fenceEvent) {
		g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!g_fenceEvent) {
			HR(HRESULT_FROM_WIN32(GetLastError()));
		}
	}

	HR(g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent));
	WaitForSingleObject(g_fenceEvent, INFINITE);
}

void UpdateInput(float dt)
{
	if (!g_appActive) return;

	float speed = g_cam.moveSpeed * ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4.0f : 1.0f);
	if (GetAsyncKeyState('W') & 0x8000) g_cam.Walk(+speed * dt);
	if (GetAsyncKeyState('S') & 0x8000) g_cam.Walk(-speed * dt);
	if (GetAsyncKeyState('D') & 0x8000) g_cam.Strafe(+speed * dt);
	if (GetAsyncKeyState('A') & 0x8000) g_cam.Strafe(-speed * dt);
	if (GetAsyncKeyState('Q') & 0x8000) g_cam.UpDown(-speed * dt);
	if (GetAsyncKeyState('E') & 0x8000) g_cam.UpDown(+speed * dt);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return 0;

	switch (msg) {
	case WM_DESTROY: 
		DX_Shutdown();
		PostQuitMessage(0); 
		return 0;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) { DestroyWindow(hWnd); }
		break;
	case WM_SIZE:
	{
		if (wParam == SIZE_MINIMIZED) return 0;
		UINT w = LOWORD(lParam), h = HIWORD(lParam);
		if (w == 0 || h == 0) return 0;

		if (g_dxReady) {
			DX_Resize(w, h);
		}
		else {
			// запомним — применим после InitD3D12
			g_pendingW = w; g_pendingH = h;
		}
		return 0;
	}
	case WM_ACTIVATE:
	{
		const WORD code = LOWORD(wParam);
		if (code == WA_INACTIVE) {
			g_appActive = false;
			g_mouseLook = false;
			ReleaseCapture();
			ClipCursor(nullptr);
			ShowCursor(TRUE);
			g_mouseHasPrev = false; // чтобы не было рывка при возврате
		}
		else {
			g_appActive = true;
			// курсор показываем; захватывать начнём только по ЛКМ
			ShowCursor(TRUE);
		}
		return 0;
	}
	case WM_SETFOCUS:
		g_appActive = true;
		return 0;

	case WM_KILLFOCUS:
		g_appActive = false;
		g_mouseLook = false;
		ReleaseCapture();
		ClipCursor(nullptr);
		ShowCursor(TRUE);
		g_mouseHasPrev = false;
		return 0;

	case WM_LBUTTONDOWN:
		g_mouseLook = true;
		SetCapture(hWnd);

		g_lastMouse.x = GET_X_LPARAM(lParam);
		g_lastMouse.y = GET_Y_LPARAM(lParam);
		g_mouseHasPrev = true;
		return 0;
	case WM_LBUTTONUP:
		g_mouseLook = false;
		ReleaseCapture();
		g_mouseHasPrev = false;
		return 0;
	case WM_MOUSEMOVE:
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureMouse) break;

		if (g_mouseLook)
		{
			POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			if (!g_mouseHasPrev) {
				// если по какой-то причине не было базовой точки — установим и выходим
				g_lastMouse = p;
				g_mouseHasPrev = true;
				return 0;
			}

			int dx = p.x - g_lastMouse.x;
			int dy = p.y - g_lastMouse.y;
			g_lastMouse = p; // обновляем «предыдущую» на текущую

			g_cam.AddYawPitch(dx * g_cam.mouseSens, -dy * g_cam.mouseSens);
			g_cam.UpdateView();
			return 0;
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		int delta = GET_WHEEL_DELTA_WPARAM(wParam); // 120 за «щелчок»
		g_cam.fovY = std::clamp(g_cam.fovY - float(delta) * 0.0005f, XM_PI / 12.0f, XM_PI / 1.2f);
		g_cam.SetLens(g_cam.fovY, g_cam.aspect, g_cam.zn, g_cam.zf);
		return 0;
	}

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
	const wchar_t* kClassName = L"BitchEngine";
	wchar_t window_name[25] = L"bitchengine FPS: ";

	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClassName;
	RegisterClassExW(&wc);

	RECT rc{ 0,0,1280,720 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

	g_hWnd = CreateWindowExW(
		0, kClassName, window_name,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hInst, nullptr);

	ShowWindow(g_hWnd, nCmdShow);
	InitD3D12(g_hWnd, 1280, 720);

	LARGE_INTEGER freq{};
	QueryPerformanceFrequency(&freq);

	LARGE_INTEGER prev{};
	QueryPerformanceCounter(&prev);

	double accTitle = 0.0;
	int frameCounter = 0;
	double fpsShown = 0.0;

	CoUninitialize();

	MSG msg{};
	for (;;) {
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) return (int)msg.wParam;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		double dt = double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart);
		prev = now;

		accTitle += dt;
		frameCounter++;

		if (accTitle >= 0.5) {
			fpsShown = frameCounter / accTitle;
			frameCounter = 0;
			accTitle = 0.0;

			std::wstring title = std::format(L"bitchengine FPS: {}", (int)std::round(fpsShown));
			SetWindowTextW(g_hWnd, title.c_str());
		}
		RenderFrame();
		Sleep(1);
	}
}