// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "CommonTypes.h"
#include "SWVideoConfig.h"

#ifndef _STATISTICS_H
#define _STATISTICS_H

struct SWStatistics
{
	struct ThisFrame
	{
		u32 numDrawnObjects;
		u32 numPrimatives;
		u32 numVerticesLoaded;
		u32 numVerticesOut;

		u32 numTrianglesIn;
		u32 numTrianglesRejected;
		u32 numTrianglesCulled;
		u32 numTrianglesClipped;
		u32 numTrianglesDrawn;

		u32 rasterizedPixels;
		u32 tevPixelsIn;
		u32 tevPixelsOut;
	};

	u32 frameCount;
	SWStatistics();

	ThisFrame thisFrame;
	void ResetFrame();
};

extern SWStatistics swstats;

#if (STATISTICS)
#define INCSTAT(a) (a)++;
#define ADDSTAT(a,b) (a)+=(b);
#define SETSTAT(a,x) (a)=(int)(x);
#else
#define INCSTAT(a) ;
#define ADDSTAT(a,b) ;
#define SETSTAT(a,x) ;
#endif

#endif  // _STATISTICS_H
