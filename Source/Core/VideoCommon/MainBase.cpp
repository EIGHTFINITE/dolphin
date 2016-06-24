// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstring>

#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Event.h"
#include "Common/Flag.h"
#include "Common/Logging/Log.h"
#include "VideoCommon/AsyncRequests.h"
#include "VideoCommon/BPStructs.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/TextureCacheBase.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoState.h"

static Common::Flag s_FifoShuttingDown;

static volatile struct
{
	u32 xfbAddr;
	u32 fbWidth;
	u32 fbStride;
	u32 fbHeight;
} s_beginFieldArgs;

void VideoBackendBase::Video_ExitLoop()
{
	Fifo::ExitGpuLoop();
	s_FifoShuttingDown.Set();
}

// Run from the CPU thread (from VideoInterface.cpp)
void VideoBackendBase::Video_BeginField(u32 xfbAddr, u32 fbWidth, u32 fbStride, u32 fbHeight)
{
	if (m_initialized && g_ActiveConfig.bUseXFB)
	{
		s_beginFieldArgs.xfbAddr = xfbAddr;
		s_beginFieldArgs.fbWidth = fbWidth;
		s_beginFieldArgs.fbStride = fbStride;
		s_beginFieldArgs.fbHeight = fbHeight;
	}
}

// Run from the CPU thread (from VideoInterface.cpp)
void VideoBackendBase::Video_EndField()
{
	if (m_initialized && g_ActiveConfig.bUseXFB && g_renderer)
	{
		Fifo::SyncGPU(Fifo::SYNC_GPU_SWAP);

		AsyncRequests::Event e;
		e.time = 0;
		e.type = AsyncRequests::Event::SWAP_EVENT;

		e.swap_event.xfbAddr = s_beginFieldArgs.xfbAddr;
		e.swap_event.fbWidth = s_beginFieldArgs.fbWidth;
		e.swap_event.fbStride = s_beginFieldArgs.fbStride;
		e.swap_event.fbHeight = s_beginFieldArgs.fbHeight;
		AsyncRequests::GetInstance()->PushEvent(e, false);
	}
}

u32 VideoBackendBase::Video_AccessEFB(EFBAccessType type, u32 x, u32 y, u32 InputData)
{
	if (!g_ActiveConfig.bEFBAccessEnable)
	{
		return 0;
	}

	if (type == POKE_COLOR || type == POKE_Z)
	{
		AsyncRequests::Event e;
		e.type = type == POKE_COLOR ? AsyncRequests::Event::EFB_POKE_COLOR : AsyncRequests::Event::EFB_POKE_Z;
		e.time = 0;
		e.efb_poke.data = InputData;
		e.efb_poke.x = x;
		e.efb_poke.y = y;
		AsyncRequests::GetInstance()->PushEvent(e, false);
		return 0;
	}
	else
	{
		AsyncRequests::Event e;
		u32 result;
		e.type = type == PEEK_COLOR ? AsyncRequests::Event::EFB_PEEK_COLOR : AsyncRequests::Event::EFB_PEEK_Z;
		e.time = 0;
		e.efb_peek.x = x;
		e.efb_peek.y = y;
		e.efb_peek.data = &result;
		AsyncRequests::GetInstance()->PushEvent(e, true);
		return result;
	}
}

u32 VideoBackendBase::Video_GetQueryResult(PerfQueryType type)
{
	if (!g_perf_query->ShouldEmulate())
	{
		return 0;
	}

	Fifo::SyncGPU(Fifo::SYNC_GPU_PERFQUERY);

	AsyncRequests::Event e;
	e.time = 0;
	e.type = AsyncRequests::Event::PERF_QUERY;

	if (!g_perf_query->IsFlushed())
		AsyncRequests::GetInstance()->PushEvent(e, true);

	return g_perf_query->GetQueryResult(type);
}

u16 VideoBackendBase::Video_GetBoundingBox(int index)
{
	if (!g_ActiveConfig.backend_info.bSupportsBBox)
		return 0;

	if (!g_ActiveConfig.bBBoxEnable)
	{
		static bool warn_once = true;
		if (warn_once)
			ERROR_LOG(VIDEO, "BBox shall be used but it is disabled. Please use a gameini to enable it for this game.");
		warn_once = false;
		return 0;
	}

	Fifo::SyncGPU(Fifo::SYNC_GPU_BBOX);

	AsyncRequests::Event e;
	u16 result;
	e.time = 0;
	e.type = AsyncRequests::Event::BBOX_READ;
	e.bbox.index = index;
	e.bbox.data = &result;
	AsyncRequests::GetInstance()->PushEvent(e, true);

	return result;
}

void VideoBackendBase::InitializeShared()
{
	VideoCommon_Init();

	s_FifoShuttingDown.Clear();
	memset((void*)&s_beginFieldArgs, 0, sizeof(s_beginFieldArgs));
	m_invalid = false;
}

// Run from the CPU thread
void VideoBackendBase::DoState(PointerWrap& p)
{
	bool software = false;
	p.Do(software);

	if (p.GetMode() == PointerWrap::MODE_READ && software == true)
	{
		// change mode to abort load of incompatible save state.
		p.SetMode(PointerWrap::MODE_VERIFY);
	}

	VideoCommon_DoState(p);
	p.DoMarker("VideoCommon");

	p.Do(s_beginFieldArgs);
	p.DoMarker("VideoBackendBase");

	// Refresh state.
	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		m_invalid = true;

		// Clear all caches that touch RAM
		// (? these don't appear to touch any emulation state that gets saved. moved to on load only.)
		VertexLoaderManager::MarkAllDirty();
	}
}

void VideoBackendBase::CheckInvalidState()
{
	if (m_invalid)
	{
		m_invalid = false;

		BPReload();
		TextureCacheBase::Invalidate();
	}
}
