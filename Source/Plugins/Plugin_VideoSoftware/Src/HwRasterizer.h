// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _HW_RASTERIZER_H
#define _HW_RASTERIZER_H

#include <map>

#include "BPMemLoader.h"
#include "../../Plugin_VideoOGL/Src/GLUtil.h"

struct OutputVertexData;

namespace HwRasterizer
{
	void Init();
	void Shutdown();

	void Prepare();

	void BeginTriangles();
	void EndTriangles();

	void DrawTriangleFrontFace(OutputVertexData *v0, OutputVertexData *v1, OutputVertexData *v2);

	void Clear();

	struct TexCacheEntry
	{
		TexImage0 texImage0; 
		TexImage1 texImage1; 
		TexImage2 texImage2; 
		TexImage3 texImage3; 
		TexTLUT texTlut;

		GLuint texture;

		TexCacheEntry();

		void Create();
		void Destroy();
		void Update();
	};

	typedef std::map<u32, TexCacheEntry> TextureCache;
	static TextureCache textures;
}

#endif 
