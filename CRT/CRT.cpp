#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <chrono>
#include <wrl/client.h>
#include <algorithm>
#include <system_error>
#include <stdexcept>
#include "resource.h"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")

using namespace Microsoft::WRL;

HWND g_hwnd = nullptr;

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_rtv;
ComPtr<ID3D11VertexShader> g_vs;
ComPtr<ID3D11PixelShader> g_ps;
ComPtr<ID3D11Buffer> g_cb;
UINT g_renderWidth = 0;
UINT g_renderHeight = 0;


constexpr float kInitialPower = 1.0f;
constexpr float kPowerFadePerSecond = 1.0f;
constexpr float kChannelTimerDuration = 3.0f;

int g_channel = 0;
float g_power = kInitialPower;
bool g_poweringOff = false;
float g_channelTimer = kChannelTimerDuration;

struct CB
{
	float time;
	float width;
	float height;
	float channel;

	float power;
	float channelTimer;
	float padding[2];
};

void CheckHR(HRESULT hr, const char* msg)
{
	if (FAILED(hr))
		throw std::system_error{ hr, std::system_category(), msg };
}

struct ResourceData
{
	const void* data;
	DWORD size;
};

ResourceData LoadBinaryResource(HINSTANCE hInstance, int resourceId)
{
	if (!hInstance)
		throw std::invalid_argument("Invalid hInstance");

	HRSRC hRes = FindResource(hInstance, MAKEINTRESOURCE(resourceId), L"RT_RCDATA");
	if (!hRes)
		throw std::runtime_error("FindResource failed");

	HGLOBAL hData = LoadResource(hInstance, hRes);
	if (!hData)
		throw std::runtime_error("LoadResource failed");

	void* pData = LockResource(hData);
	if (!pData)
		throw std::runtime_error("LockResource failed");

	DWORD size = SizeofResource(hInstance, hRes);
	if (size == 0)
		throw std::runtime_error("SizeofResource returned 0");

	return { pData, size };
}

template<typename TShader, typename CreateFunc>
void LoadShaderFromResource(
	HINSTANCE hInstance,
	ID3D11Device* device,
	int resourceId,
	TShader** outShader,
	CreateFunc create)
{
	if (!device || !outShader)
		throw std::invalid_argument("Invalid argument");

	auto resource = LoadBinaryResource(hInstance, resourceId);

	HRESULT hr = create(device, resource.data, resource.size, outShader);
	if (FAILED(hr))
		throw std::runtime_error("Shader creation failed");
}



void LoadPixelShaderFromResource(HINSTANCE hInstance, ID3D11Device* device, ID3D11PixelShader** outShader)
{
	LoadShaderFromResource(
		hInstance,
		device,
		IDR_PIXEL_SHADER,
		outShader,
		[](ID3D11Device* dev, const void* data, SIZE_T size, ID3D11PixelShader** out)
		{
			return dev->CreatePixelShader(data, size, nullptr, out);
		});
}

void LoadVertexShaderFromResource(HINSTANCE hInstance, ID3D11Device* device, ID3D11VertexShader** outShader)
{
	LoadShaderFromResource(
		hInstance,
		device,
		IDR_VERTEX_SHADER,
		outShader,
		[](ID3D11Device* dev, const void* data, SIZE_T size, ID3D11VertexShader** out)
		{
			return dev->CreateVertexShader(data, size, nullptr, out);
		});
}

void ResizeD3D(UINT width, UINT height)
{
	if (width == g_renderWidth && height == g_renderHeight)
		return;

	g_context->OMSetRenderTargets(0, nullptr, nullptr);
	g_rtv.Reset();
	CheckHR(g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
		"ResizeBuffers failed");
	ComPtr<ID3D11Texture2D> bb;
	CheckHR(g_swapChain->GetBuffer(0, IID_PPV_ARGS(bb.GetAddressOf())), "GetBuffer failed");
	CheckHR(g_device->CreateRenderTargetView(bb.Get(), nullptr, &g_rtv), "RTV failed");
	g_context->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr);
	D3D11_VIEWPORT vp = {};
	vp.Width = static_cast<float>(width);
	vp.Height = static_cast<float>(height);
	vp.MaxDepth = 1;
	g_context->RSSetViewports(1, &vp);
	g_renderWidth = width;
	g_renderHeight = height;
}

void InitD3D()
{
	RECT rc; 
	GetClientRect(g_hwnd, &rc);

	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = rc.right;
	sd.BufferDesc.Height = rc.bottom;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;

	D3D_FEATURE_LEVEL fl;
	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

	CheckHR(D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
		&sd, &g_swapChain, &g_device, &fl, &g_context),
		"Device creation failed");

	ComPtr<ID3D11Texture2D> bb;
	CheckHR(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
		reinterpret_cast<void**>(bb.GetAddressOf())), "GetBuffer failed");

	CheckHR(g_device->CreateRenderTargetView(bb.Get(), nullptr, &g_rtv), "RTV failed");

	g_context->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr);

	D3D11_VIEWPORT vp = {};
	vp.Width = static_cast<float>(rc.right);
	vp.Height = static_cast<float>(rc.bottom);
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	g_context->RSSetViewports(1, &vp);
	g_renderWidth = rc.right;
	g_renderHeight = rc.bottom;
	g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ComPtr<ID3DBlob> vs, ps, err;
	
	LoadVertexShaderFromResource(GetModuleHandle(nullptr), g_device.Get(), &g_vs);
	LoadPixelShaderFromResource(GetModuleHandle(nullptr), g_device.Get(), &g_ps);

	g_context->VSSetShader(g_vs.Get(), nullptr, 0);
	g_context->PSSetShader(g_ps.Get(), nullptr, 0);

	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = sizeof(CB);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	CheckHR(g_device->CreateBuffer(&bd, nullptr, &g_cb), "CreateBuffer failed");

	g_context->PSSetConstantBuffers(0, 1, g_cb.GetAddressOf());
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch(m)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_SETCURSOR:
		SetCursor(NULL);
		return TRUE;
	case WM_KEYDOWN:
		switch(w)
		{	
			case VK_ESCAPE:
				PostQuitMessage(0);
				break;
			case VK_UP:
			{ 
				g_channel = (g_channel + 1) % 4; 
				g_channelTimer = kChannelTimerDuration;
				break;
			}
			case VK_DOWN: 
			{
				g_channel = (g_channel - 1 + 4) % 4; 
				g_channelTimer = kChannelTimerDuration; 
				break;
			}
			case VK_SPACE:
			{
				if (g_poweringOff && g_power <= 0.01f) {
					g_poweringOff = false;
					g_power = kInitialPower;
					g_channelTimer = kChannelTimerDuration;
				}
				else {
					g_poweringOff = true;
				}
				break;
			}
		}
	}

	return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) try
{
	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = h;
	wc.lpszClassName = L"CRT";
	if (!RegisterClass(&wc))
		throw std::system_error(GetLastError(), std::system_category(), "RegisterClass failed");

	g_hwnd = CreateWindow(wc.lpszClassName, L"CRT TV",
		WS_OVERLAPPEDWINDOW, 100, 100, 1920, 1080,
		nullptr, nullptr, h, nullptr);
	if (!g_hwnd)
		throw std::system_error(GetLastError(), std::system_category(), "CreateWindow failed");

	ShowWindow(g_hwnd, SW_SHOW);

	InitD3D();

	auto start = std::chrono::high_resolution_clock::now();
	auto prev = start;
	MSG msg = {};

	while (msg.message != WM_QUIT)
	{
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (msg.message == WM_QUIT)
			break;

		auto now = std::chrono::high_resolution_clock::now();
		float t = std::chrono::duration<float>(now - start).count();
		float dt = std::chrono::duration<float>(now - prev).count();
		prev = now;

		if (g_poweringOff && g_power > 0)
			g_power = std::max<float>(0.0f, g_power - kPowerFadePerSecond * dt);

		if (g_channelTimer > 0)
			g_channelTimer = std::max<float>(0.0f, g_channelTimer - 1.0f * dt);

		if (g_cb)
		{
			RECT rc; 
			GetClientRect(g_hwnd, &rc);
			if (IsIconic(g_hwnd) || rc.right <= 0 || rc.bottom <= 0)
			{
				WaitMessage();
				prev = std::chrono::high_resolution_clock::now();
				continue;
			}
			ResizeD3D(rc.right, rc.bottom);
			D3D11_MAPPED_SUBRESOURCE ms = {};
			CheckHR(g_context->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "Map failed");
			CB* cb = reinterpret_cast<CB*>(ms.pData);

			cb->time = t;
			cb->width = static_cast<float>(rc.right);
			cb->height = static_cast<float>(rc.bottom);
			cb->channel = static_cast<float>(g_channel);
			cb->power = g_power;
			cb->channelTimer = g_channelTimer;
			cb->padding[0] = cb->padding[1] = 0;

			g_context->Unmap(g_cb.Get(), 0);
		}

		float clear[4] = { 0,0,0,1 };
		g_context->ClearRenderTargetView(g_rtv.Get(), clear);
		g_context->Draw(3, 0);
		CheckHR(g_swapChain->Present(1, 0), "Present failed");
	}

	return 0;
}
catch (const std::exception& error)
{
	MessageBoxA(nullptr, error.what(), "CRT error", MB_OK | MB_ICONERROR);
	return 1;
}
