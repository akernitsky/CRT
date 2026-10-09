#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <chrono>
#include <wrl/client.h>
#include <algorithm>
#include <system_error>
#include <stdexcept>
#include <future>
#include <memory>
#include <cmath>
#include <string>
#include "resource.h"
#include "Audio.h"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"ole32.lib")

using namespace Microsoft::WRL;

HWND g_hwnd = nullptr;
auto g_audio = std::make_unique<Audio>();
bool g_audioLoading = true;
bool g_dirty = true;
bool g_audioAvailable = false;

struct ComApartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_rtv;
ComPtr<ID3D11VertexShader> g_vs;
ComPtr<ID3D11PixelShader> g_ps;
ComPtr<ID3D11Buffer> g_cb;
UINT g_renderWidth = 0;
UINT g_renderHeight = 0;


constexpr int kBroadcastCount = 10;
constexpr int kRoomChannel = kBroadcastCount;
constexpr int kChannelCount = kBroadcastCount + 1;
constexpr int kChannelPrograms[kBroadcastCount] = { 5, 0, 8, 3, 1, 6, 9, 2, 7, 4 };
constexpr float kInitialPower = 1.0f;
constexpr float kPowerFadePerSecond = 1.0f;
constexpr float kChannelTimerDuration = 3.0f;

int g_channel = 0;
int g_roomChannel = 0;
float g_roomPower = kInitialPower;
bool g_roomPoweringOff = false;
float g_power = kInitialPower;
bool g_poweringOff = false;
float g_channelTimer = kChannelTimerDuration;
constexpr int kMaxVolume = 40;
int g_volume = kMaxVolume;
int g_previousVolume = kMaxVolume;
float g_volumeTimer = 0;

struct CB
{
	float time;
	float width;
	float height;
	float channel;

	float power;
	float channelTimer;
	float clockSeconds;
	float channelNumber;
	float volume;
	float volumeTimer;
	float reserved[2];
};

void ApplyVolume(HWND window)
{
	g_audio->SetVolume(static_cast<float>(g_volume) / kMaxVolume);
	g_volumeTimer = 3.0f;
	SetWindowText(window, g_audioLoading ? L"CRT TV - loading audio" :
		!g_audioAvailable ? L"CRT TV - audio unavailable" :
		g_volume == 0 ? L"CRT TV - muted (M)" : L"CRT TV");
}

void CheckHR(HRESULT hr, const char* msg)
{
	if (FAILED(hr))
		throw std::system_error{ hr, std::system_category(), msg };
}

#include "Room.h"
std::unique_ptr<Room> g_room;

int ActiveBroadcast()
{
    return g_channel == kRoomChannel ? g_roomChannel : g_channel;
}

float ActivePower()
{
    return g_power * (g_channel == kRoomChannel ? g_roomPower : 1.0f);
}

void UpdateTitle()
{
    const wchar_t* title = g_channel == kRoomChannel
        ? L"CRT TV - Room 11 | P: TV power | A/D: channel | Up/Down: volume | M: mute | Space: whole view"
        : L"CRT TV";
    std::wstring text = title;
    if (g_channel == kRoomChannel && g_roomPoweringOff) text += L" - TV off";
    if (g_audioLoading) text += L" - loading audio";
    else if (!g_audioAvailable) text += L" - audio unavailable";
    else if (g_volume == 0) text += L" - muted (M)";
    SetWindowText(g_hwnd, text.c_str());
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
	case WM_PAINT:
        {
            PAINTSTRUCT paint;
            BeginPaint(h, &paint);
            EndPaint(h, &paint);
            g_dirty = true;
            return 0;
        }
    case WM_SIZE:
    case WM_DISPLAYCHANGE:
        g_dirty = true;
        break;
    case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_SETCURSOR:
		SetCursor(NULL);
		return TRUE;
	case WM_KEYDOWN:
		g_dirty = true;
		switch(w)
		{
			case VK_ESCAPE:
				PostQuitMessage(0);
				break;
			case VK_RIGHT:
			{
				g_channel = (g_channel + 1) % kChannelCount;
				UpdateTitle();
				g_channelTimer = kChannelTimerDuration;
				if (g_power > 0 && !g_poweringOff) g_audio->Play(Audio::Channel);
				break;
			}
			case VK_LEFT:
			{
				g_channel = (g_channel - 1 + kChannelCount) % kChannelCount;
				UpdateTitle();
				g_channelTimer = kChannelTimerDuration;
				if (g_power > 0 && !g_poweringOff) g_audio->Play(Audio::Channel);
				break;
			}
			case 'A':
			case 'D':
				if (g_channel == kRoomChannel) {
					g_roomChannel = (g_roomChannel + (w == 'D' ? 1 : kBroadcastCount - 1)) % kBroadcastCount;
					g_channelTimer = kChannelTimerDuration;
					if (ActivePower() > 0 && !g_poweringOff && !g_roomPoweringOff) g_audio->Play(Audio::Channel);
				}
				break;
			case 'P':
				if (g_channel == kRoomChannel && !(l & (1LL << 30))) {
					g_roomPoweringOff = !g_roomPoweringOff;
					if (!g_roomPoweringOff) {
						g_roomPower = kInitialPower;
						g_channelTimer = kChannelTimerDuration;
					}
					if (g_power > 0 && !g_poweringOff)
						g_audio->Play(g_roomPoweringOff ? Audio::PowerOff : Audio::PowerOn);
					UpdateTitle();
				}
				break;
			case VK_UP:
			case VK_DOWN:
				g_volume = std::clamp(g_volume + (w == VK_UP ? 1 : -1), 0, kMaxVolume);
				if (g_volume > 0) g_previousVolume = g_volume;
				ApplyVolume(h);
				UpdateTitle();
				break;
			case VK_SPACE:
			{
				if (l & (1LL << 30)) break;
				if (g_poweringOff && g_power <= 0.01f) {
					g_poweringOff = false;
					g_power = kInitialPower;
					g_channelTimer = kChannelTimerDuration;
					g_audio->Play(Audio::PowerOn);
				}
				else {
					if (!g_poweringOff) g_audio->Play(Audio::PowerOff);
					g_poweringOff = true;
				}
				break;
			}
			case 'M':
				if (!(l & (1LL << 30)))
				{
					if (g_volume > 0) { g_previousVolume = g_volume; g_volume = 0; }
					else g_volume = g_previousVolume;
					ApplyVolume(h);
					UpdateTitle();
				}
				break;
		}
	}

	return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) try
{
    // Keep the MTA alive until the published audio engine has been destroyed.
    ComApartment apartment;
    struct AudioLifetime { ~AudioLifetime() { g_audio.reset(); } } audioLifetime;
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = h;
	wc.lpszClassName = L"CRT";
	wc.hIcon = LoadIcon(h, MAKEINTRESOURCE(IDI_CRT));
	wc.hIconSm = LoadIcon(h, MAKEINTRESOURCE(IDI_SMALL));
	if (!RegisterClassEx(&wc))
		throw std::system_error(GetLastError(), std::system_category(), "RegisterClass failed");

	g_hwnd = CreateWindow(wc.lpszClassName, L"CRT TV",
		WS_OVERLAPPEDWINDOW, 100, 100, 1920, 1080,
		nullptr, nullptr, h, nullptr);
	if (!g_hwnd)
		throw std::system_error(GetLastError(), std::system_category(), "CreateWindow failed");

	ShowWindow(g_hwnd, SW_SHOW);

	InitD3D();
    // The worker owns a separate Audio object until the future publishes it.
    // Input can safely use the inactive object while samples are being generated.
    auto audioInit = std::async(std::launch::async, []() -> std::unique_ptr<Audio> {
        ComApartment workerApartment;
        if (FAILED(workerApartment.result)) return nullptr;
        try
        {
            auto audio = std::make_unique<Audio>();
            if (audio->Init()) return audio;
        }
        catch (...) { /* Audio failure must not prevent silent operation. */ }
        return nullptr;
    });
    SetWindowText(g_hwnd, L"CRT TV - loading audio");

	auto start = std::chrono::steady_clock::now();
	auto prev = start;
	MSG msg = {};
    bool occluded = false;
    auto nextOcclusionTest = start;
    int lastRenderedSecond = -1;

	while (msg.message != WM_QUIT)
	{
		auto now = std::chrono::steady_clock::now();
		float t = std::chrono::duration<float>(now - start).count();
		float dt = std::chrono::duration<float>(now - prev).count();
		prev = now;

        const bool hadChannelOSD = g_channelTimer > 0;
        const bool hadVolumeOSD = g_volumeTimer > 0;
        const float previousPower = g_power;
        const float previousRoomPower = g_roomPower;
		if (g_roomPoweringOff && g_roomPower > 0)
			g_roomPower = std::max<float>(0.0f, g_roomPower - kPowerFadePerSecond * dt);
		if (g_poweringOff && g_power > 0)
			g_power = std::max<float>(0.0f, g_power - kPowerFadePerSecond * dt);

		if (g_channelTimer > 0)
			g_channelTimer = std::max<float>(0.0f, g_channelTimer - 1.0f * dt);
		g_volumeTimer = std::max<float>(0.0f, g_volumeTimer - dt);

		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (msg.message == WM_QUIT)
			break;

        if (g_audioLoading && audioInit.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto audio = audioInit.get();
            g_audioAvailable = audio != nullptr;
            if (audio) g_audio = std::move(audio);
            g_audioLoading = false;
            g_audio->SetVolume(static_cast<float>(g_volume) / kMaxVolume);
            UpdateTitle();
            if (ActivePower() > 0 && !g_poweringOff
                && (g_channel != kRoomChannel || !g_roomPoweringOff)) g_audio->Play(Audio::PowerOn);
        }

		SYSTEMTIME localTime;
		GetLocalTime(&localTime);
		const int clockSeconds = localTime.wHour * 3600 + localTime.wMinute * 60 + localTime.wSecond;
		g_audio->Update(kChannelPrograms[ActiveBroadcast()], ActivePower(), clockSeconds);

        const int program = kChannelPrograms[ActiveBroadcast()];
        g_dirty = g_dirty || previousPower != g_power
            || (g_channel == kRoomChannel && previousRoomPower != g_roomPower)
            || hadChannelOSD != (g_channelTimer > 0) || hadVolumeOSD != (g_volumeTimer > 0);
        const bool roomFading = g_channel == kRoomChannel && g_roomPoweringOff && g_roomPower > 0;
        const bool clockActive = program == 8 && ActivePower() > 0;
        const bool animated = g_power > 0 && (g_poweringOff || roomFading
            || (ActivePower() > 0 && (program < 3 || program == 9)));
        if (clockActive && clockSeconds != lastRenderedSecond) g_dirty = true;

        // Wake for the next visible change or audio tick, otherwise sleep until input.
        DWORD waitMs = INFINITE;
        auto wakeIn = [&](float seconds) {
            waitMs = (std::min)(waitMs, static_cast<DWORD>((std::max)(1.0f, std::ceil(seconds * 1000))));
        };
        if (g_audioLoading) waitMs = 16;
        if (g_channelTimer > 0) wakeIn(g_channelTimer);
        if (g_volumeTimer > 0) wakeIn(g_volumeTimer);
        if (clockActive) waitMs = (std::min)(waitMs, static_cast<DWORD>(1000 - localTime.wMilliseconds));
        if (g_poweringOff && g_power > 0) waitMs = (std::min)(waitMs, DWORD(16));
        if (roomFading) waitMs = (std::min)(waitMs, DWORD(16));
        auto waitForInput = [&](DWORD timeout) {
            MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        };

		if (g_cb)
		{
			RECT rc;
			GetClientRect(g_hwnd, &rc);
			if (IsIconic(g_hwnd) || rc.right <= 0 || rc.bottom <= 0)
			{
				waitForInput(waitMs);
				continue;
			}
            if (occluded)
            {
                if (now >= nextOcclusionTest)
                {
                    const HRESULT status = g_swapChain->Present(0, DXGI_PRESENT_TEST);
                    CheckHR(status, "Presentation visibility test failed");
                    occluded = status == DXGI_STATUS_OCCLUDED;
                    nextOcclusionTest = now + std::chrono::milliseconds(250);
                    if (!occluded) g_dirty = true;
                }
                if (occluded)
                {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(nextOcclusionTest - now).count();
                    waitForInput((std::min)(waitMs, static_cast<DWORD>((std::max)(1LL, remaining))));
                    continue;
                }
            }
            if (rc.right != g_renderWidth || rc.bottom != g_renderHeight) g_dirty = true;
            if (!g_dirty && !animated)
            {
                waitForInput(waitMs);
                continue;
            }
			ResizeD3D(rc.right, rc.bottom);
			const bool inRoom = g_channel == kRoomChannel;
			if (inRoom && !g_room) {
				g_room = std::make_unique<Room>();
				g_room->Init(g_device.Get());
			}
			g_context->VSSetShader(g_vs.Get(), nullptr, 0);
			g_context->PSSetShader(g_ps.Get(), nullptr, 0);
			g_context->PSSetConstantBuffers(0, 1, g_cb.GetAddressOf());
			ID3D11RenderTargetView* target = inRoom ? g_room->BroadcastTarget() : g_rtv.Get();
			g_context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport = {0,0,
				static_cast<float>(inRoom ? Room::BroadcastWidth : g_renderWidth),
				static_cast<float>(inRoom ? Room::BroadcastHeight : g_renderHeight),0,1};
			g_context->RSSetViewports(1, &viewport);
			D3D11_MAPPED_SUBRESOURCE ms = {};
			CheckHR(g_context->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "Map failed");
			CB* cb = reinterpret_cast<CB*>(ms.pData);

			cb->time = t;
			cb->width = viewport.Width;
			cb->height = viewport.Height;
			cb->channel = static_cast<float>(program);
			cb->power = inRoom ? g_roomPower : g_power;
			cb->channelTimer = g_channelTimer;
			cb->clockSeconds = static_cast<float>(clockSeconds);
			cb->channelNumber = static_cast<float>(ActiveBroadcast());
			cb->volume = static_cast<float>(g_volume) / kMaxVolume;
			cb->volumeTimer = g_volumeTimer;
			cb->reserved[0] = cb->reserved[1] = 0;

			g_context->Unmap(g_cb.Get(), 0);
		}

        // The full-screen triangle writes every pixel, including black borders.
		g_context->Draw(3, 0);
		if (g_channel == kRoomChannel)
			g_room->Draw(g_device.Get(), g_context.Get(), g_rtv.Get(), g_renderWidth, g_renderHeight, g_power, g_roomPower);
        const HRESULT status = g_swapChain->Present(1, 0);
        CheckHR(status, "Present failed");
        occluded = status == DXGI_STATUS_OCCLUDED;
        if (occluded) nextOcclusionTest = now + std::chrono::milliseconds(250);
        g_dirty = false;
        lastRenderedSecond = clockSeconds;
	}

	return 0;
}
catch (const std::exception& error)
{
	MessageBoxA(nullptr, error.what(), "CRT error", MB_OK | MB_ICONERROR);
	return 1;
}
