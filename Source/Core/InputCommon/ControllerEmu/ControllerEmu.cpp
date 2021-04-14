// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "InputCommon/ControllerEmu/ControllerEmu.h"

#include <memory>
#include <mutex>
#include <string>

#include "Common/IniFile.h"

#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ControllerEmu
{
static std::recursive_mutex s_get_state_mutex;

std::string EmulatedController::GetDisplayName() const
{
  return GetName();
}

EmulatedController::~EmulatedController() = default;

// This should be called before calling GetState() or State() on a control reference
// to prevent a race condition.
// This is a recursive mutex because UpdateReferences is recursive.
std::unique_lock<std::recursive_mutex> EmulatedController::GetStateLock()
{
  std::unique_lock<std::recursive_mutex> lock(s_get_state_mutex);
  return lock;
}

void EmulatedController::UpdateReferences(const ControllerInterface& devi)
{
  m_default_device_is_connected = devi.HasConnectedDevice(m_default_device);

  ciface::ExpressionParser::ControlEnvironment env(devi, GetDefaultDevice(), m_expression_vars);

  UpdateReferences(env);
}

void EmulatedController::UpdateReferences(ciface::ExpressionParser::ControlEnvironment& env)
{
  const auto lock = GetStateLock();

  for (auto& ctrlGroup : groups)
  {
    for (auto& control : ctrlGroup->controls)
      control->control_ref->UpdateReference(env);

    for (auto& setting : ctrlGroup->numeric_settings)
      setting->GetInputReference().UpdateReference(env);

    // Attachments:
    if (ctrlGroup->type == GroupType::Attachments)
    {
      auto* const attachments = static_cast<Attachments*>(ctrlGroup.get());

      attachments->GetSelectionSetting().GetInputReference().UpdateReference(env);

      for (auto& attachment : attachments->GetAttachmentList())
        attachment->UpdateReferences(env);
    }
  }
}

void EmulatedController::UpdateSingleControlReference(const ControllerInterface& devi,
                                                      ControlReference* ref)
{
  ciface::ExpressionParser::ControlEnvironment env(devi, GetDefaultDevice(), m_expression_vars);
  ref->UpdateReference(env);
}

bool EmulatedController::IsDefaultDeviceConnected() const
{
  return m_default_device_is_connected;
}

const ciface::Core::DeviceQualifier& EmulatedController::GetDefaultDevice() const
{
  return m_default_device;
}

void EmulatedController::SetDefaultDevice(const std::string& device)
{
  ciface::Core::DeviceQualifier devq;
  devq.FromString(device);
  SetDefaultDevice(std::move(devq));
}

void EmulatedController::SetDefaultDevice(ciface::Core::DeviceQualifier devq)
{
  m_default_device = std::move(devq);

  for (auto& ctrlGroup : groups)
  {
    // Attachments:
    if (ctrlGroup->type == GroupType::Attachments)
    {
      for (auto& ai : static_cast<Attachments*>(ctrlGroup.get())->GetAttachmentList())
      {
        ai->SetDefaultDevice(m_default_device);
      }
    }
  }
}

void EmulatedController::LoadConfig(IniFile::Section* sec, const std::string& base)
{
  std::string defdev = GetDefaultDevice().ToString();
  if (base.empty())
  {
    sec->Get(base + "Device", &defdev, "");
    SetDefaultDevice(defdev);
  }

  for (auto& cg : groups)
    cg->LoadConfig(sec, defdev, base);
}

void EmulatedController::SaveConfig(IniFile::Section* sec, const std::string& base)
{
  const std::string defdev = GetDefaultDevice().ToString();
  if (base.empty())
    sec->Set(/*std::string(" ") +*/ base + "Device", defdev, "");

  for (auto& ctrlGroup : groups)
    ctrlGroup->SaveConfig(sec, defdev, base);
}

void EmulatedController::LoadDefaults(const ControllerInterface& ciface)
{
  // load an empty inifile section, clears everything
  IniFile::Section sec;
  LoadConfig(&sec);

  const std::string& default_device_string = ciface.GetDefaultDeviceString();
  if (!default_device_string.empty())
  {
    SetDefaultDevice(default_device_string);
  }
}
}  // namespace ControllerEmu
