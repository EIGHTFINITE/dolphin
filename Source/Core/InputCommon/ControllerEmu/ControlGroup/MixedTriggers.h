// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"

namespace ControllerEmu
{
class MixedTriggers : public ControlGroup
{
public:
  explicit MixedTriggers(const std::string& name);

  void GetState(u16* digital, const u16* bitmasks, ControlState* analog,
                bool adjusted = true) const;

  ControlState GetDeadzone() const;
  ControlState GetThreshold() const;

  size_t GetTriggerCount() const;

private:
  SettingValue<double> m_threshold_setting;
  SettingValue<double> m_deadzone_setting;
};
}  // namespace ControllerEmu
