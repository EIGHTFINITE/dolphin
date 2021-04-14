// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/UberShaderVertex.h"

#include "VideoCommon/DriverDetails.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/UberShaderCommon.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/XFMemory.h"

namespace UberShader
{
VertexShaderUid GetVertexShaderUid()
{
  VertexShaderUid out;

  vertex_ubershader_uid_data* const uid_data = out.GetUidData();
  uid_data->num_texgens = xfmem.numTexGen.numTexGens;

  return out;
}

static void GenVertexShaderTexGens(APIType api_type, u32 num_texgen, ShaderCode& out);

ShaderCode GenVertexShader(APIType api_type, const ShaderHostConfig& host_config,
                           const vertex_ubershader_uid_data* uid_data)
{
  const bool msaa = host_config.msaa;
  const bool ssaa = host_config.ssaa;
  const bool per_pixel_lighting = host_config.per_pixel_lighting;
  const bool vertex_rounding = host_config.vertex_rounding;
  const u32 num_texgen = uid_data->num_texgens;
  ShaderCode out;

  out.Write("// Vertex UberShader\n\n");
  out.Write("{}", s_lighting_struct);

  // uniforms
  if (api_type == APIType::OpenGL || api_type == APIType::Vulkan)
    out.Write("UBO_BINDING(std140, 2) uniform VSBlock {{\n");
  else
    out.Write("cbuffer VSBlock {{\n");
  out.Write("{}", s_shader_uniforms);
  out.Write("}};\n");

  out.Write("struct VS_OUTPUT {{\n");
  GenerateVSOutputMembers(out, api_type, num_texgen, host_config, "");
  out.Write("}};\n\n");

  WriteUberShaderCommonHeader(out, api_type, host_config);
  WriteLightingFunction(out);

  if (api_type == APIType::OpenGL || api_type == APIType::Vulkan)
  {
    out.Write("ATTRIBUTE_LOCATION({}) in float4 rawpos;\n", SHADER_POSITION_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION({}) in uint4 posmtx;\n", SHADER_POSMTX_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION({}) in float3 rawnorm0;\n", SHADER_NORM0_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION({}) in float3 rawnorm1;\n", SHADER_NORM1_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION({}) in float3 rawnorm2;\n", SHADER_NORM2_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION({}) in float4 rawcolor0;\n", SHADER_COLOR0_ATTRIB);
    out.Write("ATTRIBUTE_LOCATION({}) in float4 rawcolor1;\n", SHADER_COLOR1_ATTRIB);
    for (int i = 0; i < 8; ++i)
      out.Write("ATTRIBUTE_LOCATION({}) in float3 rawtex{};\n", SHADER_TEXTURE0_ATTRIB + i, i);

    if (host_config.backend_geometry_shaders)
    {
      out.Write("VARYING_LOCATION(0) out VertexData {{\n");
      GenerateVSOutputMembers(out, api_type, num_texgen, host_config,
                              GetInterpolationQualifier(msaa, ssaa, true, false));
      out.Write("}} vs;\n");
    }
    else
    {
      // Let's set up attributes
      u32 counter = 0;
      out.Write("VARYING_LOCATION({}) {} out float4 colors_0;\n", counter++,
                GetInterpolationQualifier(msaa, ssaa));
      out.Write("VARYING_LOCATION({}) {} out float4 colors_1;\n", counter++,
                GetInterpolationQualifier(msaa, ssaa));
      for (u32 i = 0; i < num_texgen; ++i)
      {
        out.Write("VARYING_LOCATION({}) {} out float3 tex{};\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa), i);
      }
      if (!host_config.fast_depth_calc)
      {
        out.Write("VARYING_LOCATION({}) {} out float4 clipPos;\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa));
      }
      if (per_pixel_lighting)
      {
        out.Write("VARYING_LOCATION({}) {} out float3 Normal;\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa));
        out.Write("VARYING_LOCATION({}) {} out float3 WorldPos;\n", counter++,
                  GetInterpolationQualifier(msaa, ssaa));
      }
    }

    out.Write("void main()\n{{\n");
  }
  else  // D3D
  {
    out.Write("VS_OUTPUT main(\n");

    // inputs
    out.Write("  float3 rawnorm0 : NORMAL0,\n"
              "  float3 rawnorm1 : NORMAL1,\n"
              "  float3 rawnorm2 : NORMAL2,\n"
              "  float4 rawcolor0 : COLOR0,\n"
              "  float4 rawcolor1 : COLOR1,\n");
    for (int i = 0; i < 8; ++i)
      out.Write("  float3 rawtex{} : TEXCOORD{},\n", i, i);
    out.Write("  uint posmtx : BLENDINDICES,\n");
    out.Write("  float4 rawpos : POSITION) {{\n");
  }

  out.Write("VS_OUTPUT o;\n"
            "\n");

  // Transforms
  out.Write("// Position matrix\n"
            "float4 P0;\n"
            "float4 P1;\n"
            "float4 P2;\n"
            "\n"
            "// Normal matrix\n"
            "float3 N0;\n"
            "float3 N1;\n"
            "float3 N2;\n"
            "\n"
            "if ((components & {}u) != 0u) {{// VB_HAS_POSMTXIDX\n",
            VB_HAS_POSMTXIDX);
  out.Write("  // Vertex format has a per-vertex matrix\n"
            "  int posidx = int(posmtx.r);\n"
            "  P0 = " I_TRANSFORMMATRICES "[posidx];\n"
            "  P1 = " I_TRANSFORMMATRICES "[posidx+1];\n"
            "  P2 = " I_TRANSFORMMATRICES "[posidx+2];\n"
            "\n"
            "  int normidx = posidx >= 32 ? (posidx - 32) : posidx;\n"
            "  N0 = " I_NORMALMATRICES "[normidx].xyz;\n"
            "  N1 = " I_NORMALMATRICES "[normidx+1].xyz;\n"
            "  N2 = " I_NORMALMATRICES "[normidx+2].xyz;\n"
            "}} else {{\n"
            "  // One shared matrix\n"
            "  P0 = " I_POSNORMALMATRIX "[0];\n"
            "  P1 = " I_POSNORMALMATRIX "[1];\n"
            "  P2 = " I_POSNORMALMATRIX "[2];\n"
            "  N0 = " I_POSNORMALMATRIX "[3].xyz;\n"
            "  N1 = " I_POSNORMALMATRIX "[4].xyz;\n"
            "  N2 = " I_POSNORMALMATRIX "[5].xyz;\n"
            "}}\n"
            "\n"
            "float4 pos = float4(dot(P0, rawpos), dot(P1, rawpos), dot(P2, rawpos), 1.0);\n"
            "o.pos = float4(dot(" I_PROJECTION "[0], pos), dot(" I_PROJECTION
            "[1], pos), dot(" I_PROJECTION "[2], pos), dot(" I_PROJECTION "[3], pos));\n"
            "\n"
            "// Only the first normal gets normalized (TODO: why?)\n"
            "float3 _norm0 = float3(0.0, 0.0, 0.0);\n"
            "if ((components & {}u) != 0u) // VB_HAS_NRM0\n",
            VB_HAS_NRM0);
  out.Write(
      "  _norm0 = normalize(float3(dot(N0, rawnorm0), dot(N1, rawnorm0), dot(N2, rawnorm0)));\n"
      "\n"
      "float3 _norm1 = float3(0.0, 0.0, 0.0);\n"
      "if ((components & {}u) != 0u) // VB_HAS_NRM1\n",
      VB_HAS_NRM1);
  out.Write("  _norm1 = float3(dot(N0, rawnorm1), dot(N1, rawnorm1), dot(N2, rawnorm1));\n"
            "\n"
            "float3 _norm2 = float3(0.0, 0.0, 0.0);\n"
            "if ((components & {}u) != 0u) // VB_HAS_NRM2\n",
            VB_HAS_NRM2);
  out.Write("  _norm2 = float3(dot(N0, rawnorm2), dot(N1, rawnorm2), dot(N2, rawnorm2));\n"
            "\n");

  // Hardware Lighting
  out.Write("// xfmem.numColorChans controls the number of color channels available to TEV,\n"
            "// but we still need to generate all channels here, as it can be used in texgen.\n"
            "// Cel-damage is an example of this.\n"
            "float4 vertex_color_0, vertex_color_1;\n"
            "\n");
  out.Write("// To use color 1, the vertex descriptor must have color 0 and 1.\n"
            "// If color 1 is present but not color 0, it is used for lighting channel 0.\n"
            "bool use_color_1 = ((components & {0}u) == {0}u); // VB_HAS_COL0 | VB_HAS_COL1\n",
            VB_HAS_COL0 | VB_HAS_COL1);

  out.Write("for (uint color = 0u; color < {}u; color++) {{\n", NUM_XF_COLOR_CHANNELS);
  out.Write("  if ((color == 0u || use_color_1) && (components & ({}u << color)) != 0u) {{\n",
            VB_HAS_COL0);
  out.Write("    float4 color_value;\n"
            "    // Use color0 for channel 0, and color1 for channel 1 if both colors 0 and 1 are "
            "present.\n"
            "    if (color == 0u)\n"
            "      vertex_color_0 = rawcolor0;\n"
            "    else\n"
            "      vertex_color_1 = rawcolor1;\n"
            "  }} else if (color == 0u && (components & {}u) != 0u) {{\n",
            VB_HAS_COL1);
  out.Write("    // Use color1 for channel 0 if color0 is not present.\n"
            "    vertex_color_0 = rawcolor1;\n"
            "  }} else {{\n"
            "    // The default alpha channel depends on the number of components in the vertex.\n"
            "    float alpha = float((color_chan_alpha >> color) & 1u);\n"
            "    if (color == 0u)\n"
            "      vertex_color_0 = float4(1.0, 1.0, 1.0, alpha);\n"
            "    else\n"
            "      vertex_color_1 = float4(1.0, 1.0, 1.0, alpha);\n"
            "  }}\n"
            "}}\n"
            "\n");

  WriteVertexLighting(out, api_type, "pos.xyz", "_norm0", "vertex_color_0", "vertex_color_1",
                      "o.colors_0", "o.colors_1");

  // Texture Coordinates
  if (num_texgen > 0)
    GenVertexShaderTexGens(api_type, num_texgen, out);

  out.Write("if (xfmem_numColorChans == 0u) {{\n"
            "  if ((components & {}u) != 0u)\n"
            "    o.colors_0 = rawcolor0;\n"
            "  else\n"
            "    o.colors_1 = float4(1.0, 1.0, 1.0, 1.0);\n"
            "}}\n",
            VB_HAS_COL0);
  out.Write("if (xfmem_numColorChans < 2u) {{\n"
            "  if ((components & {}u) != 0u)\n"
            "    o.colors_0 = rawcolor1;\n"
            "  else\n"
            "    o.colors_1 = float4(1.0, 1.0, 1.0, 1.0);\n"
            "}}\n",
            VB_HAS_COL1);

  if (!host_config.fast_depth_calc)
  {
    // clipPos/w needs to be done in pixel shader, not here
    out.Write("o.clipPos = o.pos;\n");
  }

  if (per_pixel_lighting)
  {
    out.Write("o.Normal = _norm0;\n"
              "o.WorldPos = pos.xyz;\n"
              "// Pass through the vertex colors unmodified so we can evaluate the lighting\n"
              "// in the same manner.\n");
    out.Write("if ((components & {}u) != 0u) // VB_HAS_COL0\n"
              "  o.colors_0 = vertex_color_0;\n",
              VB_HAS_COL0);
    out.Write("if ((components & {}u) != 0u) // VB_HAS_COL1\n"
              "  o.colors_1 = vertex_color_1;\n",
              VB_HAS_COL1);
  }
  else
  {
    out.Write("// The number of colors available to TEV is determined by numColorChans.\n"
              "// We have to provide the fields to match the interface, so set to zero\n"
              "// if it's not enabled.\n"
              "if (xfmem_numColorChans == 0u)\n"
              "  o.colors_0 = float4(0.0, 0.0, 0.0, 0.0);\n"
              "if (xfmem_numColorChans <= 1u)\n"
              "  o.colors_1 = float4(0.0, 0.0, 0.0, 0.0);\n"
              "\n");
  }

  // If we can disable the incorrect depth clipping planes using depth clamping, then we can do
  // our own depth clipping and calculate the depth range before the perspective divide if
  // necessary.
  if (host_config.backend_depth_clamp)
  {
    // Since we're adjusting z for the depth range before the perspective divide, we have to do our
    // own clipping. We want to clip so that -w <= z <= 0, which matches the console -1..0 range.
    // We adjust our depth value for clipping purposes to match the perspective projection in the
    // software backend, which is a hack to fix Sonic Adventure and Unleashed games.
    out.Write("float clipDepth = o.pos.z * (1.0 - 1e-7);\n"
              "float clipDist0 = clipDepth + o.pos.w;\n"  // Near: z < -w
              "float clipDist1 = -clipDepth;\n");         // Far: z > 0
    if (host_config.backend_geometry_shaders)
    {
      out.Write("o.clipDist0 = clipDist0;\n"
                "o.clipDist1 = clipDist1;\n");
    }
  }

  // Write the true depth value. If the game uses depth textures, then the pixel shader will
  // override it with the correct values if not then early z culling will improve speed.
  // There are two different ways to do this, when the depth range is oversized, we process
  // the depth range in the vertex shader, if not we let the host driver handle it.
  //
  // Adjust z for the depth range. We're using an equation which incorperates a depth inversion,
  // so we can map the console -1..0 range to the 0..1 range used in the depth buffer.
  // We have to handle the depth range in the vertex shader instead of after the perspective
  // divide, because some games will use a depth range larger than what is allowed by the
  // graphics API. These large depth ranges will still be clipped to the 0..1 range, so these
  // games effectively add a depth bias to the values written to the depth buffer.
  out.Write("o.pos.z = o.pos.w * " I_PIXELCENTERCORRECTION ".w - "
            "o.pos.z * " I_PIXELCENTERCORRECTION ".z;\n");

  if (!host_config.backend_clip_control)
  {
    // If the graphics API doesn't support a depth range of 0..1, then we need to map z to
    // the -1..1 range. Unfortunately we have to use a substraction, which is a lossy floating-point
    // operation that can introduce a round-trip error.
    out.Write("o.pos.z = o.pos.z * 2.0 - o.pos.w;\n");
  }

  // Correct for negative viewports by mirroring all vertices. We need to negate the height here,
  // since the viewport height is already negated by the render backend.
  out.Write("o.pos.xy *= sign(" I_PIXELCENTERCORRECTION ".xy * float2(1.0, -1.0));\n");

  // The console GPU places the pixel center at 7/12 in screen space unless
  // antialiasing is enabled, while D3D and OpenGL place it at 0.5. This results
  // in some primitives being placed one pixel too far to the bottom-right,
  // which in turn can be critical if it happens for clear quads.
  // Hence, we compensate for this pixel center difference so that primitives
  // get rasterized correctly.
  out.Write("o.pos.xy = o.pos.xy - o.pos.w * " I_PIXELCENTERCORRECTION ".xy;\n");

  if (vertex_rounding)
  {
    // By now our position is in clip space. However, higher resolutions than the Wii outputs
    // cause an additional pixel offset. Due to a higher pixel density we need to correct this
    // by converting our clip-space position into the Wii's screen-space.
    // Acquire the right pixel and then convert it back.
    out.Write("if (o.pos.w == 1.0f)\n"
              "{{\n");

    out.Write("\tfloat ss_pixel_x = ((o.pos.x + 1.0f) * (" I_VIEWPORT_SIZE ".x * 0.5f));\n"
              "\tfloat ss_pixel_y = ((o.pos.y + 1.0f) * (" I_VIEWPORT_SIZE ".y * 0.5f));\n");

    out.Write("\tss_pixel_x = round(ss_pixel_x);\n"
              "\tss_pixel_y = round(ss_pixel_y);\n");

    out.Write("\to.pos.x = ((ss_pixel_x / (" I_VIEWPORT_SIZE ".x * 0.5f)) - 1.0f);\n"
              "\to.pos.y = ((ss_pixel_y / (" I_VIEWPORT_SIZE ".y * 0.5f)) - 1.0f);\n"
              "}}\n");
  }

  if (api_type == APIType::OpenGL || api_type == APIType::Vulkan)
  {
    if (host_config.backend_geometry_shaders)
    {
      AssignVSOutputMembers(out, "vs", "o", num_texgen, host_config);
    }
    else
    {
      // TODO: Pass interface blocks between shader stages even if geometry shaders
      // are not supported, however that will require at least OpenGL 3.2 support.
      for (u32 i = 0; i < num_texgen; ++i)
        out.Write("tex{}.xyz = o.tex{};\n", i, i);
      if (!host_config.fast_depth_calc)
        out.Write("clipPos = o.clipPos;\n");
      if (per_pixel_lighting)
      {
        out.Write("Normal = o.Normal;\n"
                  "WorldPos = o.WorldPos;\n");
      }
      out.Write("colors_0 = o.colors_0;\n"
                "colors_1 = o.colors_1;\n");
    }

    if (host_config.backend_depth_clamp)
    {
      out.Write("gl_ClipDistance[0] = clipDist0;\n"
                "gl_ClipDistance[1] = clipDist1;\n");
    }

    // Vulkan NDC space has Y pointing down (right-handed NDC space).
    if (api_type == APIType::Vulkan)
      out.Write("gl_Position = float4(o.pos.x, -o.pos.y, o.pos.z, o.pos.w);\n");
    else
      out.Write("gl_Position = o.pos;\n");
  }
  else  // D3D
  {
    out.Write("return o;\n");
  }
  out.Write("}}\n");

  return out;
}

static void GenVertexShaderTexGens(APIType api_type, u32 num_texgen, ShaderCode& out)
{
  // The HLSL compiler complains that the output texture coordinates are uninitialized when trying
  // to dynamically index them.
  for (u32 i = 0; i < num_texgen; i++)
    out.Write("o.tex{} = float3(0.0, 0.0, 0.0);\n", i);

  out.Write("// Texture coordinate generation\n");
  if (num_texgen == 1)
  {
    out.Write("{{ const uint texgen = 0u;\n");
  }
  else
  {
    out.Write("{}for (uint texgen = 0u; texgen < {}u; texgen++) {{\n",
              api_type == APIType::D3D ? "[loop] " : "", num_texgen);
  }

  out.Write("  // Texcoord transforms\n");
  out.Write("  float4 coord = float4(0.0, 0.0, 1.0, 1.0);\n"
            "  uint texMtxInfo = xfmem_texMtxInfo(texgen);\n");
  out.Write("  switch ({}) {{\n", BitfieldExtract("texMtxInfo", TexMtxInfo().sourcerow));
  out.Write("  case {:s}:\n", SourceRow::Geom);
  out.Write("    coord.xyz = rawpos.xyz;\n");
  out.Write("    break;\n\n");
  out.Write("  case {:s}:\n", SourceRow::Normal);
  out.Write(
      "    coord.xyz = ((components & {}u /* VB_HAS_NRM0 */) != 0u) ? rawnorm0.xyz : coord.xyz;",
      VB_HAS_NRM0);
  out.Write("    break;\n\n");
  out.Write("  case {:s}:\n", SourceRow::BinormalT);
  out.Write(
      "    coord.xyz = ((components & {}u /* VB_HAS_NRM1 */) != 0u) ? rawnorm1.xyz : coord.xyz;",
      VB_HAS_NRM1);
  out.Write("    break;\n\n");
  out.Write("  case {:s}:\n", SourceRow::BinormalB);
  out.Write(
      "    coord.xyz = ((components & {}u /* VB_HAS_NRM2 */) != 0u) ? rawnorm2.xyz : coord.xyz;",
      VB_HAS_NRM2);
  out.Write("    break;\n\n");
  for (u32 i = 0; i < 8; i++)
  {
    out.Write("  case {:s}:\n", static_cast<SourceRow>(static_cast<u32>(SourceRow::Tex0) + i));
    out.Write(
        "    coord = ((components & {}u /* VB_HAS_UV{} */) != 0u) ? float4(rawtex{}.x, rawtex{}.y, "
        "1.0, 1.0) : coord;\n",
        VB_HAS_UV0 << i, i, i, i);
    out.Write("    break;\n\n");
  }
  out.Write("  }}\n"
            "\n");

  out.Write("  // Input form of AB11 sets z element to 1.0\n");
  out.Write("  if ({} == {:s}) // inputform == AB11\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().inputform), TexInputForm::AB11);
  out.Write("    coord.z = 1.0f;\n"
            "\n");

  out.Write("  // first transformation\n");
  out.Write("  uint texgentype = {};\n", BitfieldExtract("texMtxInfo", TexMtxInfo().texgentype));
  out.Write("  float3 output_tex;\n"
            "  switch (texgentype)\n"
            "  {{\n");
  out.Write("  case {:s}:\n", TexGenType::EmbossMap);
  out.Write("    {{\n");
  out.Write("      uint light = {};\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().embosslightshift));
  out.Write("      uint source = {};\n",
            BitfieldExtract("texMtxInfo", TexMtxInfo().embosssourceshift));
  out.Write("      switch (source) {{\n");
  for (u32 i = 0; i < num_texgen; i++)
    out.Write("      case {}u: output_tex.xyz = o.tex{}; break;\n", i, i);
  out.Write("      default: output_tex.xyz = float3(0.0, 0.0, 0.0); break;\n"
            "      }}\n");
  out.Write("      if ((components & {}u) != 0u) {{ // VB_HAS_NRM1 | VB_HAS_NRM2\n",
            VB_HAS_NRM1 | VB_HAS_NRM2);  // Should this be VB_HAS_NRM1 | VB_HAS_NRM2
  out.Write("        float3 ldir = normalize(" I_LIGHTS "[light].pos.xyz - pos.xyz);\n"
            "        output_tex.xyz += float3(dot(ldir, _norm1), dot(ldir, _norm2), 0.0);\n"
            "      }}\n"
            "    }}\n"
            "    break;\n\n");
  out.Write("  case {:s}:\n", TexGenType::Color0);
  out.Write("    output_tex.xyz = float3(o.colors_0.x, o.colors_0.y, 1.0);\n"
            "    break;\n\n");
  out.Write("  case {:s}:\n", TexGenType::Color1);
  out.Write("    output_tex.xyz = float3(o.colors_1.x, o.colors_1.y, 1.0);\n"
            "    break;\n\n");
  out.Write("  case {:s}:\n", TexGenType::Regular);
  out.Write("  default:\n"
            "    {{\n");
  out.Write("      if ((components & ({}u /* VB_HAS_TEXMTXIDX0 */ << texgen)) != 0u) {{\n",
            VB_HAS_TEXMTXIDX0);
  out.Write("        // This is messy, due to dynamic indexing of the input texture coordinates.\n"
            "        // Hopefully the compiler will unroll this whole loop anyway and the switch.\n"
            "        int tmp = 0;\n"
            "        switch (texgen) {{\n");
  for (u32 i = 0; i < num_texgen; i++)
    out.Write("        case {}u: tmp = int(rawtex{}.z); break;\n", i, i);
  out.Write("        }}\n"
            "\n");
  out.Write("        if ({} == {:s}) {{\n", BitfieldExtract("texMtxInfo", TexMtxInfo().projection),
            TexSize::STQ);
  out.Write("          output_tex.xyz = float3(dot(coord, " I_TRANSFORMMATRICES "[tmp]),\n"
            "                                  dot(coord, " I_TRANSFORMMATRICES "[tmp + 1]),\n"
            "                                  dot(coord, " I_TRANSFORMMATRICES "[tmp + 2]));\n"
            "        }} else {{\n"
            "          output_tex.xyz = float3(dot(coord, " I_TRANSFORMMATRICES "[tmp]),\n"
            "                                  dot(coord, " I_TRANSFORMMATRICES "[tmp + 1]),\n"
            "                                  1.0);\n"
            "        }}\n"
            "      }} else {{\n");
  out.Write("        if ({} == {:s}) {{\n", BitfieldExtract("texMtxInfo", TexMtxInfo().projection),
            TexSize::STQ);
  out.Write("          output_tex.xyz = float3(dot(coord, " I_TEXMATRICES "[3u * texgen]),\n"
            "                                  dot(coord, " I_TEXMATRICES "[3u * texgen + 1u]),\n"
            "                                  dot(coord, " I_TEXMATRICES "[3u * texgen + 2u]));\n"
            "        }} else {{\n"
            "          output_tex.xyz = float3(dot(coord, " I_TEXMATRICES "[3u * texgen]),\n"
            "                                  dot(coord, " I_TEXMATRICES "[3u * texgen + 1u]),\n"
            "                                  1.0);\n"
            "        }}\n"
            "      }}\n"
            "    }}\n"
            "    break;\n\n"
            "  }}\n"
            "\n");

  out.Write("  if (xfmem_dualTexInfo != 0u) {{\n");
  out.Write("    uint postMtxInfo = xfmem_postMtxInfo(texgen);");
  out.Write("    uint base_index = {};\n", BitfieldExtract("postMtxInfo", PostMtxInfo().index));
  out.Write("    float4 P0 = " I_POSTTRANSFORMMATRICES "[base_index & 0x3fu];\n"
            "    float4 P1 = " I_POSTTRANSFORMMATRICES "[(base_index + 1u) & 0x3fu];\n"
            "    float4 P2 = " I_POSTTRANSFORMMATRICES "[(base_index + 2u) & 0x3fu];\n"
            "\n");
  out.Write("    if ({} != 0u)\n", BitfieldExtract("postMtxInfo", PostMtxInfo().normalize));
  out.Write("      output_tex.xyz = normalize(output_tex.xyz);\n"
            "\n"
            "    // multiply by postmatrix\n"
            "    output_tex.xyz = float3(dot(P0.xyz, output_tex.xyz) + P0.w,\n"
            "                            dot(P1.xyz, output_tex.xyz) + P1.w,\n"
            "                            dot(P2.xyz, output_tex.xyz) + P2.w);\n"
            "  }}\n\n");

  // When q is 0, the GameCube appears to have a special case
  // This can be seen in devkitPro's neheGX Lesson08 example for Wii
  // Makes differences in Rogue Squadron 3 (Hoth sky) and The Last Story (shadow culling)
  out.Write("  if (texgentype == {:s} && output_tex.z == 0.0)\n", TexGenType::Regular);
  out.Write(
      "    output_tex.xy = clamp(output_tex.xy / 2.0f, float2(-1.0f,-1.0f), float2(1.0f,1.0f));\n"
      "\n");

  out.Write("  // Hopefully GPUs that can support dynamic indexing will optimize this.\n");
  out.Write("  switch (texgen) {{\n");
  for (u32 i = 0; i < num_texgen; i++)
    out.Write("  case {}u: o.tex{} = output_tex; break;\n", i, i);
  out.Write("  }}\n"
            "}}\n");
}

void EnumerateVertexShaderUids(const std::function<void(const VertexShaderUid&)>& callback)
{
  VertexShaderUid uid;

  for (u32 texgens = 0; texgens <= 8; texgens++)
  {
    vertex_ubershader_uid_data* const vuid = uid.GetUidData();
    vuid->num_texgens = texgens;
    callback(uid);
  }
}
}  // namespace UberShader
