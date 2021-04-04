// Copyright 2012 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/StringUtil.h"

enum class APIType;

/**
 * Common interface for classes that need to go through the shader generation path
 * (GenerateVertexShader, GenerateGeometryShader, GeneratePixelShader)
 * In particular, this includes the shader code generator (ShaderCode).
 * A different class (ShaderUid) can be used to uniquely identify each ShaderCode object.
 * More interesting things can be done with this, e.g. ShaderConstantProfile checks what shader
 * constants are being used. This can be used to optimize buffer management.
 * If the class does not use one or more of these methods (e.g. Uid class does not need code), the
 * method will be defined as a no-op by the base class, and the call
 * should be optimized out. The reason for this implementation is so that shader
 * selection/generation can be done in two passes, with only a cache lookup being
 * required if the shader has already been generated.
 */
class ShaderGeneratorInterface
{
public:
  /*
   * Used when the shader generator would write a piece of ShaderCode.
   * Can be used like printf.
   * @note In the ShaderCode implementation, this does indeed write the parameter string to an
   * internal buffer. However, you're free to do whatever you like with the parameter.
   */
  void Write(const char*, ...)
#ifdef __GNUC__
      __attribute__((format(printf, 2, 3)))
#endif
  {
  }

  /*
   * Tells us that a specific constant range (including last_index) is being used by the shader
   */
  void SetConstantsUsed(unsigned int first_index, unsigned int last_index) {}
};

/*
 * Shader UID class used to uniquely identify the ShaderCode output written in the shader generator.
 * uid_data can be any struct of parameters that uniquely identify each shader code output.
 * Unless performance is not an issue, uid_data should be tightly packed to reduce memory footprint.
 * Shader generators will write to specific uid_data fields; ShaderUid methods will only read raw
 * u32 values from a union.
 * NOTE: Because LinearDiskCache reads and writes the storage associated with a ShaderUid instance,
 * ShaderUid must be trivially copyable.
 */
template <class uid_data>
class ShaderUid : public ShaderGeneratorInterface
{
public:
  static_assert(std::is_trivially_copyable_v<uid_data>,
                "uid_data must be a trivially copyable type");

  bool operator==(const ShaderUid& obj) const
  {
    return memcmp(GetUidData(), obj.GetUidData(), GetUidDataSize()) == 0;
  }

  bool operator!=(const ShaderUid& obj) const { return !operator==(obj); }

  // determines the storage order inside STL containers
  bool operator<(const ShaderUid& obj) const
  {
    return memcmp(GetUidData(), obj.GetUidData(), GetUidDataSize()) < 0;
  }

  // Returns a pointer to an internally stored object of the uid_data type.
  uid_data* GetUidData() { return &data; }

  // Returns a pointer to an internally stored object of the uid_data type.
  const uid_data* GetUidData() const { return &data; }

  // Returns the raw bytes that make up the shader UID.
  const u8* GetUidDataRaw() const { return reinterpret_cast<const u8*>(&data); }

  // Returns the size of the underlying UID data structure in bytes.
  size_t GetUidDataSize() const { return sizeof(data); }

private:
  uid_data data{};
};

class ShaderCode : public ShaderGeneratorInterface
{
public:
  ShaderCode() { m_buffer.reserve(16384); }
  const std::string& GetBuffer() const { return m_buffer; }

  // Writes format strings using fmtlib format strings.
  template <typename... Args>
  void Write(std::string_view format, Args&&... args)
  {
    fmt::format_to(std::back_inserter(m_buffer), format, std::forward<Args>(args)...);
  }

protected:
  std::string m_buffer;
};

/**
 * Generates a shader constant profile which can be used to query which constants are used in a
 * shader
 */
class ShaderConstantProfile : public ShaderGeneratorInterface
{
public:
  ShaderConstantProfile(int num_constants) { constant_usage.resize(num_constants); }
  void SetConstantsUsed(unsigned int first_index, unsigned int last_index)
  {
    for (unsigned int i = first_index; i < last_index + 1; ++i)
      constant_usage[i] = true;
  }

  bool ConstantIsUsed(unsigned int index) const
  {
    // TODO: Not ready for usage yet
    return true;
    // return constant_usage[index];
  }

private:
  std::vector<bool> constant_usage;  // TODO: Is vector<bool> appropriate here?
};

// Host config contains the settings which can influence generated shaders.
union ShaderHostConfig
{
  u32 bits;

  struct
  {
    u32 msaa : 1;
    u32 ssaa : 1;
    u32 stereo : 1;
    u32 wireframe : 1;
    u32 per_pixel_lighting : 1;
    u32 vertex_rounding : 1;
    u32 fast_depth_calc : 1;
    u32 bounding_box : 1;
    u32 backend_dual_source_blend : 1;
    u32 backend_geometry_shaders : 1;
    u32 backend_early_z : 1;
    u32 backend_bbox : 1;
    u32 backend_gs_instancing : 1;
    u32 backend_clip_control : 1;
    u32 backend_ssaa : 1;
    u32 backend_atomics : 1;
    u32 backend_depth_clamp : 1;
    u32 backend_reversed_depth_range : 1;
    u32 backend_bitfield : 1;
    u32 backend_dynamic_sampler_indexing : 1;
    u32 backend_shader_framebuffer_fetch : 1;
    u32 backend_logic_op : 1;
    u32 backend_palette_conversion : 1;
    u32 pad : 9;
  };

  static ShaderHostConfig GetCurrent();
};

// Gets the filename of the specified type of cache object (e.g. vertex shader, pipeline).
std::string GetDiskShaderCacheFileName(APIType api_type, const char* type, bool include_gameid,
                                       bool include_host_config, bool include_api = true);

void GenerateVSOutputMembers(ShaderCode& object, APIType api_type, u32 texgens,
                             const ShaderHostConfig& host_config, std::string_view qualifier);

void AssignVSOutputMembers(ShaderCode& object, std::string_view a, std::string_view b, u32 texgens,
                           const ShaderHostConfig& host_config);

// We use the flag "centroid" to fix some MSAA rendering bugs. With MSAA, the
// pixel shader will be executed for each pixel which has at least one passed sample.
// So there may be rendered pixels where the center of the pixel isn't in the primitive.
// As the pixel shader usually renders at the center of the pixel, this position may be
// outside the primitive. This will lead to sampling outside the texture, sign changes, ...
// As a workaround, we interpolate at the centroid of the coveraged pixel, which
// is always inside the primitive.
// Without MSAA, this flag is defined to have no effect.
const char* GetInterpolationQualifier(bool msaa, bool ssaa, bool in_glsl_interface_block = false,
                                      bool in = false);

// Constant variable names
#define I_COLORS "color"
#define I_KCOLORS "k"
#define I_ALPHA "alphaRef"
#define I_TEXDIMS "texdim"
#define I_ZBIAS "czbias"
#define I_INDTEXSCALE "cindscale"
#define I_INDTEXMTX "cindmtx"
#define I_FOGCOLOR "cfogcolor"
#define I_FOGI "cfogi"
#define I_FOGF "cfogf"
#define I_FOGRANGE "cfogrange"
#define I_ZSLOPE "czslope"
#define I_EFBSCALE "cefbscale"

#define I_POSNORMALMATRIX "cpnmtx"
#define I_PROJECTION "cproj"
#define I_MATERIALS "cmtrl"
#define I_LIGHTS "clights"
#define I_TEXMATRICES "ctexmtx"
#define I_TRANSFORMMATRICES "ctrmtx"
#define I_NORMALMATRICES "cnmtx"
#define I_POSTTRANSFORMMATRICES "cpostmtx"
#define I_PIXELCENTERCORRECTION "cpixelcenter"
#define I_VIEWPORT_SIZE "cviewport"

#define I_STEREOPARAMS "cstereo"
#define I_LINEPTPARAMS "clinept"
#define I_TEXOFFSET "ctexoffset"

static const char s_shader_uniforms[] = "\tuint    components;\n"
                                        "\tuint    xfmem_dualTexInfo;\n"
                                        "\tuint    xfmem_numColorChans;\n"
                                        "\tuint    color_chan_alpha;\n"
                                        "\tfloat4 " I_POSNORMALMATRIX "[6];\n"
                                        "\tfloat4 " I_PROJECTION "[4];\n"
                                        "\tint4 " I_MATERIALS "[4];\n"
                                        "\tLight " I_LIGHTS "[8];\n"
                                        "\tfloat4 " I_TEXMATRICES "[24];\n"
                                        "\tfloat4 " I_TRANSFORMMATRICES "[64];\n"
                                        "\tfloat4 " I_NORMALMATRICES "[32];\n"
                                        "\tfloat4 " I_POSTTRANSFORMMATRICES "[64];\n"
                                        "\tfloat4 " I_PIXELCENTERCORRECTION ";\n"
                                        "\tfloat2 " I_VIEWPORT_SIZE ";\n"
                                        "\tuint4   xfmem_pack1[8];\n"
                                        "\t#define xfmem_texMtxInfo(i) (xfmem_pack1[(i)].x)\n"
                                        "\t#define xfmem_postMtxInfo(i) (xfmem_pack1[(i)].y)\n"
                                        "\t#define xfmem_color(i) (xfmem_pack1[(i)].z)\n"
                                        "\t#define xfmem_alpha(i) (xfmem_pack1[(i)].w)\n";
