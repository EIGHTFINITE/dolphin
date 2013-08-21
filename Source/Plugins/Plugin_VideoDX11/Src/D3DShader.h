// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "D3DBase.h"
#include "D3DBlob.h"

struct ID3D11PixelShader;
struct ID3D11VertexShader;

namespace DX11
{

namespace D3D
{
	ID3D11VertexShader* CreateVertexShaderFromByteCode(const void* bytecode, unsigned int len);
	ID3D11GeometryShader* CreateGeometryShaderFromByteCode(const void* bytecode, unsigned int len);
	ID3D11PixelShader* CreatePixelShaderFromByteCode(const void* bytecode, unsigned int len);

	// The returned bytecode buffers should be Release()d.
	bool CompileVertexShader(const char* code, unsigned int len,
		D3DBlob** blob);
	bool CompileGeometryShader(const char* code, unsigned int len,
		D3DBlob** blob, const D3D_SHADER_MACRO* pDefines = NULL);
	bool CompilePixelShader(const char* code, unsigned int len,
		D3DBlob** blob, const D3D_SHADER_MACRO* pDefines = NULL);

	// Utility functions
	ID3D11VertexShader* CompileAndCreateVertexShader(const char* code,
		unsigned int len);
	ID3D11GeometryShader* CompileAndCreateGeometryShader(const char* code,
		unsigned int len, const D3D_SHADER_MACRO* pDefines = NULL);
	ID3D11PixelShader* CompileAndCreatePixelShader(const char* code,
		unsigned int len);

	inline ID3D11VertexShader* CreateVertexShaderFromByteCode(D3DBlob* bytecode)
	{ return CreateVertexShaderFromByteCode(bytecode->Data(), bytecode->Size()); }
	inline ID3D11GeometryShader* CreateGeometryShaderFromByteCode(D3DBlob* bytecode)
	{ return CreateGeometryShaderFromByteCode(bytecode->Data(), bytecode->Size()); }
	inline ID3D11PixelShader* CreatePixelShaderFromByteCode(D3DBlob* bytecode)
	{ return CreatePixelShaderFromByteCode(bytecode->Data(), bytecode->Size()); }

	inline ID3D11VertexShader* CompileAndCreateVertexShader(D3DBlob* code)
	{ return CompileAndCreateVertexShader((const char*)code->Data(), code->Size()); }
	inline ID3D11GeometryShader* CompileAndCreateGeometryShader(D3DBlob* code, const D3D_SHADER_MACRO* pDefines = NULL)
	{ return CompileAndCreateGeometryShader((const char*)code->Data(), code->Size(), pDefines); }
	inline ID3D11PixelShader* CompileAndCreatePixelShader(D3DBlob* code)
	{ return CompileAndCreatePixelShader((const char*)code->Data(), code->Size()); }
}

}  // namespace DX11