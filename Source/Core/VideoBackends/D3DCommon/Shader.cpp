// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <fstream>
#include <wrl/client.h>

#include "Common/Assert.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"

#include "VideoBackends/D3DCommon/Shader.h"

#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

namespace D3DCommon
{
Shader::Shader(ShaderStage stage, BinaryData bytecode)
    : AbstractShader(stage), m_bytecode(std::move(bytecode))
{
}

Shader::~Shader() = default;

AbstractShader::BinaryData Shader::GetBinary() const
{
  return m_bytecode;
}

static const char* GetCompileTarget(D3D_FEATURE_LEVEL feature_level, ShaderStage stage)
{
  switch (stage)
  {
  case ShaderStage::Vertex:
  {
    switch (feature_level)
    {
    case D3D_FEATURE_LEVEL_10_0:
      return "vs_4_0";
    case D3D_FEATURE_LEVEL_10_1:
      return "vs_4_1";
    default:
      return "vs_5_0";
    }
  }

  case ShaderStage::Geometry:
  {
    switch (feature_level)
    {
    case D3D_FEATURE_LEVEL_10_0:
      return "gs_4_0";
    case D3D_FEATURE_LEVEL_10_1:
      return "gs_4_1";
    default:
      return "gs_5_0";
    }
  }

  case ShaderStage::Pixel:
  {
    switch (feature_level)
    {
    case D3D_FEATURE_LEVEL_10_0:
      return "ps_4_0";
    case D3D_FEATURE_LEVEL_10_1:
      return "ps_4_1";
    default:
      return "ps_5_0";
    }
  }

  case ShaderStage::Compute:
  {
    switch (feature_level)
    {
    case D3D_FEATURE_LEVEL_10_0:
    case D3D_FEATURE_LEVEL_10_1:
      return "";

    default:
      return "cs_5_0";
    }
  }

  default:
    return "";
  }
}

std::optional<Shader::BinaryData> Shader::CompileShader(D3D_FEATURE_LEVEL feature_level,
                                                        ShaderStage stage, std::string_view source)
{
  static constexpr D3D_SHADER_MACRO macros[] = {{"API_D3D", "1"}, {nullptr, nullptr}};
  const UINT flags = g_ActiveConfig.bEnableValidationLayer ?
                         (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION) :
                         (D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_SKIP_VALIDATION);
  const char* target = GetCompileTarget(feature_level, stage);

  Microsoft::WRL::ComPtr<ID3DBlob> code;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  HRESULT hr = d3d_compile(source.data(), source.size(), nullptr, macros, nullptr, "main", target,
                           flags, 0, &code, &errors);
  if (FAILED(hr))
  {
    static int num_failures = 0;
    std::string filename = VideoBackendBase::BadShaderFilename(target, num_failures++);
    std::ofstream file;
    File::OpenFStream(file, filename, std::ios_base::out);
    file.write(source.data(), source.size());
    file << "\n";
    file.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
    file << "\n";
    file << "Dolphin Version: " + Common::scm_rev_str + "\n";
    file << "Video Backend: " + g_video_backend->GetDisplayName();
    file.close();

    PanicAlertFmt("Failed to compile {}:\nDebug info ({}):\n{}", filename, target,
                  static_cast<const char*>(errors->GetBufferPointer()));
    return std::nullopt;
  }

  if (errors && errors->GetBufferSize() > 0)
  {
    WARN_LOG_FMT(VIDEO, "{} compilation succeeded with warnings:\n{}", target,
                 static_cast<const char*>(errors->GetBufferPointer()));
  }

  return CreateByteCode(code->GetBufferPointer(), code->GetBufferSize());
}

AbstractShader::BinaryData Shader::CreateByteCode(const void* data, size_t length)
{
  const auto* const begin = static_cast<const u8*>(data);
  const auto* const end = begin + length;

  return {begin, end};
}

}  // namespace D3DCommon
