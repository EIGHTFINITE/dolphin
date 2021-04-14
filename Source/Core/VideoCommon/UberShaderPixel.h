// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include "Common/CommonTypes.h"
#include "VideoCommon/ShaderGenCommon.h"

enum class APIType;

namespace UberShader
{
#pragma pack(1)
struct pixel_ubershader_uid_data
{
  u32 num_texgens : 4;
  u32 early_depth : 1;
  u32 per_pixel_depth : 1;
  u32 uint_output : 1;

  u32 NumValues() const { return sizeof(pixel_ubershader_uid_data); }
};
#pragma pack()

using PixelShaderUid = ShaderUid<pixel_ubershader_uid_data>;

PixelShaderUid GetPixelShaderUid();

ShaderCode GenPixelShader(APIType ApiType, const ShaderHostConfig& host_config,
                          const pixel_ubershader_uid_data* uid_data);

void EnumeratePixelShaderUids(const std::function<void(const PixelShaderUid&)>& callback);
void ClearUnusedPixelShaderUidBits(APIType ApiType, const ShaderHostConfig& host_config,
                                   PixelShaderUid* uid);
}  // namespace UberShader
