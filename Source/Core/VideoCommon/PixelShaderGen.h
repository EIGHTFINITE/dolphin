// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/CommonTypes.h"
#include "VideoCommon/LightingShaderGen.h"
#include "VideoCommon/ShaderGenCommon.h"

enum class APIType;
enum class AlphaTestResult;
enum class SrcBlendFactor : u32;
enum class DstBlendFactor : u32;
enum class CompareMode : u32;
enum class AlphaTestOp : u32;
enum class RasColorChan : u32;
enum class KonstSel : u32;
enum class FogProjection : u32;
enum class FogType : u32;

#pragma pack(1)
struct pixel_shader_uid_data
{
  // TODO: Optimize field order for easy access!

  u32 num_values;  // TODO: Shouldn't be a u32
  u32 NumValues() const { return num_values; }
  u32 pad0 : 4;
  u32 useDstAlpha : 1;
  AlphaTestResult Pretest : 2;
  u32 nIndirectStagesUsed : 4;
  u32 genMode_numtexgens : 4;
  u32 genMode_numtevstages : 4;
  u32 genMode_numindstages : 3;
  CompareMode alpha_test_comp0 : 3;
  CompareMode alpha_test_comp1 : 3;
  AlphaTestOp alpha_test_logic : 2;
  u32 alpha_test_use_zcomploc_hack : 1;
  FogProjection fog_proj : 1;

  FogType fog_fsel : 3;
  u32 fog_RangeBaseEnabled : 1;
  u32 ztex_op : 2;
  u32 per_pixel_depth : 1;
  u32 forced_early_z : 1;
  u32 early_ztest : 1;
  u32 late_ztest : 1;
  u32 bounding_box : 1;
  u32 zfreeze : 1;
  u32 numColorChans : 2;
  u32 rgba6_format : 1;
  u32 dither : 1;
  u32 uint_output : 1;
  u32 blend_enable : 1;                       // Only used with shader_framebuffer_fetch blend
  SrcBlendFactor blend_src_factor : 3;        // Only used with shader_framebuffer_fetch blend
  SrcBlendFactor blend_src_factor_alpha : 3;  // Only used with shader_framebuffer_fetch blend
  DstBlendFactor blend_dst_factor : 3;        // Only used with shader_framebuffer_fetch blend
  DstBlendFactor blend_dst_factor_alpha : 3;  // Only used with shader_framebuffer_fetch blend
  u32 blend_subtract : 1;                     // Only used with shader_framebuffer_fetch blend
  u32 blend_subtract_alpha : 1;               // Only used with shader_framebuffer_fetch blend

  u32 texMtxInfo_n_projection : 8;  // 8x1 bit
  u32 tevindref_bi0 : 3;
  u32 tevindref_bc0 : 3;
  u32 tevindref_bi1 : 3;
  u32 tevindref_bc1 : 3;
  u32 tevindref_bi2 : 3;
  u32 tevindref_bc3 : 3;
  u32 tevindref_bi4 : 3;
  u32 tevindref_bc4 : 3;

  void SetTevindrefValues(int index, u32 texcoord, u32 texmap)
  {
    if (index == 0)
    {
      tevindref_bc0 = texcoord;
      tevindref_bi0 = texmap;
    }
    else if (index == 1)
    {
      tevindref_bc1 = texcoord;
      tevindref_bi1 = texmap;
    }
    else if (index == 2)
    {
      tevindref_bc3 = texcoord;
      tevindref_bi2 = texmap;
    }
    else if (index == 3)
    {
      tevindref_bc4 = texcoord;
      tevindref_bi4 = texmap;
    }
  }

  u32 GetTevindirefCoord(int index) const
  {
    if (index == 0)
    {
      return tevindref_bc0;
    }
    else if (index == 1)
    {
      return tevindref_bc1;
    }
    else if (index == 2)
    {
      return tevindref_bc3;
    }
    else if (index == 3)
    {
      return tevindref_bc4;
    }
    return 0;
  }

  u32 GetTevindirefMap(int index) const
  {
    if (index == 0)
    {
      return tevindref_bi0;
    }
    else if (index == 1)
    {
      return tevindref_bi1;
    }
    else if (index == 2)
    {
      return tevindref_bi2;
    }
    else if (index == 3)
    {
      return tevindref_bi4;
    }
    return 0;
  }

  struct
  {
    // TODO: Can save a lot space by removing the padding bits
    u32 cc : 24;
    u32 ac : 24;  // tswap and rswap are left blank (encoded into the tevksel fields below)

    u32 tevorders_texmap : 3;
    u32 tevorders_texcoord : 3;
    u32 tevorders_enable : 1;
    RasColorChan tevorders_colorchan : 3;
    u32 pad1 : 6;

    // TODO: Clean up the swapXY mess
    u32 hasindstage : 1;
    u32 tevind : 21;
    u32 tevksel_swap1a : 2;
    u32 tevksel_swap2a : 2;
    u32 tevksel_swap1b : 2;
    u32 tevksel_swap2b : 2;
    u32 pad2 : 2;

    u32 tevksel_swap1c : 2;
    u32 tevksel_swap2c : 2;
    u32 tevksel_swap1d : 2;
    u32 tevksel_swap2d : 2;
    KonstSel tevksel_kc : 5;
    KonstSel tevksel_ka : 5;
    u32 pad3 : 14;
  } stagehash[16];

  LightingUidData lighting;
};
#pragma pack()

using PixelShaderUid = ShaderUid<pixel_shader_uid_data>;

ShaderCode GeneratePixelShaderCode(APIType api_type, const ShaderHostConfig& host_config,
                                   const pixel_shader_uid_data* uid_data);
void WritePixelShaderCommonHeader(ShaderCode& out, APIType api_type, u32 num_texgens,
                                  const ShaderHostConfig& host_config, bool bounding_box);
void ClearUnusedPixelShaderUidBits(APIType api_type, const ShaderHostConfig& host_config,
                                   PixelShaderUid* uid);
PixelShaderUid GetPixelShaderUid();
