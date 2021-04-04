// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>

struct BootParameters;
struct WindowSystemInfo;

namespace BootManager
{
bool BootCore(std::unique_ptr<BootParameters> parameters, const WindowSystemInfo& wsi);
void SetEmulationSpeedReset(bool value);

// Synchronise Dolphin's configuration with the SYSCONF (which may have changed during emulation),
// and restore settings that were overriden by per-game INIs or for some other reason.
void RestoreConfig();
}  // namespace BootManager
