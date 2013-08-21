// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "PixelShaderGen.h"

#include <d3d11.h>

#include <map>

enum DSTALPHA_MODE;

namespace DX11
{

class PixelShaderCache
{
public:
	static void Init();
	static void Clear();
	static void Shutdown();
	static bool SetShader(DSTALPHA_MODE dstAlphaMode, u32 components); // TODO: Should be renamed to LoadShader
	static bool InsertByteCode(const PixelShaderUid &uid, const void* bytecode, unsigned int bytecodelen);

	static ID3D11PixelShader* GetActiveShader() { return last_entry->shader; }
	static ID3D11Buffer* &GetConstantBuffer();

	static ID3D11PixelShader* GetColorMatrixProgram(bool multisampled);
	static ID3D11PixelShader* GetColorCopyProgram(bool multisampled);
	static ID3D11PixelShader* GetDepthMatrixProgram(bool multisampled);
	static ID3D11PixelShader* GetClearProgram();
	static ID3D11PixelShader* ReinterpRGBA6ToRGB8(bool multisampled);
	static ID3D11PixelShader* ReinterpRGB8ToRGBA6(bool multisampled);

	static void InvalidateMSAAShaders();

private:
	struct PSCacheEntry
	{
		ID3D11PixelShader* shader;

		std::string code;

		PSCacheEntry() : shader(NULL) {}
		void Destroy() { SAFE_RELEASE(shader); }
	};

	typedef std::map<PixelShaderUid, PSCacheEntry> PSCache;

	static PSCache PixelShaders;
	static const PSCacheEntry* last_entry;
	static PixelShaderUid last_uid;

	static UidChecker<PixelShaderUid,PixelShaderCode> pixel_uid_checker;
};

}  // namespace DX11
