// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "CommonTypes.h"
#include "VideoCommon.h"
#include <vector>

#ifndef _STATISTICS_H
#define _STATISTICS_H

struct Statistics
{
	int numPixelShadersCreated;
	int numPixelShadersAlive;
	int numVertexShadersCreated;
	int numVertexShadersAlive;

	int numTexturesCreated;
	int numTexturesAlive;

	int numRenderTargetsCreated;
	int numRenderTargetsAlive;
	
	int numDListsCalled;
	int numDListsCreated;
	int numDListsAlive;

	int numVertexLoaders;

	int numUniquePixelShaders;

	float proj_0, proj_1, proj_2, proj_3, proj_4, proj_5;
	float gproj_0, gproj_1, gproj_2, gproj_3, gproj_4, gproj_5;
	float gproj_6, gproj_7, gproj_8, gproj_9, gproj_10, gproj_11, gproj_12, gproj_13, gproj_14, gproj_15;

	float g2proj_0, g2proj_1, g2proj_2, g2proj_3, g2proj_4, g2proj_5;
	float g2proj_6, g2proj_7, g2proj_8, g2proj_9, g2proj_10, g2proj_11, g2proj_12, g2proj_13, g2proj_14, g2proj_15;

	std::vector<EFBRectangle> efb_regions;

	struct ThisFrame
	{
		int numBPLoads;
		int numCPLoads;
		int numXFLoads;
		
		int numBPLoadsInDL;
		int numCPLoadsInDL;
		int numXFLoadsInDL;
		
		int numDLs;
		int numPrims;
		int numDLPrims;
		int numShaderChanges;

		int numPrimitiveJoins;
		int numDrawCalls;
		int numIndexedDrawCalls;
		int numBufferSplits;

		int numDListsCalled;
		
		int bytesVertexStreamed;
		int bytesIndexStreamed;
		int bytesUniformStreamed;
	};
	ThisFrame thisFrame;
	void ResetFrame();
	static void SwapDL();

	// Yeah, this is unsafe, but we really don't wanna faff around allocating
	// buffers here.
	static char *ToString(char *ptr);
	static char *ToStringProj(char *ptr);
};

extern Statistics stats;

#define STATISTICS

#ifdef STATISTICS
#define INCSTAT(a) (a)++;
#define DECSTAT(a) (a)--;
#define ADDSTAT(a,b) (a)+=(b);
#define SETSTAT(a,x) (a)=(int)(x);
#define SETSTAT_UINT(a,x) (a)=(u32)(x);
#define SETSTAT_FT(a,x) (a)=(float)(x);
#else
#define INCSTAT(a) ;
#define ADDSTAT(a,b) ;
#define SETSTAT(a,x) ;
#endif

#endif  // _STATISTICS_H
