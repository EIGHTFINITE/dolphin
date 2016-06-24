// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <memory>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"
#include "Core/Host.h"

#include "VideoBackends/D3D/BoundingBox.h"
#include "VideoBackends/D3D/D3DBase.h"
#include "VideoBackends/D3D/D3DUtil.h"
#include "VideoBackends/D3D/GeometryShaderCache.h"
#include "VideoBackends/D3D/PerfQuery.h"
#include "VideoBackends/D3D/PixelShaderCache.h"
#include "VideoBackends/D3D/Render.h"
#include "VideoBackends/D3D/TextureCache.h"
#include "VideoBackends/D3D/VertexManager.h"
#include "VideoBackends/D3D/VertexShaderCache.h"
#include "VideoBackends/D3D/VideoBackend.h"

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

namespace DX11
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
	return "D3D";
}

std::string VideoBackend::GetDisplayName() const
{
	return "Direct3D 11";
}

void InitBackendInfo()
{
	HRESULT hr = DX11::D3D::LoadDXGI();
	if (SUCCEEDED(hr)) hr = DX11::D3D::LoadD3D();
	if (FAILED(hr))
	{
		DX11::D3D::UnloadDXGI();
		return;
	}

	g_Config.backend_info.APIType = API_D3D;
	g_Config.backend_info.bSupportsExclusiveFullscreen = true;
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
	hr = DX11::PCreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(hr))
		PanicAlert("Failed to create IDXGIFactory object");

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
			std::string samples;
			std::vector<DXGI_SAMPLE_DESC> modes = DX11::D3D::EnumAAModes(ad);
			// First iteration will be 1. This equals no AA.
			for (unsigned int i = 0; i < modes.size(); ++i)
			{
				g_Config.backend_info.AAModes.push_back(modes[i].Count);
			}

			bool shader_model_5_supported = (DX11::D3D::GetFeatureLevel(ad) >= D3D_FEATURE_LEVEL_11_0);

			// Requires the earlydepthstencil attribute (only available in shader model 5)
			g_Config.backend_info.bSupportsEarlyZ = shader_model_5_supported;

			// Requires full UAV functionality (only available in shader model 5)
			g_Config.backend_info.bSupportsBBox = shader_model_5_supported;

			// Requires the instance attribute (only available in shader model 5)
			g_Config.backend_info.bSupportsGSInstancing = shader_model_5_supported;

			// Sample shading requires shader model 5
			g_Config.backend_info.bSupportsSSAA = shader_model_5_supported;
		}
		g_Config.backend_info.Adapters.push_back(UTF16ToUTF8(desc.Description));
		ad->Release();
	}
	factory->Release();

	// Clear ppshaders string vector
	g_Config.backend_info.PPShaders.clear();
	g_Config.backend_info.AnaglyphShaders.clear();

	DX11::D3D::UnloadDXGI();
	DX11::D3D::UnloadD3D();
}

void VideoBackend::ShowConfig(void *hParent)
{
	InitBackendInfo();
	Host_ShowVideoConfig(hParent, GetDisplayName(), "gfx_dx11");
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
		g_Config.Load(File::GetUserPath(D_CONFIG_IDX) + "gfx_dx11.ini");

	g_Config.GameIniLoad();
	g_Config.UpdateProjectionHack();
	g_Config.VerifyValidity();
	UpdateActiveConfig();

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
	VertexShaderCache::Init();
	PixelShaderCache::Init();
	GeometryShaderCache::Init();
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
	m_initialized = false;

	// TODO: should be in Video_Cleanup
	if (g_renderer)
	{
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
		PixelShaderCache::Shutdown();
		VertexShaderCache::Shutdown();
		GeometryShaderCache::Shutdown();
		BBox::Shutdown();

		g_perf_query.reset();
		g_vertex_manager.reset();
		g_texture_cache.reset();
		g_renderer.reset();
	}
}

void VideoBackend::Video_Cleanup()
{
}

}
