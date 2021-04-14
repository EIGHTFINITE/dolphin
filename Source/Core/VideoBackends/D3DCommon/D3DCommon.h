// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3dcompiler.h>
#include <dxgiformat.h>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "Common/CommonTypes.h"

#define CHECK(cond, Message, ...)                                                                  \
  if (!(cond))                                                                                     \
  {                                                                                                \
    PanicAlert("%s failed in %s at line %d: " Message, __func__, __FILE__, __LINE__,               \
               ##__VA_ARGS__);                                                                     \
  }

struct IDXGIFactory;

enum class AbstractTextureFormat : u32;

namespace D3DCommon
{
// Loading dxgi.dll and d3dcompiler.dll
bool LoadLibraries();
void UnloadLibraries();

// Returns a list of D3D device names.
std::vector<std::string> GetAdapterNames();

// Helper function which creates a DXGI factory.
Microsoft::WRL::ComPtr<IDXGIFactory> CreateDXGIFactory(bool debug_device);

// Globally-accessible D3DCompiler function.
extern pD3DCompile d3d_compile;

// Helpers for texture format conversion.
DXGI_FORMAT GetDXGIFormatForAbstractFormat(AbstractTextureFormat format, bool typeless);
DXGI_FORMAT GetSRVFormatForAbstractFormat(AbstractTextureFormat format);
DXGI_FORMAT GetRTVFormatForAbstractFormat(AbstractTextureFormat format, bool integer);
DXGI_FORMAT GetDSVFormatForAbstractFormat(AbstractTextureFormat format);
AbstractTextureFormat GetAbstractFormatForDXGIFormat(DXGI_FORMAT format);

// This function will assign a name to the given resource.
// The DirectX debug layer will make it easier to identify resources that way,
// e.g. when listing up all resources who have unreleased references.
void SetDebugObjectName(IUnknown* resource, std::string_view name);
}  // namespace D3DCommon
