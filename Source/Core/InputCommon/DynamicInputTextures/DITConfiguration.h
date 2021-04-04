// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/IniFile.h"
#include "InputCommon/DynamicInputTextures/DITData.h"

namespace InputCommon::DynamicInputTextures
{
class Configuration
{
public:
  explicit Configuration(const std::string& json_file);
  ~Configuration();
  bool GenerateTextures(const IniFile& file,
                        const std::vector<std::string>& controller_names) const;

private:
  bool GenerateTexture(const IniFile& file, const std::vector<std::string>& controller_names,
                       const Data& texture_data) const;

  std::vector<Data> m_dynamic_input_textures;
  std::string m_base_path;
  bool m_valid = true;
};
}  // namespace InputCommon::DynamicInputTextures
