// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _VIDEOSOFTWARE_CONFIG_H_
#define _VIDEOSOFTWARE_CONFIG_H_

#include "Common.h"

#define STATISTICS 1

// NEVER inherit from this class.
struct SWVideoConfig : NonCopyable
{
	SWVideoConfig();
	void Load(const char* ini_file);
	void Save(const char* ini_file);

	// General
	bool bFullscreen;
	bool bHideCursor;
	bool renderToMainframe;	

	bool bHwRasterizer;

	// Emulation features
	bool bZComploc;
	bool bZFreeze;

	bool bShowStats;

	bool bDumpTextures;
	bool bDumpObjects;
	bool bDumpFrames;

	// Debug only
	bool bDumpTevStages;
	bool bDumpTevTextureFetches;

	u32 drawStart;
	u32 drawEnd;
};

extern SWVideoConfig g_SWVideoConfig;

#endif  // _VIDEOSOFTWARE_CONFIG_H_
