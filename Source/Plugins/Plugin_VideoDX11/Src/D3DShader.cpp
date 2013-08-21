// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <string>

#include "VideoConfig.h"

#include "D3DBase.h"
#include "D3DShader.h"

namespace DX11
{

namespace D3D
{

// bytecode->shader
ID3D11VertexShader* CreateVertexShaderFromByteCode(const void* bytecode, unsigned int len)
{
	ID3D11VertexShader* v_shader;
	HRESULT hr = D3D::device->CreateVertexShader(bytecode, len, NULL, &v_shader);
	if (FAILED(hr))
		return NULL;

	return v_shader;
}

// code->bytecode
bool CompileVertexShader(const char* code, unsigned int len, D3DBlob** blob)
{
	ID3D10Blob* shaderBuffer = NULL;
	ID3D10Blob* errorBuffer = NULL;

#if defined(_DEBUG) || defined(DEBUGFAST)
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_DEBUG|D3D10_SHADER_WARNINGS_ARE_ERRORS;
#else
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_OPTIMIZATION_LEVEL3|D3D10_SHADER_SKIP_VALIDATION;
#endif
	HRESULT hr = PD3DX11CompileFromMemory(code, len, NULL, NULL, NULL, "main", D3D::VertexShaderVersionString(),
							flags, 0, NULL, &shaderBuffer, &errorBuffer, NULL);
	
	if (errorBuffer)
	{
		INFO_LOG(VIDEO, "Vertex shader compiler messages:\n%s\n",
			(const char*)errorBuffer->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		static int num_failures = 0;
		char szTemp[MAX_PATH];
		sprintf(szTemp, "%sbad_vs_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
		std::ofstream file;
		OpenFStream(file, szTemp, std::ios_base::out);
		file << code;
		file.close();

		PanicAlert("Failed to compile vertex shader!\nThis usually happens when trying to use Dolphin with an outdated GPU or integrated GPU like the Intel GMA series.\n\nIf you're sure this is Dolphin's error anyway, post the contents of %s along with this error message at the forums.\n\nDebug info (%s):\n%s",
						szTemp,
						D3D::VertexShaderVersionString(),
						(char*)errorBuffer->GetBufferPointer());

		*blob = NULL;
		errorBuffer->Release();
	}
	else
	{
		*blob = new D3DBlob(shaderBuffer);
		shaderBuffer->Release();
	}
	return SUCCEEDED(hr);
}

// bytecode->shader
ID3D11GeometryShader* CreateGeometryShaderFromByteCode(const void* bytecode, unsigned int len)
{
	ID3D11GeometryShader* g_shader;
	HRESULT hr = D3D::device->CreateGeometryShader(bytecode, len, NULL, &g_shader);
	if (FAILED(hr))
		return NULL;

	return g_shader;
}

// code->bytecode
bool CompileGeometryShader(const char* code, unsigned int len, D3DBlob** blob,
	const D3D_SHADER_MACRO* pDefines)
{
	ID3D10Blob* shaderBuffer = NULL;
	ID3D10Blob* errorBuffer = NULL;

#if defined(_DEBUG) || defined(DEBUGFAST)
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_DEBUG|D3D10_SHADER_WARNINGS_ARE_ERRORS;
#else
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_OPTIMIZATION_LEVEL3|D3D10_SHADER_SKIP_VALIDATION;
#endif
	HRESULT hr = PD3DX11CompileFromMemory(code, len, NULL, pDefines, NULL, "main", D3D::GeometryShaderVersionString(),
							flags, 0, NULL, &shaderBuffer, &errorBuffer, NULL);
	
	if (errorBuffer)
	{
		INFO_LOG(VIDEO, "Geometry shader compiler messages:\n%s\n",
			(const char*)errorBuffer->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		static int num_failures = 0;
		char szTemp[MAX_PATH];
		sprintf(szTemp, "%sbad_gs_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
		std::ofstream file;
		OpenFStream(file, szTemp, std::ios_base::out);
		file << code;
		file.close();

		PanicAlert("Failed to compile geometry shader!\nThis usually happens when trying to use Dolphin with an outdated GPU or integrated GPU like the Intel GMA series.\n\nIf you're sure this is Dolphin's error anyway, post the contents of %s along with this error message at the forums.\n\nDebug info (%s):\n%s",
						szTemp,
						D3D::GeometryShaderVersionString(),
						(char*)errorBuffer->GetBufferPointer());

		*blob = NULL;
		errorBuffer->Release();
	}
	else
	{
		*blob = new D3DBlob(shaderBuffer);
		shaderBuffer->Release();
	}
	return SUCCEEDED(hr);
}

// bytecode->shader
ID3D11PixelShader* CreatePixelShaderFromByteCode(const void* bytecode, unsigned int len)
{
	ID3D11PixelShader* p_shader;
	HRESULT hr = D3D::device->CreatePixelShader(bytecode, len, NULL, &p_shader);
	if (FAILED(hr))
	{
		PanicAlert("CreatePixelShaderFromByteCode failed at %s %d\n", __FILE__, __LINE__);
		p_shader = NULL;
	}
	return p_shader;
}

// code->bytecode
bool CompilePixelShader(const char* code, unsigned int len, D3DBlob** blob,
	const D3D_SHADER_MACRO* pDefines)
{
	ID3D10Blob* shaderBuffer = NULL;
	ID3D10Blob* errorBuffer = NULL;

#if defined(_DEBUG) || defined(DEBUGFAST)
	UINT flags = D3D10_SHADER_DEBUG|D3D10_SHADER_WARNINGS_ARE_ERRORS;
#else
	UINT flags = D3D10_SHADER_OPTIMIZATION_LEVEL3;
#endif
	HRESULT hr = PD3DX11CompileFromMemory(code, len, NULL, pDefines, NULL, "main", D3D::PixelShaderVersionString(),
							flags, 0, NULL, &shaderBuffer, &errorBuffer, NULL);
	
	if (errorBuffer)
	{
		INFO_LOG(VIDEO, "Pixel shader compiler messages:\n%s",
			(const char*)errorBuffer->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		static int num_failures = 0;
		char szTemp[MAX_PATH];
		sprintf(szTemp, "%sbad_ps_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
		std::ofstream file;
		OpenFStream(file, szTemp, std::ios_base::out);
		file << code;
		file.close();

		PanicAlert("Failed to compile pixel shader!\nThis usually happens when trying to use Dolphin with an outdated GPU or integrated GPU like the Intel GMA series.\n\nIf you're sure this is Dolphin's error anyway, post the contents of %s along with this error message at the forums.\n\nDebug info (%s):\n%s",
						szTemp,
						D3D::PixelShaderVersionString(),
						(char*)errorBuffer->GetBufferPointer());

		*blob = NULL;
		errorBuffer->Release();
	}
	else
	{
		*blob = new D3DBlob(shaderBuffer);
		shaderBuffer->Release();
	}

	return SUCCEEDED(hr);
}

ID3D11VertexShader* CompileAndCreateVertexShader(const char* code,
	unsigned int len)
{
	D3DBlob* blob = NULL;
	if (CompileVertexShader(code, len, &blob))
	{
		ID3D11VertexShader* v_shader = CreateVertexShaderFromByteCode(blob);
		blob->Release();
		return v_shader;
	}
	return NULL;
}

ID3D11GeometryShader* CompileAndCreateGeometryShader(const char* code,
	unsigned int len, const D3D_SHADER_MACRO* pDefines)
{
	D3DBlob* blob = NULL;
	if (CompileGeometryShader(code, len, &blob, pDefines))
	{
		ID3D11GeometryShader* g_shader = CreateGeometryShaderFromByteCode(blob);
		blob->Release();
		return g_shader;
	}
	return NULL;
}

ID3D11PixelShader* CompileAndCreatePixelShader(const char* code,
	unsigned int len)
{
	D3DBlob* blob = NULL;
	CompilePixelShader(code, len, &blob);
	if (blob)
	{
		ID3D11PixelShader* p_shader = CreatePixelShaderFromByteCode(blob);
		blob->Release();
		return p_shader;
	}
	return NULL;
}

}  // namespace

}  // namespace DX11