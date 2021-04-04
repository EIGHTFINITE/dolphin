// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/UberShaderPixel.h"

#include "VideoCommon/BPMemory.h"
#include "VideoCommon/DriverDetails.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/ShaderGenCommon.h"
#include "VideoCommon/UberShaderCommon.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

namespace UberShader
{
PixelShaderUid GetPixelShaderUid()
{
  PixelShaderUid out;

  pixel_ubershader_uid_data* const uid_data = out.GetUidData();
  uid_data->num_texgens = xfmem.numTexGen.numTexGens;
  uid_data->early_depth = bpmem.UseEarlyDepthTest() &&
                          (g_ActiveConfig.bFastDepthCalc ||
                           bpmem.alpha_test.TestResult() == AlphaTestResult::Undetermined) &&
                          !(bpmem.zmode.testenable && bpmem.genMode.zfreeze);
  uid_data->per_pixel_depth =
      (bpmem.ztex2.op != ZTexOp::Disabled && bpmem.UseLateDepthTest()) ||
      (!g_ActiveConfig.bFastDepthCalc && bpmem.zmode.testenable && !uid_data->early_depth) ||
      (bpmem.zmode.testenable && bpmem.genMode.zfreeze);
  uid_data->uint_output = bpmem.blendmode.UseLogicOp();

  return out;
}

void ClearUnusedPixelShaderUidBits(APIType ApiType, const ShaderHostConfig& host_config,
                                   PixelShaderUid* uid)
{
  pixel_ubershader_uid_data* const uid_data = uid->GetUidData();

  // OpenGL and Vulkan convert implicitly normalized color outputs to their uint representation.
  // Therefore, it is not necessary to use a uint output on these backends. We also disable the
  // uint output when logic op is not supported (i.e. driver/device does not support D3D11.1).
  if (ApiType != APIType::D3D || !host_config.backend_logic_op)
    uid_data->uint_output = 0;
}

ShaderCode GenPixelShader(APIType ApiType, const ShaderHostConfig& host_config,
                          const pixel_ubershader_uid_data* uid_data)
{
  const bool per_pixel_lighting = host_config.per_pixel_lighting;
  const bool msaa = host_config.msaa;
  const bool ssaa = host_config.ssaa;
  const bool stereo = host_config.stereo;
  const bool use_dual_source = host_config.backend_dual_source_blend;
  const bool use_shader_blend = !use_dual_source && host_config.backend_shader_framebuffer_fetch;
  const bool early_depth = uid_data->early_depth != 0;
  const bool per_pixel_depth = uid_data->per_pixel_depth != 0;
  const bool bounding_box = host_config.bounding_box;
  const u32 numTexgen = uid_data->num_texgens;
  ShaderCode out;

  out.Write("// Pixel UberShader for {} texgens{}{}\n", numTexgen,
            early_depth ? ", early-depth" : "", per_pixel_depth ? ", per-pixel depth" : "");
  WritePixelShaderCommonHeader(out, ApiType, numTexgen, host_config, bounding_box);
  WriteUberShaderCommonHeader(out, ApiType, host_config);
  if (per_pixel_lighting)
    WriteLightingFunction(out);

  // Shader inputs/outputs in GLSL (HLSL is in main).
  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
  {
    if (use_dual_source)
    {
      if (DriverDetails::HasBug(DriverDetails::BUG_BROKEN_FRAGMENT_SHADER_INDEX_DECORATION))
      {
        out.Write("FRAGMENT_OUTPUT_LOCATION(0) out vec4 ocol0;\n"
                  "FRAGMENT_OUTPUT_LOCATION(1) out vec4 ocol1;\n");
      }
      else
      {
        out.Write("FRAGMENT_OUTPUT_LOCATION_INDEXED(0, 0) out vec4 ocol0;\n"
                  "FRAGMENT_OUTPUT_LOCATION_INDEXED(0, 1) out vec4 ocol1;\n");
      }
    }
    else if (use_shader_blend)
    {
      // QComm's Adreno driver doesn't seem to like using the framebuffer_fetch value as an
      // intermediate value with multiple reads & modifications, so pull out the "real" output value
      // and use a temporary for calculations, then set the output value once at the end of the
      // shader
      if (DriverDetails::HasBug(DriverDetails::BUG_BROKEN_FRAGMENT_SHADER_INDEX_DECORATION))
      {
        out.Write("FRAGMENT_OUTPUT_LOCATION(0) FRAGMENT_INOUT vec4 real_ocol0;\n");
      }
      else
      {
        out.Write("FRAGMENT_OUTPUT_LOCATION_INDEXED(0, 0) FRAGMENT_INOUT vec4 real_ocol0;\n");
      }
    }
    else
    {
      out.Write("FRAGMENT_OUTPUT_LOCATION(0) out vec4 ocol0;\n");
    }

    if (per_pixel_depth)
      out.Write("#define depth gl_FragDepth\n");

    if (host_config.backend_geometry_shaders)
    {
      out.Write("VARYING_LOCATION(0) in VertexData {{\n");
      GenerateVSOutputMembers(out, ApiType, numTexgen, host_config,
                              GetInterpolationQualifier(msaa, ssaa, true, true));

      if (stereo)
        out.Write("  flat int layer;\n");

      out.Write("}};\n\n");
    }
    else
    {
      // Let's set up attributes
      u32 counter = 0;
      out.Write("VARYING_LOCATION({}) {} in float4 colors_0;\n", counter++,
                GetInterpolationQualifier(msaa, ssaa));
      out.Write("VARYING_LOCATION({}) {} in float4 colors_1;\n", counter++,
                GetInterpolationQualifier(msaa, ssaa));
      for (u32 i = 0; i < numTexgen; ++i)
      {
        out.Write("VARYING_LOCATION({}) {} in float3 tex{};\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa), i);
      }
      if (!host_config.fast_depth_calc)
      {
        out.Write("VARYING_LOCATION({}) {} in float4 clipPos;\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa));
      }
      if (per_pixel_lighting)
      {
        out.Write("VARYING_LOCATION({}) {} in float3 Normal;\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa));
        out.Write("VARYING_LOCATION({}) {} in float3 WorldPos;\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa));
      }
    }
  }

  // Uniform index -> texture coordinates
  if (numTexgen > 0)
  {
    if (ApiType != APIType::D3D)
    {
      out.Write("float3 selectTexCoord(uint index) {{\n");
    }
    else
    {
      out.Write("float3 selectTexCoord(uint index");
      for (u32 i = 0; i < numTexgen; i++)
        out.Write(", float3 tex{}", i);
      out.Write(") {{\n");
    }

    if (ApiType == APIType::D3D)
    {
      out.Write("  switch (index) {{\n");
      for (u32 i = 0; i < numTexgen; i++)
      {
        out.Write("  case {}u:\n"
                  "    return tex{};\n",
                  i, i);
      }
      out.Write("  default:\n"
                "    return float3(0.0, 0.0, 0.0);\n"
                "  }}\n");
    }
    else
    {
      if (numTexgen > 4)
        out.Write("  if (index < 4u) {{\n");
      if (numTexgen > 2)
        out.Write("    if (index < 2u) {{\n");
      if (numTexgen > 1)
        out.Write("      return (index == 0u) ? tex0 : tex1;\n");
      else
        out.Write("      return (index == 0u) ? tex0 : float3(0.0, 0.0, 0.0);\n");
      if (numTexgen > 2)
      {
        out.Write("    }} else {{\n");  // >= 2
        if (numTexgen > 3)
          out.Write("      return (index == 2u) ? tex2 : tex3;\n");
        else
          out.Write("      return (index == 2u) ? tex2 : float3(0.0, 0.0, 0.0);\n");
        out.Write("    }}\n");
      }
      if (numTexgen > 4)
      {
        out.Write("  }} else {{\n");  // >= 4 <= 8
        if (numTexgen > 6)
          out.Write("    if (index < 6u) {{\n");
        if (numTexgen > 5)
          out.Write("      return (index == 4u) ? tex4 : tex5;\n");
        else
          out.Write("      return (index == 4u) ? tex4 : float3(0.0, 0.0, 0.0);\n");
        if (numTexgen > 6)
        {
          out.Write("    }} else {{\n");  // >= 6 <= 8
          if (numTexgen > 7)
            out.Write("      return (index == 6u) ? tex6 : tex7;\n");
          else
            out.Write("      return (index == 6u) ? tex6 : float3(0.0, 0.0, 0.0);\n");
          out.Write("    }}\n");
        }
        out.Write("  }}\n");
      }
    }

    out.Write("}}\n\n");
  }

  // =====================
  //   Texture Sampling
  // =====================

  if (host_config.backend_dynamic_sampler_indexing)
  {
    // Doesn't look like DirectX supports this. Oh well the code path is here just in case it
    // supports this in the future.
    out.Write("int4 sampleTexture(uint sampler_num, float3 uv) {{\n");
    if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
      out.Write("  return iround(texture(samp[sampler_num], uv) * 255.0);\n");
    else if (ApiType == APIType::D3D)
      out.Write("  return iround(Tex[sampler_num].Sample(samp[sampler_num], uv) * 255.0);\n");
    out.Write("}}\n\n");
  }
  else
  {
    out.Write("int4 sampleTexture(uint sampler_num, float3 uv) {{\n"
              "  // This is messy, but DirectX, OpenGL 3.3 and OpenGL ES 3.0 doesn't support "
              "dynamic indexing of the sampler array\n"
              "  // With any luck the shader compiler will optimise this if the hardware supports "
              "dynamic indexing.\n"
              "  switch(sampler_num) {{\n");
    for (int i = 0; i < 8; i++)
    {
      if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
        out.Write("  case {}u: return iround(texture(samp[{}], uv) * 255.0);\n", i, i);
      else if (ApiType == APIType::D3D)
        out.Write("  case {}u: return iround(Tex[{}].Sample(samp[{}], uv) * 255.0);\n", i, i, i);
    }
    out.Write("  }}\n"
              "}}\n\n");
  }

  // ======================
  //   Arbitrary Swizzling
  // ======================

  out.Write("int4 Swizzle(uint s, int4 color) {{\n"
            "  // AKA: Color Channel Swapping\n"
            "\n"
            "  int4 ret;\n");
  out.Write("  ret.r = color[{}];\n", BitfieldExtract("bpmem_tevksel(s * 2u)", TevKSel().swap1));
  out.Write("  ret.g = color[{}];\n", BitfieldExtract("bpmem_tevksel(s * 2u)", TevKSel().swap2));
  out.Write("  ret.b = color[{}];\n",
            BitfieldExtract("bpmem_tevksel(s * 2u + 1u)", TevKSel().swap1));
  out.Write("  ret.a = color[{}];\n",
            BitfieldExtract("bpmem_tevksel(s * 2u + 1u)", TevKSel().swap2));
  out.Write("  return ret;\n"
            "}}\n\n");

  // ======================
  //   Indirect Wrapping
  // ======================
  out.Write("int Wrap(int coord, uint mode) {{\n"
            "  if (mode == 0u) // ITW_OFF\n"
            "    return coord;\n"
            "  else if (mode < 6u) // ITW_256 to ITW_16\n"
            "    return coord & (0xfffe >> mode);\n"
            "  else // ITW_0\n"
            "    return 0;\n"
            "}}\n\n");

  // ======================
  //    Indirect Lookup
  // ======================
  const auto LookupIndirectTexture = [&out, stereo](std::string_view out_var_name,
                                                    std::string_view in_index_name) {
    out.Write("{{\n"
              "  uint iref = bpmem_iref({});\n"
              "  if ( iref != 0u)\n"
              "  {{\n"
              "    uint texcoord = bitfieldExtract(iref, 0, 3);\n"
              "    uint texmap = bitfieldExtract(iref, 8, 3);\n"
              "    float3 uv = getTexCoord(texcoord);\n"
              "    int2 fixedPoint_uv = int2((uv.z == 0.0 ? uv.xy : (uv.xy / uv.z)) * " I_TEXDIMS
              "[texcoord].zw);\n"
              "\n"
              "    if (({} & 1u) == 0u)\n"
              "      fixedPoint_uv = fixedPoint_uv >> " I_INDTEXSCALE "[{} >> 1].xy;\n"
              "    else\n"
              "      fixedPoint_uv = fixedPoint_uv >> " I_INDTEXSCALE "[{} >> 1].zw;\n"
              "\n"
              "    {} = sampleTexture(texmap, float3(float2(fixedPoint_uv) * " I_TEXDIMS
              "[texmap].xy, {})).abg;\n",
              in_index_name, in_index_name, in_index_name, in_index_name, out_var_name,
              stereo ? "float(layer)" : "0.0");
    out.Write("  }}\n"
              "  else\n"
              "  {{\n"
              "    {} = int3(0, 0, 0);\n"
              "  }}\n"
              "}}\n",
              out_var_name);
  };

  // ======================
  //   TEV's Special Lerp
  // ======================
  const auto WriteTevLerp = [&out](std::string_view components) {
    out.Write(
        "// TEV's Linear Interpolate, plus bias, add/subtract and scale\n"
        "int{0} tevLerp{0}(int{0} A, int{0} B, int{0} C, int{0} D, uint bias, bool op, bool alpha, "
        "uint shift) {{\n"
        " // Scale C from 0..255 to 0..256\n"
        "  C += C >> 7;\n"
        "\n"
        " // Add bias to D\n"
        "  if (bias == 1u) D += 128;\n"
        "  else if (bias == 2u) D -= 128;\n"
        "\n"
        "  int{0} lerp = (A << 8) + (B - A)*C;\n"
        "  if (shift != 3u) {{\n"
        "    lerp = lerp << shift;\n"
        "    D = D << shift;\n"
        "  }}\n"
        "\n"
        "  if ((shift == 3u) == alpha)\n"
        "    lerp = lerp + (op ? 127 : 128);\n"
        "\n"
        "  int{0} result = lerp >> 8;\n"
        "\n"
        "  // Add/Subtract D\n"
        "  if (op) // Subtract\n"
        "    result = D - result;\n"
        "  else // Add\n"
        "    result = D + result;\n"
        "\n"
        "  // Most of the Shift was moved inside the lerp for improved precision\n"
        "  // But we still do the divide by 2 here\n"
        "  if (shift == 3u)\n"
        "    result = result >> 1;\n"
        "  return result;\n"
        "}}\n\n",
        components);
  };
  WriteTevLerp("");   // int
  WriteTevLerp("3");  // int3

  // =======================
  //   TEV's Color Compare
  // =======================

  out.Write(
      "// Implements operations 0-5 of TEV's compare mode,\n"
      "// which are common to both color and alpha channels\n"
      "bool tevCompare(uint op, int3 color_A, int3 color_B) {{\n"
      "  switch (op) {{\n"
      "  case 0u: // TevCompareMode::R8, TevComparison::GT\n"
      "    return (color_A.r > color_B.r);\n"
      "  case 1u: // TevCompareMode::R8, TevComparison::EQ\n"
      "    return (color_A.r == color_B.r);\n"
      "  case 2u: // TevCompareMode::GR16, TevComparison::GT\n"
      "    int A_16 = (color_A.r | (color_A.g << 8));\n"
      "    int B_16 = (color_B.r | (color_B.g << 8));\n"
      "    return A_16 > B_16;\n"
      "  case 3u: // TevCompareMode::GR16, TevComparison::EQ\n"
      "    return (color_A.r == color_B.r && color_A.g == color_B.g);\n"
      "  case 4u: // TevCompareMode::BGR24, TevComparison::GT\n"
      "    int A_24 = (color_A.r | (color_A.g << 8) | (color_A.b << 16));\n"
      "    int B_24 = (color_B.r | (color_B.g << 8) | (color_B.b << 16));\n"
      "    return A_24 > B_24;\n"
      "  case 5u: // TevCompareMode::BGR24, TevComparison::EQ\n"
      "    return (color_A.r == color_B.r && color_A.g == color_B.g && color_A.b == color_B.b);\n"
      "  default:\n"
      "    return false;\n"
      "  }}\n"
      "}}\n\n");

  // =================
  //   Input Selects
  // =================

  out.Write("struct State {{\n"
            "  int4 Reg[4];\n"
            "  int4 TexColor;\n"
            "  int AlphaBump;\n"
            "}};\n"
            "struct StageState {{\n"
            "  uint stage;\n"
            "  uint order;\n"
            "  uint cc;\n"
            "  uint ac;\n"
            "}};\n"
            "\n"
            "int4 getRasColor(State s, StageState ss, float4 colors_0, float4 colors_1);\n"
            "int4 getKonstColor(State s, StageState ss);\n"
            "\n");

  // The switch statements in these functions appear to get transformed into an if..else chain
  // on NVIDIA's OpenGL/Vulkan drivers, resulting in lower performance than the D3D counterparts.
  // Transforming the switch into a binary tree of ifs can increase performance by up to 20%.
  if (ApiType == APIType::D3D)
  {
    out.Write("// Helper function for Alpha Test\n"
              "bool alphaCompare(int a, int b, uint compare) {{\n"
              "  switch (compare) {{\n"
              "  case 0u: // NEVER\n"
              "    return false;\n"
              "  case 1u: // LESS\n"
              "    return a < b;\n"
              "  case 2u: // EQUAL\n"
              "    return a == b;\n"
              "  case 3u: // LEQUAL\n"
              "    return a <= b;\n"
              "  case 4u: // GREATER\n"
              "    return a > b;\n"
              "  case 5u: // NEQUAL;\n"
              "    return a != b;\n"
              "  case 6u: // GEQUAL\n"
              "    return a >= b;\n"
              "  case 7u: // ALWAYS\n"
              "    return true;\n"
              "  }}\n"
              "}}\n"
              "\n"
              "int3 selectColorInput(State s, StageState ss, float4 colors_0, float4 colors_1, "
              "uint index) {{\n"
              "  switch (index) {{\n"
              "  case 0u: // prev.rgb\n"
              "    return s.Reg[0].rgb;\n"
              "  case 1u: // prev.aaa\n"
              "    return s.Reg[0].aaa;\n"
              "  case 2u: // c0.rgb\n"
              "    return s.Reg[1].rgb;\n"
              "  case 3u: // c0.aaa\n"
              "    return s.Reg[1].aaa;\n"
              "  case 4u: // c1.rgb\n"
              "    return s.Reg[2].rgb;\n"
              "  case 5u: // c1.aaa\n"
              "    return s.Reg[2].aaa;\n"
              "  case 6u: // c2.rgb\n"
              "    return s.Reg[3].rgb;\n"
              "  case 7u: // c2.aaa\n"
              "    return s.Reg[3].aaa;\n"
              "  case 8u:\n"
              "    return s.TexColor.rgb;\n"
              "  case 9u:\n"
              "    return s.TexColor.aaa;\n"
              "  case 10u:\n"
              "    return getRasColor(s, ss, colors_0, colors_1).rgb;\n"
              "  case 11u:\n"
              "    return getRasColor(s, ss, colors_0, colors_1).aaa;\n"
              "  case 12u: // One\n"
              "    return int3(255, 255, 255);\n"
              "  case 13u: // Half\n"
              "    return int3(128, 128, 128);\n"
              "  case 14u:\n"
              "    return getKonstColor(s, ss).rgb;\n"
              "  case 15u: // Zero\n"
              "    return int3(0, 0, 0);\n"
              "  }}\n"
              "}}\n"
              "\n"
              "int selectAlphaInput(State s, StageState ss, float4 colors_0, float4 colors_1, "
              "uint index) {{\n"
              "  switch (index) {{\n"
              "  case 0u: // prev.a\n"
              "    return s.Reg[0].a;\n"
              "  case 1u: // c0.a\n"
              "    return s.Reg[1].a;\n"
              "  case 2u: // c1.a\n"
              "    return s.Reg[2].a;\n"
              "  case 3u: // c2.a\n"
              "    return s.Reg[3].a;\n"
              "  case 4u:\n"
              "    return s.TexColor.a;\n"
              "  case 5u:\n"
              "    return getRasColor(s, ss, colors_0, colors_1).a;\n"
              "  case 6u:\n"
              "    return getKonstColor(s, ss).a;\n"
              "  case 7u: // Zero\n"
              "    return 0;\n"
              "  }}\n"
              "}}\n"
              "\n"
              "int4 getTevReg(in State s, uint index) {{\n"
              "  switch (index) {{\n"
              "  case 0u: // prev\n"
              "    return s.Reg[0];\n"
              "  case 1u: // c0\n"
              "    return s.Reg[1];\n"
              "  case 2u: // c1\n"
              "    return s.Reg[2];\n"
              "  case 3u: // c2\n"
              "    return s.Reg[3];\n"
              "  default: // prev\n"
              "    return s.Reg[0];\n"
              "  }}\n"
              "}}\n"
              "\n"
              "void setRegColor(inout State s, uint index, int3 color) {{\n"
              "  switch (index) {{\n"
              "  case 0u: // prev\n"
              "    s.Reg[0].rgb = color;\n"
              "    break;\n"
              "  case 1u: // c0\n"
              "    s.Reg[1].rgb = color;\n"
              "    break;\n"
              "  case 2u: // c1\n"
              "    s.Reg[2].rgb = color;\n"
              "    break;\n"
              "  case 3u: // c2\n"
              "    s.Reg[3].rgb = color;\n"
              "    break;\n"
              "  }}\n"
              "}}\n"
              "\n"
              "void setRegAlpha(inout State s, uint index, int alpha) {{\n"
              "  switch (index) {{\n"
              "  case 0u: // prev\n"
              "    s.Reg[0].a = alpha;\n"
              "    break;\n"
              "  case 1u: // c0\n"
              "    s.Reg[1].a = alpha;\n"
              "    break;\n"
              "  case 2u: // c1\n"
              "    s.Reg[2].a = alpha;\n"
              "    break;\n"
              "  case 3u: // c2\n"
              "    s.Reg[3].a = alpha;\n"
              "    break;\n"
              "  }}\n"
              "}}\n"
              "\n");
  }
  else
  {
    out.Write(
        "// Helper function for Alpha Test\n"
        "bool alphaCompare(int a, int b, uint compare) {{\n"
        "  if (compare < 4u) {{\n"
        "    if (compare < 2u) {{\n"
        "      return (compare == 0u) ? (false) : (a < b);\n"
        "    }} else {{\n"
        "      return (compare == 2u) ? (a == b) : (a <= b);\n"
        "    }}\n"
        "  }} else {{\n"
        "    if (compare < 6u) {{\n"
        "      return (compare == 4u) ? (a > b) : (a != b);\n"
        "    }} else {{\n"
        "      return (compare == 6u) ? (a >= b) : (true);\n"
        "    }}\n"
        "  }}\n"
        "}}\n"
        "\n"
        "int3 selectColorInput(State s, StageState ss, float4 colors_0, float4 colors_1, "
        "uint index) {{\n"
        "  if (index < 8u) {{\n"
        "    if (index < 4u) {{\n"
        "      if (index < 2u) {{\n"
        "        return (index == 0u) ? s.Reg[0].rgb : s.Reg[0].aaa;\n"
        "      }} else {{\n"
        "        return (index == 2u) ? s.Reg[1].rgb : s.Reg[1].aaa;\n"
        "      }}\n"
        "    }} else {{\n"
        "      if (index < 6u) {{\n"
        "        return (index == 4u) ? s.Reg[2].rgb : s.Reg[2].aaa;\n"
        "      }} else {{\n"
        "        return (index == 6u) ? s.Reg[3].rgb : s.Reg[3].aaa;\n"
        "      }}\n"
        "    }}\n"
        "  }} else {{\n"
        "    if (index < 12u) {{\n"
        "      if (index < 10u) {{\n"
        "        return (index == 8u) ? s.TexColor.rgb : s.TexColor.aaa;\n"
        "      }} else {{\n"
        "        int4 ras = getRasColor(s, ss, colors_0, colors_1);\n"
        "        return (index == 10u) ? ras.rgb : ras.aaa;\n"
        "      }}\n"
        "    }} else {{\n"
        "      if (index < 14u) {{\n"
        "        return (index == 12u) ? int3(255, 255, 255) : int3(128, 128, 128);\n"
        "      }} else {{\n"
        "        return (index == 14u) ? getKonstColor(s, ss).rgb : int3(0, 0, 0);\n"
        "      }}\n"
        "    }}\n"
        "  }}\n"
        "}}\n"
        "\n"
        "int selectAlphaInput(State s, StageState ss, float4 colors_0, float4 colors_1, "
        "uint index) {{\n"
        "  if (index < 4u) {{\n"
        "    if (index < 2u) {{\n"
        "      return (index == 0u) ? s.Reg[0].a : s.Reg[1].a;\n"
        "    }} else {{\n"
        "      return (index == 2u) ? s.Reg[2].a : s.Reg[3].a;\n"
        "    }}\n"
        "  }} else {{\n"
        "    if (index < 6u) {{\n"
        "      return (index == 4u) ? s.TexColor.a : getRasColor(s, ss, colors_0, colors_1).a;\n"
        "    }} else {{\n"
        "      return (index == 6u) ? getKonstColor(s, ss).a : 0;\n"
        "    }}\n"
        "  }}\n"
        "}}\n"
        "\n"
        "int4 getTevReg(in State s, uint index) {{\n"
        "  if (index < 2u) {{\n"
        "    if (index == 0u) {{\n"
        "      return s.Reg[0];\n"
        "    }} else {{\n"
        "      return s.Reg[1];\n"
        "    }}\n"
        "  }} else {{\n"
        "    if (index == 2u) {{\n"
        "      return s.Reg[2];\n"
        "    }} else {{\n"
        "      return s.Reg[3];\n"
        "    }}\n"
        "  }}\n"
        "}}\n"
        "\n"
        "void setRegColor(inout State s, uint index, int3 color) {{\n"
        "  if (index < 2u) {{\n"
        "    if (index == 0u) {{\n"
        "      s.Reg[0].rgb = color;\n"
        "    }} else {{\n"
        "      s.Reg[1].rgb = color;\n"
        "    }}\n"
        "  }} else {{\n"
        "    if (index == 2u) {{\n"
        "      s.Reg[2].rgb = color;\n"
        "    }} else {{\n"
        "      s.Reg[3].rgb = color;\n"
        "    }}\n"
        "  }}\n"
        "}}\n"
        "\n"
        "void setRegAlpha(inout State s, uint index, int alpha) {{\n"
        "  if (index < 2u) {{\n"
        "    if (index == 0u) {{\n"
        "      s.Reg[0].a = alpha;\n"
        "    }} else {{\n"
        "      s.Reg[1].a = alpha;\n"
        "    }}\n"
        "  }} else {{\n"
        "    if (index == 2u) {{\n"
        "      s.Reg[2].a = alpha;\n"
        "    }} else {{\n"
        "      s.Reg[3].a = alpha;\n"
        "    }}\n"
        "  }}\n"
        "}}\n"
        "\n");
  }

  // Since the texture coodinate variables aren't global, we need to pass
  // them to the select function in D3D.
  if (numTexgen > 0)
  {
    if (ApiType != APIType::D3D)
    {
      out.Write("#define getTexCoord(index) selectTexCoord((index))\n\n");
    }
    else
    {
      out.Write("#define getTexCoord(index) selectTexCoord((index)");
      for (u32 i = 0; i < numTexgen; i++)
        out.Write(", tex{}", i);
      out.Write(")\n\n");
    }
  }

  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
  {
    if (early_depth && host_config.backend_early_z)
      out.Write("FORCE_EARLY_Z;\n");

    out.Write("void main()\n{{\n");
    out.Write("  float4 rawpos = gl_FragCoord;\n");
    if (use_shader_blend)
    {
      // Store off a copy of the initial fb value for blending
      out.Write("  float4 initial_ocol0 = FB_FETCH_VALUE;\n"
                "  float4 ocol0;\n"
                "  float4 ocol1;\n");
    }
  }
  else  // D3D
  {
    if (early_depth && host_config.backend_early_z)
      out.Write("[earlydepthstencil]\n");

    out.Write("void main(\n");
    if (uid_data->uint_output)
    {
      out.Write("  out uint4 ocol0 : SV_Target,\n");
    }
    else
    {
      out.Write("  out float4 ocol0 : SV_Target0,\n"
                "  out float4 ocol1 : SV_Target1,\n");
    }
    if (per_pixel_depth)
      out.Write("  out float depth : SV_Depth,\n");
    out.Write("  in float4 rawpos : SV_Position,\n");
    out.Write("  in {} float4 colors_0 : COLOR0,\n", GetInterpolationQualifier(msaa, ssaa));
    out.Write("  in {} float4 colors_1 : COLOR1", GetInterpolationQualifier(msaa, ssaa));

    // compute window position if needed because binding semantic WPOS is not widely supported
    for (u32 i = 0; i < numTexgen; ++i)
    {
      out.Write(",\n  in {} float3 tex{} : TEXCOORD{}", GetInterpolationQualifier(msaa, ssaa), i,
                i);
    }
    if (!host_config.fast_depth_calc)
    {
      out.Write("\n,\n  in {} float4 clipPos : TEXCOORD{}", GetInterpolationQualifier(msaa, ssaa),
                numTexgen);
    }
    if (per_pixel_lighting)
    {
      out.Write(",\n  in {} float3 Normal : TEXCOORD{}", GetInterpolationQualifier(msaa, ssaa),
                numTexgen + 1);
      out.Write(",\n  in {} float3 WorldPos : TEXCOORD{}", GetInterpolationQualifier(msaa, ssaa),
                numTexgen + 2);
    }
    out.Write(",\n  in float clipDist0 : SV_ClipDistance0\n"
              ",\n  in float clipDist1 : SV_ClipDistance1\n");
    if (stereo)
      out.Write(",\n  in uint layer : SV_RenderTargetArrayIndex\n");
    out.Write("\n        ) {{\n");
  }

  out.Write("  int3 tevcoord = int3(0, 0, 0);\n"
            "  State s;\n"
            "  s.TexColor = int4(0, 0, 0, 0);\n"
            "  s.AlphaBump = 0;\n"
            "\n");
  for (int i = 0; i < 4; i++)
    out.Write("  s.Reg[{}] = " I_COLORS "[{}];\n", i, i);

  const char* color_input_prefix = "";
  if (per_pixel_lighting)
  {
    out.Write("  float4 lit_colors_0 = colors_0;\n"
              "  float4 lit_colors_1 = colors_1;\n"
              "  float3 lit_normal = normalize(Normal.xyz);\n"
              "  float3 lit_pos = WorldPos.xyz;\n");
    WriteVertexLighting(out, ApiType, "lit_pos", "lit_normal", "colors_0", "colors_1",
                        "lit_colors_0", "lit_colors_1");
    color_input_prefix = "lit_";
  }

  out.Write("  uint num_stages = {};\n\n",
            BitfieldExtract("bpmem_genmode", bpmem.genMode.numtevstages));

  out.Write("  // Main tev loop\n");
  if (ApiType == APIType::D3D)
  {
    // Tell DirectX we don't want this loop unrolled (it crashes if it tries to)
    out.Write("  [loop]\n");
  }

  out.Write("  for(uint stage = 0u; stage <= num_stages; stage++)\n"
            "  {{\n"
            "    StageState ss;\n"
            "    ss.stage = stage;\n"
            "    ss.cc = bpmem_combiners(stage).x;\n"
            "    ss.ac = bpmem_combiners(stage).y;\n"
            "    ss.order = bpmem_tevorder(stage>>1);\n"
            "    if ((stage & 1u) == 1u)\n"
            "      ss.order = ss.order >> {};\n\n",
            int(TwoTevStageOrders().enable1.StartBit() - TwoTevStageOrders().enable0.StartBit()));

  // Disable texturing when there are no texgens (for now)
  if (numTexgen != 0)
  {
    out.Write("    uint tex_coord = {};\n",
              BitfieldExtract("ss.order", TwoTevStageOrders().texcoord0));
    out.Write("    float3 uv = getTexCoord(tex_coord);\n"
              "    int2 fixedPoint_uv = int2((uv.z == 0.0 ? uv.xy : (uv.xy / uv.z)) * " I_TEXDIMS
              "[tex_coord].zw);\n"
              "\n"
              "    bool texture_enabled = (ss.order & {}u) != 0u;\n",
              1 << TwoTevStageOrders().enable0.StartBit());
    out.Write("\n"
              "    // Indirect textures\n"
              "    uint tevind = bpmem_tevind(stage);\n"
              "    if (tevind != 0u)\n"
              "    {{\n"
              "      uint bs = {};\n",
              BitfieldExtract("tevind", TevStageIndirect().bs));
    out.Write("      uint fmt = {};\n", BitfieldExtract("tevind", TevStageIndirect().fmt));
    out.Write("      uint bias = {};\n", BitfieldExtract("tevind", TevStageIndirect().bias));
    out.Write("      uint bt = {};\n", BitfieldExtract("tevind", TevStageIndirect().bt));
    out.Write("      uint mid = {};\n", BitfieldExtract("tevind", TevStageIndirect().mid));
    out.Write("\n");
    out.Write("      int3 indcoord;\n");
    LookupIndirectTexture("indcoord", "bt");
    out.Write("      if (bs != 0u)\n"
              "        s.AlphaBump = indcoord[bs - 1u];\n"
              "      switch(fmt)\n"
              "      {{\n"
              "      case {:s}:\n",
              IndTexFormat::ITF_8);
    out.Write("        indcoord.x = indcoord.x + ((bias & 1u) != 0u ? -128 : 0);\n"
              "        indcoord.y = indcoord.y + ((bias & 2u) != 0u ? -128 : 0);\n"
              "        indcoord.z = indcoord.z + ((bias & 4u) != 0u ? -128 : 0);\n"
              "        s.AlphaBump = s.AlphaBump & 0xf8;\n"
              "        break;\n"
              "      case {:s}:\n",
              IndTexFormat::ITF_5);
    out.Write("        indcoord.x = (indcoord.x & 0x1f) + ((bias & 1u) != 0u ? 1 : 0);\n"
              "        indcoord.y = (indcoord.y & 0x1f) + ((bias & 2u) != 0u ? 1 : 0);\n"
              "        indcoord.z = (indcoord.z & 0x1f) + ((bias & 4u) != 0u ? 1 : 0);\n"
              "        s.AlphaBump = s.AlphaBump & 0xe0;\n"
              "        break;\n"
              "      case {:s}:\n",
              IndTexFormat::ITF_4);
    out.Write("        indcoord.x = (indcoord.x & 0x0f) + ((bias & 1u) != 0u ? 1 : 0);\n"
              "        indcoord.y = (indcoord.y & 0x0f) + ((bias & 2u) != 0u ? 1 : 0);\n"
              "        indcoord.z = (indcoord.z & 0x0f) + ((bias & 4u) != 0u ? 1 : 0);\n"
              "        s.AlphaBump = s.AlphaBump & 0xf0;\n"
              "        break;\n"
              "      case {:s}:\n",
              IndTexFormat::ITF_3);
    out.Write("        indcoord.x = (indcoord.x & 0x07) + ((bias & 1u) != 0u ? 1 : 0);\n"
              "        indcoord.y = (indcoord.y & 0x07) + ((bias & 2u) != 0u ? 1 : 0);\n"
              "        indcoord.z = (indcoord.z & 0x07) + ((bias & 4u) != 0u ? 1 : 0);\n"
              "        s.AlphaBump = s.AlphaBump & 0xf8;\n"
              "        break;\n"
              "      }}\n"
              "\n"
              "      // Matrix multiply\n"
              "      int2 indtevtrans = int2(0, 0);\n"
              "      if ((mid & 3u) != 0u)\n"
              "      {{\n"
              "        uint mtxidx = 2u * ((mid & 3u) - 1u);\n"
              "        int shift = " I_INDTEXMTX "[mtxidx].w;\n"
              "\n"
              "        switch (mid >> 2)\n"
              "        {{\n"
              "        case 0u: // 3x2 S0.10 matrix\n"
              "          indtevtrans = int2(idot(" I_INDTEXMTX
              "[mtxidx].xyz, indcoord), idot(" I_INDTEXMTX "[mtxidx + 1u].xyz, indcoord)) >> 3;\n"
              "          break;\n"
              "        case 1u: // S matrix, S17.7 format\n"
              "          indtevtrans = (fixedPoint_uv * indcoord.xx) >> 8;\n"
              "          break;\n"
              "        case 2u: // T matrix, S17.7 format\n"
              "          indtevtrans = (fixedPoint_uv * indcoord.yy) >> 8;\n"
              "          break;\n"
              "        }}\n"
              "\n"
              "        if (shift >= 0)\n"
              "          indtevtrans = indtevtrans >> shift;\n"
              "        else\n"
              "          indtevtrans = indtevtrans << ((-shift) & 31);\n"
              "      }}\n"
              "\n"
              "      // Wrapping\n"
              "      uint sw = {};\n",
              BitfieldExtract("tevind", TevStageIndirect().sw));
    out.Write("      uint tw = {}; \n", BitfieldExtract("tevind", TevStageIndirect().tw));
    out.Write(
        "      int2 wrapped_coord = int2(Wrap(fixedPoint_uv.x, sw), Wrap(fixedPoint_uv.y, tw));\n"
        "\n"
        "      if ((tevind & {}u) != 0u) // add previous tevcoord\n",
        1 << TevStageIndirect().fb_addprev.StartBit());
    out.Write("        tevcoord.xy += wrapped_coord + indtevtrans;\n"
              "      else\n"
              "        tevcoord.xy = wrapped_coord + indtevtrans;\n"
              "\n"
              "      // Emulate s24 overflows\n"
              "      tevcoord.xy = (tevcoord.xy << 8) >> 8;\n"
              "    }}\n"
              "    else if (texture_enabled)\n"
              "    {{\n"
              "      tevcoord.xy = fixedPoint_uv;\n"
              "    }}\n"
              "\n"
              "    // Sample texture for stage\n"
              "    if (texture_enabled) {{\n"
              "      uint sampler_num = {};\n",
              BitfieldExtract("ss.order", TwoTevStageOrders().texmap0));
    out.Write("\n"
              "      float2 uv = (float2(tevcoord.xy)) * " I_TEXDIMS "[sampler_num].xy;\n");
    out.Write("      int4 color = sampleTexture(sampler_num, float3(uv, {}));\n",
              stereo ? "float(layer)" : "0.0");
    out.Write("      uint swap = {};\n", BitfieldExtract("ss.ac", TevStageCombiner().alphaC.tswap));
    out.Write("      s.TexColor = Swizzle(swap, color);\n");
    out.Write("    }} else {{\n"
              "      // Texture is disabled\n"
              "      s.TexColor = int4(255, 255, 255, 255);\n"
              "    }}\n"
              "\n");
  }

  out.Write("    // This is the Meat of TEV\n"
            "    {{\n"
            "      // Color Combiner\n");
  out.Write("      uint color_a = {};\n", BitfieldExtract("ss.cc", TevStageCombiner().colorC.a));
  out.Write("      uint color_b = {};\n", BitfieldExtract("ss.cc", TevStageCombiner().colorC.b));
  out.Write("      uint color_c = {};\n", BitfieldExtract("ss.cc", TevStageCombiner().colorC.c));
  out.Write("      uint color_d = {};\n", BitfieldExtract("ss.cc", TevStageCombiner().colorC.d));

  out.Write("      uint color_bias = {};\n",
            BitfieldExtract("ss.cc", TevStageCombiner().colorC.bias));
  out.Write("      bool color_op = bool({});\n",
            BitfieldExtract("ss.cc", TevStageCombiner().colorC.op));
  out.Write("      bool color_clamp = bool({});\n",
            BitfieldExtract("ss.cc", TevStageCombiner().colorC.clamp));
  out.Write("      uint color_shift = {};\n",
            BitfieldExtract("ss.cc", TevStageCombiner().colorC.scale));
  out.Write("      uint color_dest = {};\n",
            BitfieldExtract("ss.cc", TevStageCombiner().colorC.dest));

  out.Write(
      "      uint color_compare_op = color_shift << 1 | uint(color_op);\n"
      "\n"
      "      int3 color_A = selectColorInput(s, ss, {0}colors_0, {0}colors_1, color_a) & "
      "int3(255, 255, 255);\n"
      "      int3 color_B = selectColorInput(s, ss, {0}colors_0, {0}colors_1, color_b) & "
      "int3(255, 255, 255);\n"
      "      int3 color_C = selectColorInput(s, ss, {0}colors_0, {0}colors_1, color_c) & "
      "int3(255, 255, 255);\n"
      "      int3 color_D = selectColorInput(s, ss, {0}colors_0, {0}colors_1, color_d);  // 10 "
      "bits + sign\n"
      "\n",  // TODO: do we need to sign extend?
      color_input_prefix);
  out.Write(
      "      int3 color;\n"
      "      if (color_bias != 3u) {{ // Normal mode\n"
      "        color = tevLerp3(color_A, color_B, color_C, color_D, color_bias, color_op, false, "
      "color_shift);\n"
      "      }} else {{ // Compare mode\n"
      "        // op 6 and 7 do a select per color channel\n"
      "        if (color_compare_op == 6u) {{\n"
      "          // TevCompareMode::RGB8, TevComparison::GT\n"
      "          color.r = (color_A.r > color_B.r) ? color_C.r : 0;\n"
      "          color.g = (color_A.g > color_B.g) ? color_C.g : 0;\n"
      "          color.b = (color_A.b > color_B.b) ? color_C.b : 0;\n"
      "        }} else if (color_compare_op == 7u) {{\n"
      "          // TevCompareMode::RGB8, TevComparison::EQ\n"
      "          color.r = (color_A.r == color_B.r) ? color_C.r : 0;\n"
      "          color.g = (color_A.g == color_B.g) ? color_C.g : 0;\n"
      "          color.b = (color_A.b == color_B.b) ? color_C.b : 0;\n"
      "        }} else {{\n"
      "          // The remaining ops do one compare which selects all 3 channels\n"
      "          color = tevCompare(color_compare_op, color_A, color_B) ? color_C : int3(0, 0, "
      "0);\n"
      "        }}\n"
      "        color = color_D + color;\n"
      "      }}\n"
      "\n"
      "      // Clamp result\n"
      "      if (color_clamp)\n"
      "        color = clamp(color, 0, 255);\n"
      "      else\n"
      "        color = clamp(color, -1024, 1023);\n"
      "\n"
      "      // Write result to the correct input register of the next stage\n"
      "      setRegColor(s, color_dest, color);\n"
      "\n");

  // Alpha combiner
  out.Write("      // Alpha Combiner\n");
  out.Write("      uint alpha_a = {};\n", BitfieldExtract("ss.ac", TevStageCombiner().alphaC.a));
  out.Write("      uint alpha_b = {};\n", BitfieldExtract("ss.ac", TevStageCombiner().alphaC.b));
  out.Write("      uint alpha_c = {};\n", BitfieldExtract("ss.ac", TevStageCombiner().alphaC.c));
  out.Write("      uint alpha_d = {};\n", BitfieldExtract("ss.ac", TevStageCombiner().alphaC.d));

  out.Write("      uint alpha_bias = {};\n",
            BitfieldExtract("ss.ac", TevStageCombiner().alphaC.bias));
  out.Write("      bool alpha_op = bool({});\n",
            BitfieldExtract("ss.ac", TevStageCombiner().alphaC.op));
  out.Write("      bool alpha_clamp = bool({});\n",
            BitfieldExtract("ss.ac", TevStageCombiner().alphaC.clamp));
  out.Write("      uint alpha_shift = {};\n",
            BitfieldExtract("ss.ac", TevStageCombiner().alphaC.scale));
  out.Write("      uint alpha_dest = {};\n",
            BitfieldExtract("ss.ac", TevStageCombiner().alphaC.dest));

  out.Write(
      "      uint alpha_compare_op = alpha_shift << 1 | uint(alpha_op);\n"
      "\n"
      "      int alpha_A;\n"
      "      int alpha_B;\n"
      "      if (alpha_bias != 3u || alpha_compare_op > 5u) {{\n"
      "        // Small optimisation here: alpha_A and alpha_B are unused by compare ops 0-5\n"
      "        alpha_A = selectAlphaInput(s, ss, {0}colors_0, {0}colors_1, alpha_a) & 255;\n"
      "        alpha_B = selectAlphaInput(s, ss, {0}colors_0, {0}colors_1, alpha_b) & 255;\n"
      "      }};\n"
      "      int alpha_C = selectAlphaInput(s, ss, {0}colors_0, {0}colors_1, alpha_c) & 255;\n"
      "      int alpha_D = selectAlphaInput(s, ss, {0}colors_0, {0}colors_1, alpha_d); // 10 bits "
      "+ sign\n"
      "\n",  // TODO: do we need to sign extend?
      color_input_prefix);
  out.Write("\n"
            "      int alpha;\n"
            "      if (alpha_bias != 3u) {{ // Normal mode\n"
            "        alpha = tevLerp(alpha_A, alpha_B, alpha_C, alpha_D, alpha_bias, alpha_op, "
            "true, alpha_shift);\n"
            "      }} else {{ // Compare mode\n"
            "        if (alpha_compare_op == 6u) {{\n"
            "          // TevCompareMode::A8, TevComparison::GT\n"
            "          alpha = (alpha_A > alpha_B) ? alpha_C : 0;\n"
            "        }} else if (alpha_compare_op == 7u) {{\n"
            "          // TevCompareMode::A8, TevComparison::EQ\n"
            "          alpha = (alpha_A == alpha_B) ? alpha_C : 0;\n"
            "        }} else {{\n"
            "          // All remaining alpha compare ops actually compare the color channels\n"
            "          alpha = tevCompare(alpha_compare_op, color_A, color_B) ? alpha_C : 0;\n"
            "        }}\n"
            "        alpha = alpha_D + alpha;\n"
            "      }}\n"
            "\n"
            "      // Clamp result\n"
            "      if (alpha_clamp)\n"
            "        alpha = clamp(alpha, 0, 255);\n"
            "      else\n"
            "        alpha = clamp(alpha, -1024, 1023);\n"
            "\n"
            "      // Write result to the correct input register of the next stage\n"
            "      setRegAlpha(s, alpha_dest, alpha);\n"
            "    }}\n");

  out.Write("  }} // Main TEV loop\n"
            "\n");

  // Select the output color and alpha registers from the last stage.
  out.Write("  int4 TevResult;\n");
  out.Write("  TevResult.xyz = getTevReg(s, {}).xyz;\n",
            BitfieldExtract("bpmem_combiners(num_stages).x", TevStageCombiner().colorC.dest));
  out.Write("  TevResult.w = getTevReg(s, {}).w;\n",
            BitfieldExtract("bpmem_combiners(num_stages).y", TevStageCombiner().alphaC.dest));

  out.Write("  TevResult &= 255;\n\n");

  if (host_config.fast_depth_calc)
  {
    if (!host_config.backend_reversed_depth_range)
      out.Write("  int zCoord = int((1.0 - rawpos.z) * 16777216.0);\n");
    else
      out.Write("  int zCoord = int(rawpos.z * 16777216.0);\n");
    out.Write("  zCoord = clamp(zCoord, 0, 0xFFFFFF);\n"
              "\n");
  }
  else
  {
    out.Write("\tint zCoord = " I_ZBIAS "[1].x + int((clipPos.z / clipPos.w) * float(" I_ZBIAS
              "[1].y));\n");
  }

  // ===========
  //   ZFreeze
  // ===========

  if (per_pixel_depth)
  {
    // Zfreeze forces early depth off
    out.Write("  // ZFreeze\n"
              "  if ((bpmem_genmode & {}u) != 0u) {{\n",
              1 << GenMode().zfreeze.StartBit());
    out.Write("    float2 screenpos = rawpos.xy * " I_EFBSCALE ".xy;\n");
    if (ApiType == APIType::OpenGL)
    {
      out.Write("    // OpenGL has reversed vertical screenspace coordinates\n"
                "    screenpos.y = 528.0 - screenpos.y;\n");
    }
    out.Write("    zCoord = int(" I_ZSLOPE ".z + " I_ZSLOPE ".x * screenpos.x + " I_ZSLOPE
              ".y * screenpos.y);\n"
              " }}\n"
              "\n");
  }

  // =================
  //   Depth Texture
  // =================

  out.Write("  // Depth Texture\n"
            "  int early_zCoord = zCoord;\n"
            "  if (bpmem_ztex_op != 0u) {{\n"
            "    int ztex = int(" I_ZBIAS "[1].w); // fixed bias\n"
            "\n"
            "    // Whatever texture was in our last stage, it's now our depth texture\n"
            "    ztex += idot(s.TexColor.xyzw, " I_ZBIAS "[0].xyzw);\n"
            "    ztex += (bpmem_ztex_op == 1u) ? zCoord : 0;\n"
            "    zCoord = ztex & 0xFFFFFF;\n"
            "  }}\n"
            "\n");

  if (per_pixel_depth)
  {
    out.Write("  // If early depth is enabled, write to zbuffer before depth textures\n"
              "  // If early depth isn't enabled, we write to the zbuffer here\n"
              "  int zbuffer_zCoord = bpmem_late_ztest ? zCoord : early_zCoord;\n");
    if (!host_config.backend_reversed_depth_range)
      out.Write("  depth = 1.0 - float(zbuffer_zCoord) / 16777216.0;\n");
    else
      out.Write("  depth = float(zbuffer_zCoord) / 16777216.0;\n");
  }

  out.Write("  // Alpha Test\n"
            "  if (bpmem_alphaTest != 0u) {{\n"
            "    bool comp0 = alphaCompare(TevResult.a, " I_ALPHA ".r, {});\n",
            BitfieldExtract("bpmem_alphaTest", AlphaTest().comp0));
  out.Write("    bool comp1 = alphaCompare(TevResult.a, " I_ALPHA ".g, {});\n",
            BitfieldExtract("bpmem_alphaTest", AlphaTest().comp1));
  out.Write("\n"
            "    // These if statements are written weirdly to work around intel and Qualcomm "
            "bugs with handling booleans.\n"
            "    switch ({}) {{\n",
            BitfieldExtract("bpmem_alphaTest", AlphaTest().logic));
  out.Write("    case 0u: // AND\n"
            "      if (comp0 && comp1) break; else discard; break;\n"
            "    case 1u: // OR\n"
            "      if (comp0 || comp1) break; else discard; break;\n"
            "    case 2u: // XOR\n"
            "      if (comp0 != comp1) break; else discard; break;\n"
            "    case 3u: // XNOR\n"
            "      if (comp0 == comp1) break; else discard; break;\n"
            "    }}\n"
            "  }}\n"
            "\n");

  // =========
  // Dithering
  // =========
  out.Write("  if (bpmem_dither) {{\n"
            "    // Flipper uses a standard 2x2 Bayer Matrix for 6 bit dithering\n"
            "    // Here the matrix is encoded into the two factor constants\n"
            "    int2 dither = int2(rawpos.xy) & 1;\n"
            "    TevResult.rgb = (TevResult.rgb - (TevResult.rgb >> 6)) + abs(dither.y * 3 - "
            "dither.x * 2);\n"
            "  }}\n\n");

  // =========
  //    Fog
  // =========

  // FIXME: Fog is implemented the same as ShaderGen, but ShaderGen's fog is all hacks.
  //        Should be fixed point, and should not make guesses about Range-Based adjustments.
  out.Write("  // Fog\n"
            "  uint fog_function = {};\n",
            BitfieldExtract("bpmem_fogParam3", FogParam3().fsel));
  out.Write("  if (fog_function != {:s}) {{\n", FogType::Off);
  out.Write("    // TODO: This all needs to be converted from float to fixed point\n"
            "    float ze;\n"
            "    if ({} == 0u) {{\n",
            BitfieldExtract("bpmem_fogParam3", FogParam3().proj));
  out.Write("      // perspective\n"
            "      // ze = A/(B - (Zs >> B_SHF)\n"
            "      ze = (" I_FOGF ".x * 16777216.0) / float(" I_FOGI ".y - (zCoord >> " I_FOGI
            ".w));\n"
            "    }} else {{\n"
            "      // orthographic\n"
            "      // ze = a*Zs    (here, no B_SHF)\n"
            "      ze = " I_FOGF ".z * float(zCoord) / 16777216.0;\n"
            "    }}\n"
            "\n"
            "    if (bool({})) {{\n",
            BitfieldExtract("bpmem_fogRangeBase", FogRangeParams::RangeBase().Enabled));
  out.Write("      // x_adjust = sqrt((x-center)^2 + k^2)/k\n"
            "      // ze *= x_adjust\n"
            "      float offset = (2.0 * (rawpos.x / " I_FOGF ".w)) - 1.0 - " I_FOGF ".z;\n"
            "      float floatindex = clamp(9.0 - abs(offset) * 9.0, 0.0, 9.0);\n"
            "      uint indexlower = uint(floatindex);\n"
            "      uint indexupper = indexlower + 1u;\n"
            "      float klower = " I_FOGRANGE "[indexlower >> 2u][indexlower & 3u];\n"
            "      float kupper = " I_FOGRANGE "[indexupper >> 2u][indexupper & 3u];\n"
            "      float k = lerp(klower, kupper, frac(floatindex));\n"
            "      float x_adjust = sqrt(offset * offset + k * k) / k;\n"
            "      ze *= x_adjust;\n"
            "    }}\n"
            "\n"
            "    float fog = clamp(ze - " I_FOGF ".y, 0.0, 1.0);\n"
            "\n");
  out.Write("    if (fog_function >= {:s}) {{\n", FogType::Exp);
  out.Write("      switch (fog_function) {{\n"
            "      case {:s}:\n"
            "        fog = 1.0 - exp2(-8.0 * fog);\n"
            "        break;\n",
            FogType::Exp);
  out.Write("      case {:s}:\n"
            "        fog = 1.0 - exp2(-8.0 * fog * fog);\n"
            "        break;\n",
            FogType::ExpSq);
  out.Write("      case {:s}:\n"
            "        fog = exp2(-8.0 * (1.0 - fog));\n"
            "        break;\n",
            FogType::BackwardsExp);
  out.Write("      case {:s}:\n"
            "        fog = 1.0 - fog;\n"
            "        fog = exp2(-8.0 * fog * fog);\n"
            "        break;\n",
            FogType::BackwardsExpSq);
  out.Write("      }}\n"
            "    }}\n"
            "\n"
            "    int ifog = iround(fog * 256.0);\n"
            "    TevResult.rgb = (TevResult.rgb * (256 - ifog) + " I_FOGCOLOR ".rgb * ifog) >> 8;\n"
            "  }}\n"
            "\n");

  // D3D requires that the shader outputs be uint when writing to a uint render target for logic op.
  if (ApiType == APIType::D3D && uid_data->uint_output)
  {
    out.Write("  if (bpmem_rgba6_format)\n"
              "    ocol0 = uint4(TevResult & 0xFC);\n"
              "  else\n"
              "    ocol0 = uint4(TevResult);\n"
              "\n");
  }
  else
  {
    out.Write("  if (bpmem_rgba6_format)\n"
              "    ocol0.rgb = float3(TevResult.rgb >> 2) / 63.0;\n"
              "  else\n"
              "    ocol0.rgb = float3(TevResult.rgb) / 255.0;\n"
              "\n"
              "  if (bpmem_dstalpha != 0u)\n");
    out.Write("    ocol0.a = float({} >> 2) / 63.0;\n",
              BitfieldExtract("bpmem_dstalpha", ConstantAlpha().alpha));
    out.Write("  else\n"
              "    ocol0.a = float(TevResult.a >> 2) / 63.0;\n"
              "  \n");

    if (use_dual_source || use_shader_blend)
    {
      out.Write("  // Dest alpha override (dual source blending)\n"
                "  // Colors will be blended against the alpha from ocol1 and\n"
                "  // the alpha from ocol0 will be written to the framebuffer.\n"
                "  ocol1 = float4(0.0, 0.0, 0.0, float(TevResult.a) / 255.0);\n");
    }
  }

  if (bounding_box)
  {
    out.Write("  if (bpmem_bounding_box) {{\n"
              "    UpdateBoundingBox(rawpos.xy);\n"
              "  }}\n");
  }

  if (use_shader_blend)
  {
    static constexpr std::array<std::string_view, 8> blendSrcFactor{{
        "float3(0,0,0);",                      // ZERO
        "float3(1,1,1);",                      // ONE
        "initial_ocol0.rgb;",                  // DSTCLR
        "float3(1,1,1) - initial_ocol0.rgb;",  // INVDSTCLR
        "ocol1.aaa;",                          // SRCALPHA
        "float3(1,1,1) - ocol1.aaa;",          // INVSRCALPHA
        "initial_ocol0.aaa;",                  // DSTALPHA
        "float3(1,1,1) - initial_ocol0.aaa;",  // INVDSTALPHA
    }};
    static constexpr std::array<std::string_view, 8> blendSrcFactorAlpha{{
        "0.0;",                    // ZERO
        "1.0;",                    // ONE
        "initial_ocol0.a;",        // DSTCLR
        "1.0 - initial_ocol0.a;",  // INVDSTCLR
        "ocol1.a;",                // SRCALPHA
        "1.0 - ocol1.a;",          // INVSRCALPHA
        "initial_ocol0.a;",        // DSTALPHA
        "1.0 - initial_ocol0.a;",  // INVDSTALPHA
    }};
    static constexpr std::array<std::string_view, 8> blendDstFactor{{
        "float3(0,0,0);",                      // ZERO
        "float3(1,1,1);",                      // ONE
        "ocol0.rgb;",                          // SRCCLR
        "float3(1,1,1) - ocol0.rgb;",          // INVSRCCLR
        "ocol1.aaa;",                          // SRCALHA
        "float3(1,1,1) - ocol1.aaa;",          // INVSRCALPHA
        "initial_ocol0.aaa;",                  // DSTALPHA
        "float3(1,1,1) - initial_ocol0.aaa;",  // INVDSTALPHA
    }};
    static constexpr std::array<std::string_view, 8> blendDstFactorAlpha{{
        "0.0;",                    // ZERO
        "1.0;",                    // ONE
        "ocol0.a;",                // SRCCLR
        "1.0 - ocol0.a;",          // INVSRCCLR
        "ocol1.a;",                // SRCALPHA
        "1.0 - ocol1.a;",          // INVSRCALPHA
        "initial_ocol0.a;",        // DSTALPHA
        "1.0 - initial_ocol0.a;",  // INVDSTALPHA
    }};

    out.Write("  if (blend_enable) {{\n"
              "    float4 blend_src;\n"
              "    switch (blend_src_factor) {{\n");
    for (size_t i = 0; i < blendSrcFactor.size(); i++)
    {
      out.Write("      case {}u: blend_src.rgb = {}; break;\n", i, blendSrcFactor[i]);
    }

    out.Write("    }}\n"
              "    switch (blend_src_factor_alpha) {{\n");
    for (size_t i = 0; i < blendSrcFactorAlpha.size(); i++)
    {
      out.Write("      case {}u: blend_src.a = {}; break;\n", i, blendSrcFactorAlpha[i]);
    }

    out.Write("    }}\n"
              "    float4 blend_dst;\n"
              "    switch (blend_dst_factor) {{\n");
    for (size_t i = 0; i < blendDstFactor.size(); i++)
    {
      out.Write("      case {}u: blend_dst.rgb = {}; break;\n", i, blendDstFactor[i]);
    }
    out.Write("    }}\n"
              "    switch (blend_dst_factor_alpha) {{\n");
    for (size_t i = 0; i < blendDstFactorAlpha.size(); i++)
    {
      out.Write("      case {}u: blend_dst.a = {}; break;\n", i, blendDstFactorAlpha[i]);
    }

    out.Write(
        "    }}\n"
        "    float4 blend_result;\n"
        "    if (blend_subtract)\n"
        "      blend_result.rgb = initial_ocol0.rgb * blend_dst.rgb - ocol0.rgb * blend_src.rgb;\n"
        "    else\n"
        "      blend_result.rgb = initial_ocol0.rgb * blend_dst.rgb + ocol0.rgb * "
        "blend_src.rgb;\n");

    out.Write("    if (blend_subtract_alpha)\n"
              "      blend_result.a = initial_ocol0.a * blend_dst.a - ocol0.a * blend_src.a;\n"
              "    else\n"
              "      blend_result.a = initial_ocol0.a * blend_dst.a + ocol0.a * blend_src.a;\n");

    out.Write("    real_ocol0 = blend_result;\n");

    out.Write("  }} else {{\n"
              "    real_ocol0 = ocol0;\n"
              "  }}\n");
  }

  out.Write("}}\n"
            "\n"
            "int4 getRasColor(State s, StageState ss, float4 colors_0, float4 colors_1) {{\n"
            "  // Select Ras for stage\n"
            "  uint ras = {};\n",
            BitfieldExtract("ss.order", TwoTevStageOrders().colorchan0));
  out.Write("  if (ras < 2u) {{ // Lighting Channel 0 or 1\n"
            "    int4 color = iround(((ras == 0u) ? colors_0 : colors_1) * 255.0);\n"
            "    uint swap = {};\n",
            BitfieldExtract("ss.ac", TevStageCombiner().alphaC.rswap));
  out.Write("    return Swizzle(swap, color);\n");
  out.Write("  }} else if (ras == 5u) {{ // Alpha Bumb\n"
            "    return int4(s.AlphaBump, s.AlphaBump, s.AlphaBump, s.AlphaBump);\n"
            "  }} else if (ras == 6u) {{ // Normalzied Alpha Bump\n"
            "    int normalized = s.AlphaBump | s.AlphaBump >> 5;\n"
            "    return int4(normalized, normalized, normalized, normalized);\n"
            "  }} else {{\n"
            "    return int4(0, 0, 0, 0);\n"
            "  }}\n"
            "}}\n"
            "\n"
            "int4 getKonstColor(State s, StageState ss) {{\n"
            "  // Select Konst for stage\n"
            "  // TODO: a switch case might be better here than an dynamically"
            "  // indexed uniform lookup\n"
            "  uint tevksel = bpmem_tevksel(ss.stage>>1);\n"
            "  if ((ss.stage & 1u) == 0u)\n"
            "    return int4(konstLookup[{}].rgb, konstLookup[{}].a);\n",
            BitfieldExtract("tevksel", bpmem.tevksel[0].kcsel0),
            BitfieldExtract("tevksel", bpmem.tevksel[0].kasel0));
  out.Write("  else\n"
            "    return int4(konstLookup[{}].rgb, konstLookup[{}].a);\n",
            BitfieldExtract("tevksel", bpmem.tevksel[0].kcsel1),
            BitfieldExtract("tevksel", bpmem.tevksel[0].kasel1));
  out.Write("}}\n");

  return out;
}

void EnumeratePixelShaderUids(const std::function<void(const PixelShaderUid&)>& callback)
{
  PixelShaderUid uid;

  for (u32 texgens = 0; texgens <= 8; texgens++)
  {
    pixel_ubershader_uid_data* const puid = uid.GetUidData();
    puid->num_texgens = texgens;

    for (u32 early_depth = 0; early_depth < 2; early_depth++)
    {
      puid->early_depth = early_depth != 0;
      for (u32 per_pixel_depth = 0; per_pixel_depth < 2; per_pixel_depth++)
      {
        // Don't generate shaders where we have early depth tests enabled, and write gl_FragDepth.
        if (early_depth && per_pixel_depth)
          continue;

        puid->per_pixel_depth = per_pixel_depth != 0;
        for (u32 uint_output = 0; uint_output < 2; uint_output++)
        {
          puid->uint_output = uint_output;
          callback(uid);
        }
      }
    }
  }
}
}  // namespace UberShader
