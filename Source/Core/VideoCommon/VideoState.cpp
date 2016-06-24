// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstring>

#include "Common/ChunkFile.h"
#include "VideoCommon/BoundingBox.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/CPMemory.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoState.h"
#include "VideoCommon/XFMemory.h"

void VideoCommon_DoState(PointerWrap &p)
{
	// BP Memory
	p.Do(bpmem);
	p.DoMarker("BP Memory");

	// CP Memory
	DoCPState(p);

	// XF Memory
	p.Do(xfmem);
	p.DoMarker("XF Memory");

	// Texture decoder
	p.DoArray(texMem);
	p.DoMarker("texMem");

	// FIFO
	Fifo::DoState(p);
	p.DoMarker("Fifo");

	CommandProcessor::DoState(p);
	p.DoMarker("CommandProcessor");

	PixelEngine::DoState(p);
	p.DoMarker("PixelEngine");

	// the old way of replaying current bpmem as writes to push side effects to pixel shader manager doesn't really work.
	PixelShaderManager::DoState(p);
	p.DoMarker("PixelShaderManager");

	VertexShaderManager::DoState(p);
	p.DoMarker("VertexShaderManager");

	GeometryShaderManager::DoState(p);
	p.DoMarker("GeometryShaderManager");

	VertexManagerBase::DoState(p);
	p.DoMarker("VertexManager");

	BoundingBox::DoState(p);
	p.DoMarker("BoundingBox");


	// TODO: search for more data that should be saved and add it here
}

void VideoCommon_Init()
{
	memset(&g_main_cp_state, 0, sizeof(g_main_cp_state));
	memset(&g_preprocess_cp_state, 0, sizeof(g_preprocess_cp_state));
	memset(texMem, 0, TMEM_SIZE);
}
