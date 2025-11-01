// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/Common.h"
#include "VideoCommon/VideoBackendBase.h"

namespace Vulkan
{
class VideoBackend : public VideoBackendBase
{
public:
  bool Initialize(const WindowSystemInfo& wsi) override;
  void Shutdown() override;

  std::string GetConfigName() const override { return CONFIG_NAME; }
  std::string GetDisplayName() const override { return _trans("Vulkan"); }
  void InitBackendInfo(const WindowSystemInfo& wsi) override;
  void PrepareWindow(WindowSystemInfo& wsi) override;

  static constexpr const char* CONFIG_NAME = "Vulkan";
};
}  // namespace Vulkan
