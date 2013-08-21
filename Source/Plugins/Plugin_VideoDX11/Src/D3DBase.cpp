// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "StringUtil.h"
#include "VideoConfig.h"

#include "D3DBase.h"
#include "D3DTexture.h"
#include "GfxState.h"

namespace DX11
{

HINSTANCE hD3DCompilerDll = NULL;
D3DREFLECT PD3DReflect = NULL;
int d3dcompiler_dll_ref = 0;

HINSTANCE hD3DXDll = NULL;
D3DX11COMPILEFROMMEMORYTYPE PD3DX11CompileFromMemory = NULL;
D3DX11FILTERTEXTURETYPE PD3DX11FilterTexture = NULL;
D3DX11SAVETEXTURETOFILEATYPE PD3DX11SaveTextureToFileA = NULL;
D3DX11SAVETEXTURETOFILEWTYPE PD3DX11SaveTextureToFileW = NULL;
int d3dx_dll_ref = 0;

CREATEDXGIFACTORY PCreateDXGIFactory = NULL;
HINSTANCE hDXGIDll = NULL;
int dxgi_dll_ref = 0;

typedef HRESULT (WINAPI* D3D11CREATEDEVICEANDSWAPCHAIN)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, CONST D3D_FEATURE_LEVEL*, UINT, UINT, CONST DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
D3D11CREATEDEVICE PD3D11CreateDevice = NULL;
D3D11CREATEDEVICEANDSWAPCHAIN PD3D11CreateDeviceAndSwapChain = NULL;
HINSTANCE hD3DDll = NULL;
int d3d_dll_ref = 0;

namespace D3D
{

ID3D11Device* device = NULL;
ID3D11DeviceContext* context = NULL;
IDXGISwapChain* swapchain = NULL;
D3D_FEATURE_LEVEL featlevel;
D3DTexture2D* backbuf = NULL;
HWND hWnd;

std::vector<DXGI_SAMPLE_DESC> aa_modes; // supported AA modes of the current adapter

bool bgra_textures_supported;

#define NUM_SUPPORTED_FEATURE_LEVELS 3
const D3D_FEATURE_LEVEL supported_feature_levels[NUM_SUPPORTED_FEATURE_LEVELS] = {
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0
};

unsigned int xres, yres;

bool bFrameInProgress = false;

HRESULT LoadDXGI()
{
	if (dxgi_dll_ref++ > 0) return S_OK;

	if (hDXGIDll) return S_OK;
	hDXGIDll = LoadLibraryA("dxgi.dll");
	if (!hDXGIDll)
	{
		MessageBoxA(NULL, "Failed to load dxgi.dll", "Critical error", MB_OK | MB_ICONERROR);
		--dxgi_dll_ref;
		return E_FAIL;
	}
	PCreateDXGIFactory = (CREATEDXGIFACTORY)GetProcAddress(hDXGIDll, "CreateDXGIFactory");
	if (PCreateDXGIFactory == NULL) MessageBoxA(NULL, "GetProcAddress failed for CreateDXGIFactory!", "Critical error", MB_OK | MB_ICONERROR);

	return S_OK;
}

HRESULT LoadD3D()
{
	if (d3d_dll_ref++ > 0) return S_OK;

	if (hD3DDll) return S_OK;
	hD3DDll = LoadLibraryA("d3d11.dll");
	if (!hD3DDll)
	{
		MessageBoxA(NULL, "Failed to load d3d11.dll", "Critical error", MB_OK | MB_ICONERROR);
		--d3d_dll_ref;
		return E_FAIL;
	}
	PD3D11CreateDevice = (D3D11CREATEDEVICE)GetProcAddress(hD3DDll, "D3D11CreateDevice");
	if (PD3D11CreateDevice == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3D11CreateDevice!", "Critical error", MB_OK | MB_ICONERROR);

	PD3D11CreateDeviceAndSwapChain = (D3D11CREATEDEVICEANDSWAPCHAIN)GetProcAddress(hD3DDll, "D3D11CreateDeviceAndSwapChain");
	if (PD3D11CreateDeviceAndSwapChain == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3D11CreateDeviceAndSwapChain!", "Critical error", MB_OK | MB_ICONERROR);

	return S_OK;
}

HRESULT LoadD3DX()
{
	if (d3dx_dll_ref++ > 0) return S_OK;
	if (hD3DXDll) return S_OK;

	// try to load D3DX11 first to check whether we have proper runtime support
	// try to use the dll the backend was compiled against first - don't bother about debug runtimes
	hD3DXDll = LoadLibraryA(D3DX11_DLL_A);
	if (!hD3DXDll)
	{
		// if that fails, use the dll which should be available in every SDK which officially supports DX11.
		hD3DXDll = LoadLibraryA("d3dx11_42.dll");
		if (!hD3DXDll)
		{
			MessageBoxA(NULL, "Failed to load d3dx11_42.dll, update your DX11 runtime, please", "Critical error", MB_OK | MB_ICONERROR);
			return E_FAIL;
		}
		else
		{
			NOTICE_LOG(VIDEO, "Successfully loaded d3dx11_42.dll. If you're having trouble, try updating your DX runtime first.");
		}
	}

	PD3DX11CompileFromMemory = (D3DX11COMPILEFROMMEMORYTYPE)GetProcAddress(hD3DXDll, "D3DX11CompileFromMemory");
	if (PD3DX11CompileFromMemory == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3DX11CompileFromMemory!", "Critical error", MB_OK | MB_ICONERROR);

	PD3DX11FilterTexture = (D3DX11FILTERTEXTURETYPE)GetProcAddress(hD3DXDll, "D3DX11FilterTexture");
	if (PD3DX11FilterTexture == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3DX11FilterTexture!", "Critical error", MB_OK | MB_ICONERROR);

	PD3DX11SaveTextureToFileA = (D3DX11SAVETEXTURETOFILEATYPE)GetProcAddress(hD3DXDll, "D3DX11SaveTextureToFileA");
	if (PD3DX11SaveTextureToFileA == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3DX11SaveTextureToFileA!", "Critical error", MB_OK | MB_ICONERROR);

	PD3DX11SaveTextureToFileW = (D3DX11SAVETEXTURETOFILEWTYPE)GetProcAddress(hD3DXDll, "D3DX11SaveTextureToFileW");
	if (PD3DX11SaveTextureToFileW == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3DX11SaveTextureToFileW!", "Critical error", MB_OK | MB_ICONERROR);

	return S_OK;
}

HRESULT LoadD3DCompiler()
{
	if (d3dcompiler_dll_ref++ > 0) return S_OK;
	if (hD3DCompilerDll) return S_OK;

	// try to load D3DCompiler first to check whether we have proper runtime support
	// try to use the dll the backend was compiled against first - don't bother about debug runtimes
	hD3DCompilerDll = LoadLibraryA(D3DCOMPILER_DLL_A);
	if (!hD3DCompilerDll)
	{
		// if that fails, use the dll which should be available in every SDK which officially supports DX11.
		hD3DCompilerDll = LoadLibraryA("D3DCompiler_42.dll");
		if (!hD3DCompilerDll)
		{
			MessageBoxA(NULL, "Failed to load D3DCompiler_42.dll, update your DX11 runtime, please", "Critical error", MB_OK | MB_ICONERROR);
			return E_FAIL;
		}
		else
		{
			NOTICE_LOG(VIDEO, "Successfully loaded D3DCompiler_42.dll. If you're having trouble, try updating your DX runtime first.");
		}
	}

	PD3DReflect = (D3DREFLECT)GetProcAddress(hD3DCompilerDll, "D3DReflect");
	if (PD3DReflect == NULL) MessageBoxA(NULL, "GetProcAddress failed for D3DReflect!", "Critical error", MB_OK | MB_ICONERROR);

	return S_OK;
}

void UnloadDXGI()
{
	if (!dxgi_dll_ref) return;
	if (--dxgi_dll_ref != 0) return;

	if(hDXGIDll) FreeLibrary(hDXGIDll);
	hDXGIDll = NULL;
	PCreateDXGIFactory = NULL;
}

void UnloadD3DX()
{
	if (!d3dx_dll_ref) return;
	if (--d3dx_dll_ref != 0) return;

	if(hD3DXDll) FreeLibrary(hD3DXDll);
	hD3DXDll = NULL;
	PD3DX11FilterTexture = NULL;
	PD3DX11SaveTextureToFileA = NULL;
	PD3DX11SaveTextureToFileW = NULL;
}

void UnloadD3D()
{
	if (!d3d_dll_ref) return;
	if (--d3d_dll_ref != 0) return;

	if(hD3DDll) FreeLibrary(hD3DDll);
	hD3DDll = NULL;
	PD3D11CreateDevice = NULL;
	PD3D11CreateDeviceAndSwapChain = NULL;
}

void UnloadD3DCompiler()
{
	if (!d3dcompiler_dll_ref) return;
	if (--d3dcompiler_dll_ref != 0) return;

	if (hD3DCompilerDll) FreeLibrary(hD3DCompilerDll);
	hD3DCompilerDll = NULL;
	PD3DReflect = NULL;
}

std::vector<DXGI_SAMPLE_DESC> EnumAAModes(IDXGIAdapter* adapter)
{
	std::vector<DXGI_SAMPLE_DESC> aa_modes;

	// NOTE: D3D 10.0 doesn't support multisampled resources which are bound as depth buffers AND shader resources.
	// Thus, we can't have MSAA with 10.0 level hardware.
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	D3D_FEATURE_LEVEL feat_level;
	HRESULT hr = PD3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_SINGLETHREADED, supported_feature_levels, NUM_SUPPORTED_FEATURE_LEVELS, D3D11_SDK_VERSION, &device, &feat_level, &context);
	if (FAILED(hr) || feat_level == D3D_FEATURE_LEVEL_10_0)
	{
		DXGI_SAMPLE_DESC desc;
		desc.Count = 1;
		desc.Quality = 0;
		aa_modes.push_back(desc);
		SAFE_RELEASE(context);
		SAFE_RELEASE(device);
	}
	else
	{
		for (int samples = 0; samples < D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; ++samples)
		{
			UINT quality_levels = 0;
			device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &quality_levels);
			if (quality_levels > 0) {
				DXGI_SAMPLE_DESC desc;
				desc.Count = samples;
				for (desc.Quality = 0; desc.Quality < quality_levels; ++desc.Quality)
					aa_modes.push_back(desc);
			}
		}
		context->Release();
		device->Release();
	}
	return aa_modes;
}

D3D_FEATURE_LEVEL GetFeatureLevel(IDXGIAdapter* adapter)
{
	D3D_FEATURE_LEVEL feat_level = D3D_FEATURE_LEVEL_9_1;
	PD3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_SINGLETHREADED, supported_feature_levels, NUM_SUPPORTED_FEATURE_LEVELS, D3D11_SDK_VERSION, NULL, &feat_level, NULL);
	return feat_level;
}

DXGI_SAMPLE_DESC GetAAMode(int index)
{
	return aa_modes[index];
}

HRESULT Create(HWND wnd)
{
	hWnd = wnd;
	HRESULT hr;

	RECT client;
	GetClientRect(hWnd, &client);
	xres = client.right - client.left;
	yres = client.bottom - client.top;

	hr = LoadDXGI();
	if (SUCCEEDED(hr)) hr = LoadD3D();
	if (SUCCEEDED(hr)) hr = LoadD3DX();
	if (SUCCEEDED(hr)) hr = LoadD3DCompiler();
	if (FAILED(hr))
	{
		UnloadDXGI();
		UnloadD3D();
		UnloadD3DX();
		UnloadD3DCompiler();
		return hr;
	}

	IDXGIFactory* factory;
	IDXGIAdapter* adapter;
	IDXGIOutput* output;
	hr = PCreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(hr)) MessageBox(wnd, _T("Failed to create IDXGIFactory object"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);

	hr = factory->EnumAdapters(g_ActiveConfig.iAdapter, &adapter);
	if (FAILED(hr))
	{
		// try using the first one
		hr = factory->EnumAdapters(0, &adapter);
		if (FAILED(hr)) MessageBox(wnd, _T("Failed to enumerate adapters"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
	}

	// TODO: Make this configurable
	hr = adapter->EnumOutputs(0, &output);
	if (FAILED(hr))
	{
		// try using the first one
		hr = adapter->EnumOutputs(0, &output);
		if (FAILED(hr)) MessageBox(wnd, _T("Failed to enumerate outputs"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
	}

	// get supported AA modes
	aa_modes = EnumAAModes(adapter);
	if (g_Config.iMultisampleMode >= (int)aa_modes.size())
	{
		g_Config.iMultisampleMode = 0;
		UpdateActiveConfig();
	}

	DXGI_SWAP_CHAIN_DESC swap_chain_desc;
	memset(&swap_chain_desc, 0, sizeof(swap_chain_desc));
	swap_chain_desc.BufferCount = 1;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.OutputWindow = wnd;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.Windowed = TRUE;

	DXGI_MODE_DESC mode_desc;
	memset(&mode_desc, 0, sizeof(mode_desc));
	mode_desc.Width = xres;
	mode_desc.Height = yres;
	mode_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	mode_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	hr = output->FindClosestMatchingMode(&mode_desc, &swap_chain_desc.BufferDesc, NULL);
	if (FAILED(hr)) MessageBox(wnd, _T("Failed to find a supported video mode"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);

	// forcing buffer resolution to xres and yres.. TODO: The new video mode might not actually be supported!
	swap_chain_desc.BufferDesc.Width = xres;
	swap_chain_desc.BufferDesc.Height = yres;

#if defined(_DEBUG) || defined(DEBUGFAST)
	// Creating debug devices can sometimes fail if the user doesn't have the correct
	// version of the DirectX SDK. If it does, simply fallback to a non-debug device.
	{
		hr = PD3D11CreateDeviceAndSwapChain(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
											D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG,
											supported_feature_levels, NUM_SUPPORTED_FEATURE_LEVELS,
											D3D11_SDK_VERSION, &swap_chain_desc, &swapchain, &device,
											&featlevel, &context);
	}

	if (FAILED(hr))
#endif
	{
		hr = PD3D11CreateDeviceAndSwapChain(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
											D3D11_CREATE_DEVICE_SINGLETHREADED,
											supported_feature_levels, NUM_SUPPORTED_FEATURE_LEVELS,
											D3D11_SDK_VERSION, &swap_chain_desc, &swapchain, &device,
											&featlevel, &context);
	}

	if (FAILED(hr))
	{
		MessageBox(wnd, _T("Failed to initialize Direct3D.\nMake sure your video card supports at least D3D 10.0"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
		SAFE_RELEASE(device);
		SAFE_RELEASE(context);
		SAFE_RELEASE(swapchain);
		return E_FAIL;
	}
	SetDebugObjectName((ID3D11DeviceChild*)context, "device context");
	SAFE_RELEASE(factory);
	SAFE_RELEASE(output);
	SAFE_RELEASE(adapter);

	ID3D11Texture2D* buf;
	hr = swapchain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&buf);
	if (FAILED(hr))
	{
		MessageBox(wnd, _T("Failed to get swapchain buffer"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
		SAFE_RELEASE(device);
		SAFE_RELEASE(context);
		SAFE_RELEASE(swapchain);
		return E_FAIL;
	}
	backbuf = new D3DTexture2D(buf, D3D11_BIND_RENDER_TARGET);
	SAFE_RELEASE(buf);
	CHECK(backbuf!=NULL, "Create back buffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetTex(), "backbuffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetRTV(), "backbuffer render target view");

	context->OMSetRenderTargets(1, &backbuf->GetRTV(), NULL);

	// BGRA textures are easier to deal with in TextureCache, but might not be supported by the hardware
	UINT format_support;
	device->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &format_support);
	bgra_textures_supported = (format_support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;

	stateman = new StateManager;
	return S_OK;
}

void Close()
{
	// release all bound resources
	context->ClearState();
	SAFE_RELEASE(backbuf);
	SAFE_RELEASE(swapchain);
	SAFE_DELETE(stateman);
	context->Flush();  // immediately destroy device objects

	SAFE_RELEASE(context);
	ULONG references = device->Release();
	if (references)
	{
		ERROR_LOG(VIDEO, "Unreleased references: %i.", references);
	}
	else
	{
		NOTICE_LOG(VIDEO, "Successfully released all device references!");
	}
	device = NULL;

	// unload DLLs
	UnloadD3DX();
	UnloadD3D();
	UnloadDXGI();
}

const char* VertexShaderVersionString()
{
	if(featlevel == D3D_FEATURE_LEVEL_11_0) return "vs_5_0";
	else if(featlevel == D3D_FEATURE_LEVEL_10_1) return "vs_4_1";
	else /*if(featlevel == D3D_FEATURE_LEVEL_10_0)*/ return "vs_4_0";
}

const char* GeometryShaderVersionString()
{
	if(featlevel == D3D_FEATURE_LEVEL_11_0) return "gs_5_0";
	else if(featlevel == D3D_FEATURE_LEVEL_10_1) return "gs_4_1";
	else /*if(featlevel == D3D_FEATURE_LEVEL_10_0)*/ return "gs_4_0";
}

const char* PixelShaderVersionString()
{
	if(featlevel == D3D_FEATURE_LEVEL_11_0) return "ps_5_0";
	else if(featlevel == D3D_FEATURE_LEVEL_10_1) return "ps_4_1";
	else /*if(featlevel == D3D_FEATURE_LEVEL_10_0)*/ return "ps_4_0";
}

D3DTexture2D* &GetBackBuffer() { return backbuf; }
unsigned int GetBackBufferWidth() { return xres; }
unsigned int GetBackBufferHeight() { return yres; }

bool BGRATexturesSupported() { return bgra_textures_supported; }

// Returns the maximum width/height of a texture. This value only depends upon the feature level in DX11
unsigned int GetMaxTextureSize()
{
	switch (featlevel)
	{
		case D3D_FEATURE_LEVEL_11_0:
			return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

		case D3D_FEATURE_LEVEL_10_1:
		case D3D_FEATURE_LEVEL_10_0:
			return D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;

		case D3D_FEATURE_LEVEL_9_3:
			return 4096;

		case D3D_FEATURE_LEVEL_9_2:
		case D3D_FEATURE_LEVEL_9_1:
			return 2048;

		default:
			return 0;
	}
}

void Reset()
{
	// release all back buffer references
	SAFE_RELEASE(backbuf);

	// resize swapchain buffers
	RECT client;
	GetClientRect(hWnd, &client);
	xres = client.right - client.left;
	yres = client.bottom - client.top;
	D3D::swapchain->ResizeBuffers(1, xres, yres, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

	// recreate back buffer texture
	ID3D11Texture2D* buf;
	HRESULT hr = swapchain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&buf);
	if (FAILED(hr))
	{
		MessageBox(hWnd, _T("Failed to get swapchain buffer"), _T("Dolphin Direct3D 11 backend"), MB_OK | MB_ICONERROR);
		SAFE_RELEASE(device);
		SAFE_RELEASE(context);
		SAFE_RELEASE(swapchain);
		return;
	}
	backbuf = new D3DTexture2D(buf, D3D11_BIND_RENDER_TARGET);
	SAFE_RELEASE(buf);
	CHECK(backbuf!=NULL, "Create back buffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetTex(), "backbuffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetRTV(), "backbuffer render target view");
}

bool BeginFrame()
{
	if (bFrameInProgress)
	{
		PanicAlert("BeginFrame called although a frame is already in progress");
		return false;
	}
	bFrameInProgress = true;
	return (device != NULL);
}

void EndFrame()
{
	if (!bFrameInProgress)
	{
		PanicAlert("EndFrame called although no frame is in progress");
		return;
	}
	bFrameInProgress = false;
}

void Present()
{
	// TODO: Is 1 the correct value for vsyncing?
	swapchain->Present((UINT)g_ActiveConfig.IsVSync(), 0);
}

}  // namespace D3D

}  // namespace DX11
