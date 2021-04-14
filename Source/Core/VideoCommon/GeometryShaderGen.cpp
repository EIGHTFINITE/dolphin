// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/GeometryShaderGen.h"

#include <cmath>

#include "Common/CommonTypes.h"
#include "VideoCommon/DriverDetails.h"
#include "VideoCommon/LightingShaderGen.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

constexpr std::array<const char*, 4> primitives_ogl{
    "points",
    "lines",
    "triangles",
    "triangles",
};
constexpr std::array<const char*, 4> primitives_d3d{
    "point",
    "line",
    "triangle",
    "triangle",
};

bool geometry_shader_uid_data::IsPassthrough() const
{
  const bool stereo = g_ActiveConfig.stereo_mode != StereoMode::Off;
  const bool wireframe = g_ActiveConfig.bWireFrame;
  return primitive_type >= static_cast<u32>(PrimitiveType::Triangles) && !stereo && !wireframe;
}

GeometryShaderUid GetGeometryShaderUid(PrimitiveType primitive_type)
{
  GeometryShaderUid out;

  geometry_shader_uid_data* const uid_data = out.GetUidData();
  uid_data->primitive_type = static_cast<u32>(primitive_type);
  uid_data->numTexGens = xfmem.numTexGen.numTexGens;

  return out;
}

static void EmitVertex(ShaderCode& out, const ShaderHostConfig& host_config,
                       const geometry_shader_uid_data* uid_data, const char* vertex,
                       APIType ApiType, bool wireframe, bool first_vertex = false);
static void EndPrimitive(ShaderCode& out, const ShaderHostConfig& host_config,
                         const geometry_shader_uid_data* uid_data, APIType ApiType, bool wireframe);

ShaderCode GenerateGeometryShaderCode(APIType ApiType, const ShaderHostConfig& host_config,
                                      const geometry_shader_uid_data* uid_data)
{
  ShaderCode out;
  // Non-uid template parameters will write to the dummy data (=> gets optimized out)

  const bool wireframe = host_config.wireframe;
  const bool msaa = host_config.msaa;
  const bool ssaa = host_config.ssaa;
  const bool stereo = host_config.stereo;
  const auto primitive_type = static_cast<PrimitiveType>(uid_data->primitive_type);
  const auto primitive_type_index = static_cast<unsigned>(uid_data->primitive_type);
  const auto vertex_in = std::min(static_cast<unsigned>(primitive_type_index) + 1, 3u);
  u32 vertex_out = primitive_type == PrimitiveType::TriangleStrip ? 3 : 4;

  if (wireframe)
    vertex_out++;

  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
  {
    // Insert layout parameters
    if (host_config.backend_gs_instancing)
    {
      out.Write("layout({}, invocations = {}) in;\n", primitives_ogl[primitive_type_index],
                stereo ? 2 : 1);
      out.Write("layout({}_strip, max_vertices = {}) out;\n", wireframe ? "line" : "triangle",
                vertex_out);
    }
    else
    {
      out.Write("layout({}) in;\n", primitives_ogl[primitive_type_index]);
      out.Write("layout({}_strip, max_vertices = {}) out;\n", wireframe ? "line" : "triangle",
                stereo ? vertex_out * 2 : vertex_out);
    }
  }

  out.Write("{}", s_lighting_struct);

  // uniforms
  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
    out.Write("UBO_BINDING(std140, 3) uniform GSBlock {{\n");
  else
    out.Write("cbuffer GSBlock {{\n");

  out.Write("\tfloat4 " I_STEREOPARAMS ";\n"
            "\tfloat4 " I_LINEPTPARAMS ";\n"
            "\tint4 " I_TEXOFFSET ";\n"
            "}};\n");

  out.Write("struct VS_OUTPUT {{\n");
  GenerateVSOutputMembers(out, ApiType, uid_data->numTexGens, host_config, "");
  out.Write("}};\n");

  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
  {
    if (host_config.backend_gs_instancing)
      out.Write("#define InstanceID gl_InvocationID\n");

    out.Write("VARYING_LOCATION(0) in VertexData {{\n");
    GenerateVSOutputMembers(out, ApiType, uid_data->numTexGens, host_config,
                            GetInterpolationQualifier(msaa, ssaa, true, true));
    out.Write("}} vs[{}];\n", vertex_in);

    out.Write("VARYING_LOCATION(0) out VertexData {{\n");
    GenerateVSOutputMembers(out, ApiType, uid_data->numTexGens, host_config,
                            GetInterpolationQualifier(msaa, ssaa, true, false));

    if (stereo)
      out.Write("\tflat int layer;\n");

    out.Write("}} ps;\n");

    out.Write("void main()\n{{\n");
  }
  else  // D3D
  {
    out.Write("struct VertexData {{\n");
    out.Write("\tVS_OUTPUT o;\n");

    if (stereo)
      out.Write("\tuint layer : SV_RenderTargetArrayIndex;\n");

    out.Write("}};\n");

    if (host_config.backend_gs_instancing)
    {
      out.Write("[maxvertexcount({})]\n[instance({})]\n", vertex_out, stereo ? 2 : 1);
      out.Write("void main({} VS_OUTPUT o[{}], inout {}Stream<VertexData> output, in uint "
                "InstanceID : SV_GSInstanceID)\n{{\n",
                primitives_d3d[primitive_type_index], vertex_in, wireframe ? "Line" : "Triangle");
    }
    else
    {
      out.Write("[maxvertexcount({})]\n", stereo ? vertex_out * 2 : vertex_out);
      out.Write("void main({} VS_OUTPUT o[{}], inout {}Stream<VertexData> output)\n{{\n",
                primitives_d3d[primitive_type_index], vertex_in, wireframe ? "Line" : "Triangle");
    }

    out.Write("\tVertexData ps;\n");
  }

  if (primitive_type == PrimitiveType::Lines)
  {
    if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
    {
      out.Write("\tVS_OUTPUT start, end;\n");
      AssignVSOutputMembers(out, "start", "vs[0]", uid_data->numTexGens, host_config);
      AssignVSOutputMembers(out, "end", "vs[1]", uid_data->numTexGens, host_config);
    }
    else
    {
      out.Write("\tVS_OUTPUT start = o[0];\n"
                "\tVS_OUTPUT end = o[1];\n");
    }

    // GameCube/Wii's line drawing algorithm is a little quirky. It does not
    // use the correct line caps. Instead, the line caps are vertical or
    // horizontal depending the slope of the line.
    out.Write("\tfloat2 offset;\n"
              "\tfloat2 to = abs(end.pos.xy / end.pos.w - start.pos.xy / start.pos.w);\n"
              // FIXME: What does real hardware do when line is at a 45-degree angle?
              // FIXME: Lines aren't drawn at the correct width. See Twilight Princess map.
              "\tif (" I_LINEPTPARAMS ".y * to.y > " I_LINEPTPARAMS ".x * to.x) {{\n"
              // Line is more tall. Extend geometry left and right.
              // Lerp LineWidth/2 from [0..VpWidth] to [-1..1]
              "\t\toffset = float2(" I_LINEPTPARAMS ".z / " I_LINEPTPARAMS ".x, 0);\n"
              "\t}} else {{\n"
              // Line is more wide. Extend geometry up and down.
              // Lerp LineWidth/2 from [0..VpHeight] to [1..-1]
              "\t\toffset = float2(0, -" I_LINEPTPARAMS ".z / " I_LINEPTPARAMS ".y);\n"
              "\t}}\n");
  }
  else if (primitive_type == PrimitiveType::Points)
  {
    if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
    {
      out.Write("\tVS_OUTPUT center;\n");
      AssignVSOutputMembers(out, "center", "vs[0]", uid_data->numTexGens, host_config);
    }
    else
    {
      out.Write("\tVS_OUTPUT center = o[0];\n");
    }

    // Offset from center to upper right vertex
    // Lerp PointSize/2 from [0,0..VpWidth,VpHeight] to [-1,1..1,-1]
    out.Write("\tfloat2 offset = float2(" I_LINEPTPARAMS ".w / " I_LINEPTPARAMS
              ".x, -" I_LINEPTPARAMS ".w / " I_LINEPTPARAMS ".y) * center.pos.w;\n");
  }

  if (stereo)
  {
    // If the GPU supports invocation we don't need a for loop and can simply use the
    // invocation identifier to determine which layer we're rendering.
    if (host_config.backend_gs_instancing)
      out.Write("\tint eye = InstanceID;\n");
    else
      out.Write("\tfor (int eye = 0; eye < 2; ++eye) {{\n");
  }

  if (wireframe)
    out.Write("\tVS_OUTPUT first;\n");

  out.Write("\tfor (int i = 0; i < {}; ++i) {{\n", vertex_in);

  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
  {
    out.Write("\tVS_OUTPUT f;\n");
    AssignVSOutputMembers(out, "f", "vs[i]", uid_data->numTexGens, host_config);

    if (host_config.backend_depth_clamp &&
        DriverDetails::HasBug(DriverDetails::BUG_BROKEN_CLIP_DISTANCE))
    {
      // On certain GPUs we have to consume the clip distance from the vertex shader
      // or else the other vertex shader outputs will get corrupted.
      out.Write("\tf.clipDist0 = gl_in[i].gl_ClipDistance[0];\n"
                "\tf.clipDist1 = gl_in[i].gl_ClipDistance[1];\n");
    }
  }
  else
  {
    out.Write("\tVS_OUTPUT f = o[i];\n");
  }

  if (stereo)
  {
    // Select the output layer
    out.Write("\tps.layer = eye;\n");
    if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
      out.Write("\tgl_Layer = eye;\n");

    // For stereoscopy add a small horizontal offset in Normalized Device Coordinates proportional
    // to the depth of the vertex. We retrieve the depth value from the w-component of the projected
    // vertex which contains the negated z-component of the original vertex.
    // For negative parallax (out-of-screen effects) we subtract a convergence value from
    // the depth value. This results in objects at a distance smaller than the convergence
    // distance to seemingly appear in front of the screen.
    // This formula is based on page 13 of the "Nvidia 3D Vision Automatic, Best Practices Guide"
    out.Write("\tfloat hoffset = (eye == 0) ? " I_STEREOPARAMS ".x : " I_STEREOPARAMS ".y;\n");
    out.Write("\tf.pos.x += hoffset * (f.pos.w - " I_STEREOPARAMS ".z);\n");
  }

  if (primitive_type == PrimitiveType::Lines)
  {
    out.Write("\tVS_OUTPUT l = f;\n"
              "\tVS_OUTPUT r = f;\n");

    out.Write("\tl.pos.xy -= offset * l.pos.w;\n"
              "\tr.pos.xy += offset * r.pos.w;\n");

    out.Write("\tif (" I_TEXOFFSET "[2] != 0) {{\n");
    out.Write("\tfloat texOffset = 1.0 / float(" I_TEXOFFSET "[2]);\n");

    for (u32 i = 0; i < uid_data->numTexGens; ++i)
    {
      out.Write("\tif (((" I_TEXOFFSET "[0] >> {}) & 0x1) != 0)\n", i);
      out.Write("\t\tr.tex{}.x += texOffset;\n", i);
    }
    out.Write("\t}}\n");

    EmitVertex(out, host_config, uid_data, "l", ApiType, wireframe, true);
    EmitVertex(out, host_config, uid_data, "r", ApiType, wireframe);
  }
  else if (primitive_type == PrimitiveType::Points)
  {
    out.Write("\tVS_OUTPUT ll = f;\n"
              "\tVS_OUTPUT lr = f;\n"
              "\tVS_OUTPUT ul = f;\n"
              "\tVS_OUTPUT ur = f;\n");

    out.Write("\tll.pos.xy += float2(-1,-1) * offset;\n"
              "\tlr.pos.xy += float2(1,-1) * offset;\n"
              "\tul.pos.xy += float2(-1,1) * offset;\n"
              "\tur.pos.xy += offset;\n");

    out.Write("\tif (" I_TEXOFFSET "[3] != 0) {{\n");
    out.Write("\tfloat2 texOffset = float2(1.0 / float(" I_TEXOFFSET
              "[3]), 1.0 / float(" I_TEXOFFSET "[3]));\n");

    for (u32 i = 0; i < uid_data->numTexGens; ++i)
    {
      out.Write("\tif (((" I_TEXOFFSET "[1] >> {}) & 0x1) != 0) {{\n", i);
      out.Write("\t\tul.tex{}.xy += float2(0,1) * texOffset;\n", i);
      out.Write("\t\tur.tex{}.xy += texOffset;\n", i);
      out.Write("\t\tlr.tex{}.xy += float2(1,0) * texOffset;\n", i);
      out.Write("\t}}\n");
    }
    out.Write("\t}}\n");

    EmitVertex(out, host_config, uid_data, "ll", ApiType, wireframe, true);
    EmitVertex(out, host_config, uid_data, "lr", ApiType, wireframe);
    EmitVertex(out, host_config, uid_data, "ul", ApiType, wireframe);
    EmitVertex(out, host_config, uid_data, "ur", ApiType, wireframe);
  }
  else
  {
    EmitVertex(out, host_config, uid_data, "f", ApiType, wireframe, true);
  }

  out.Write("\t}}\n");

  EndPrimitive(out, host_config, uid_data, ApiType, wireframe);

  if (stereo && !host_config.backend_gs_instancing)
    out.Write("\t}}\n");

  out.Write("}}\n");

  return out;
}

static void EmitVertex(ShaderCode& out, const ShaderHostConfig& host_config,
                       const geometry_shader_uid_data* uid_data, const char* vertex,
                       APIType ApiType, bool wireframe, bool first_vertex)
{
  if (wireframe && first_vertex)
    out.Write("\tif (i == 0) first = {};\n", vertex);

  if (ApiType == APIType::OpenGL)
  {
    out.Write("\tgl_Position = {}.pos;\n", vertex);
    if (host_config.backend_depth_clamp)
    {
      out.Write("\tgl_ClipDistance[0] = {}.clipDist0;\n", vertex);
      out.Write("\tgl_ClipDistance[1] = {}.clipDist1;\n", vertex);
    }
    AssignVSOutputMembers(out, "ps", vertex, uid_data->numTexGens, host_config);
  }
  else if (ApiType == APIType::Vulkan)
  {
    // Vulkan NDC space has Y pointing down (right-handed NDC space).
    out.Write("\tgl_Position = {}.pos;\n", vertex);
    out.Write("\tgl_Position.y = -gl_Position.y;\n");
    AssignVSOutputMembers(out, "ps", vertex, uid_data->numTexGens, host_config);
  }
  else
  {
    out.Write("\tps.o = {};\n", vertex);
  }

  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
    out.Write("\tEmitVertex();\n");
  else
    out.Write("\toutput.Append(ps);\n");
}

static void EndPrimitive(ShaderCode& out, const ShaderHostConfig& host_config,
                         const geometry_shader_uid_data* uid_data, APIType ApiType, bool wireframe)
{
  if (wireframe)
    EmitVertex(out, host_config, uid_data, "first", ApiType, wireframe);

  if (ApiType == APIType::OpenGL || ApiType == APIType::Vulkan)
    out.Write("\tEndPrimitive();\n");
  else
    out.Write("\toutput.RestartStrip();\n");
}

void EnumerateGeometryShaderUids(const std::function<void(const GeometryShaderUid&)>& callback)
{
  GeometryShaderUid uid;

  const std::array<PrimitiveType, 3> primitive_lut = {
      {g_ActiveConfig.backend_info.bSupportsPrimitiveRestart ? PrimitiveType::TriangleStrip :
                                                               PrimitiveType::Triangles,
       PrimitiveType::Lines, PrimitiveType::Points}};
  for (PrimitiveType primitive : primitive_lut)
  {
    geometry_shader_uid_data* const guid = uid.GetUidData();
    guid->primitive_type = static_cast<u32>(primitive);

    for (u32 texgens = 0; texgens <= 8; texgens++)
    {
      guid->numTexGens = texgens;
      callback(uid);
    }
  }
}
