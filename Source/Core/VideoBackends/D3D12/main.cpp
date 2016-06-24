// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"
#include "Core/Host.h"

#include "VideoBackends/D3D12/BoundingBox.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DUtil.h"
#include "VideoBackends/D3D12/PerfQuery.h"
#include "VideoBackends/D3D12/Render.h"
#include "VideoBackends/D3D12/ShaderCache.h"
#include "VideoBackends/D3D12/ShaderConstantsManager.h"
#include "VideoBackends/D3D12/StaticShaderCache.h"
#include "VideoBackends/D3D12/TextureCache.h"
#include "VideoBackends/D3D12/VertexManager.h"
#include "VideoBackends/D3D12/VideoBackend.h"
#include "VideoBackends/D3D12/XFBEncoder.h"

#include "VideoCommon/BPStructs.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

unsigned int VideoBackend::PeekMessages()
{
	MSG msg;
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return FALSE;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return TRUE;
}

std::string VideoBackend::GetName() const
{
	return "D3D12";
}

std::string VideoBackend::GetDisplayName() const
{
	return "Direct3D 12 (experimental)";
}

void InitBackendInfo()
{
	HRESULT hr = D3D::LoadDXGI();
	if (FAILED(hr))
		return;

	hr = D3D::LoadD3D();
	if (FAILED(hr))
	{
		D3D::UnloadDXGI();
		return;
	}

	g_Config.backend_info.APIType = API_D3D;
	g_Config.backend_info.bSupportsExclusiveFullscreen = false;
	g_Config.backend_info.bSupportsDualSourceBlend = true;
	g_Config.backend_info.bSupportsPrimitiveRestart = true;
	g_Config.backend_info.bSupportsOversizedViewports = false;
	g_Config.backend_info.bSupportsGeometryShaders = true;
	g_Config.backend_info.bSupports3DVision = true;
	g_Config.backend_info.bSupportsPostProcessing = false;
	g_Config.backend_info.bSupportsPaletteConversion = true;
	g_Config.backend_info.bSupportsClipControl = true;

	IDXGIFactory* factory;
	IDXGIAdapter* ad;
	hr = create_dxgi_factory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(hr))
	{
		PanicAlert("Failed to create IDXGIFactory object");
		D3D::UnloadD3D();
		D3D::UnloadDXGI();
		return;
	}

	// adapters
	g_Config.backend_info.Adapters.clear();
	g_Config.backend_info.AAModes.clear();
	while (factory->EnumAdapters((UINT)g_Config.backend_info.Adapters.size(), &ad) != DXGI_ERROR_NOT_FOUND)
	{
		const size_t adapter_index = g_Config.backend_info.Adapters.size();

		DXGI_ADAPTER_DESC desc;
		ad->GetDesc(&desc);

		// TODO: These don't get updated on adapter change, yet
		if (adapter_index == g_Config.iAdapter)
		{
			ID3D12Device* temp_device;
			hr = d3d12_create_device(ad, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&temp_device));
			if (SUCCEEDED(hr))
			{
				std::string samples;
				std::vector<DXGI_SAMPLE_DESC> modes = D3D::EnumAAModes(temp_device);
				// First iteration will be 1. This equals no AA.
				for (unsigned int i = 0; i < modes.size(); ++i)
				{
					g_Config.backend_info.AAModes.push_back(modes[i].Count);
				}

				// Requires the earlydepthstencil attribute (only available in shader model 5)
				g_Config.backend_info.bSupportsEarlyZ = true;

				// Requires full UAV functionality (only available in shader model 5)
				g_Config.backend_info.bSupportsBBox = true;

				// Requires the instance attribute (only available in shader model 5)
				g_Config.backend_info.bSupportsGSInstancing = true;

				// Sample shading requires shader model 5
				g_Config.backend_info.bSupportsSSAA = true;

				temp_device->Release();
			}
		}

		g_Config.backend_info.Adapters.push_back(UTF16ToUTF8(desc.Description));
		ad->Release();
	}
	factory->Release();

	// Clear ppshaders string vector
	g_Config.backend_info.PPShaders.clear();
	g_Config.backend_info.AnaglyphShaders.clear();

	D3D::UnloadD3D();
	D3D::UnloadDXGI();
}

void VideoBackend::ShowConfig(void *hParent)
{
	InitBackendInfo();
	Host_ShowVideoConfig(hParent, GetDisplayName(), "gfx_dx12");
}

bool VideoBackend::Initialize(void *window_handle)
{
	if (window_handle == nullptr)
		return false;

	InitializeShared();
	InitBackendInfo();

	frameCount = 0;

	if (File::Exists(File::GetUserPath(D_CONFIG_IDX) + "GFX.ini"))
		g_Config.Load(File::GetUserPath(D_CONFIG_IDX) + "GFX.ini");
	else
		g_Config.Load(File::GetUserPath(D_CONFIG_IDX) + "gfx_dx12.ini");

	g_Config.GameIniLoad();
	g_Config.UpdateProjectionHack();
	g_Config.VerifyValidity();
	UpdateActiveConfig();

	if (FAILED(D3D::Create((HWND)window_handle)))
		return false;

	m_window_handle = window_handle;
	m_initialized = true;

	return true;
}

void VideoBackend::Video_Prepare()
{
	// internal interfaces
	g_renderer = std::make_unique<Renderer>(m_window_handle);
	g_texture_cache = std::make_unique<TextureCache>();
	g_vertex_manager = std::make_unique<VertexManager>();
	g_perf_query = std::make_unique<PerfQuery>();
	g_xfb_encoder = std::make_unique<XFBEncoder>();
	ShaderCache::Init();
	ShaderConstantsManager::Init();
	StaticShaderCache::Init();
	StateCache::Init(); // PSO cache is populated here, after constituent shaders are loaded.
	D3D::InitUtils();

	// VideoCommon
	BPInit();
	Fifo::Init();
	IndexGenerator::Init();
	VertexLoaderManager::Init();
	OpcodeDecoder::Init();
	VertexShaderManager::Init();
	PixelShaderManager::Init();
	GeometryShaderManager::Init();
	CommandProcessor::Init();
	PixelEngine::Init();
	BBox::Init();

	// Tell the host that the window is ready
	Host_Message(WM_USER_CREATE);
}

void VideoBackend::Shutdown()
{
	m_initialized = true;

	// TODO: should be in Video_Cleanup
	if (g_renderer)
	{
		// Immediately stop app from submitting work to GPU, and wait for all submitted work to complete. D3D12TODO: Check this.
		D3D::command_list_mgr->ExecuteQueuedWork(true);

		// VideoCommon
		Fifo::Shutdown();
		CommandProcessor::Shutdown();
		GeometryShaderManager::Shutdown();
		PixelShaderManager::Shutdown();
		VertexShaderManager::Shutdown();
		OpcodeDecoder::Shutdown();
		VertexLoaderManager::Shutdown();

		// internal interfaces
		D3D::ShutdownUtils();
		ShaderCache::Shutdown();
		ShaderConstantsManager::Shutdown();
		StaticShaderCache::Shutdown();
		BBox::Shutdown();

		g_xfb_encoder.reset();
		g_perf_query.reset();
		g_vertex_manager.reset();
		g_texture_cache.reset();
		g_renderer.reset();

		D3D::Close();
	}
}

void VideoBackend::Video_Cleanup()
{
}

}
