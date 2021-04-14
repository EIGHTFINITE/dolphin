// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <set>

#include <IOKit/hid/IOHIDLib.h>

#include "InputCommon/ControllerInterface/CoreDevice.h"
#include "InputCommon/ControllerInterface/ForceFeedback/ForceFeedbackDevice.h"

namespace ciface::OSX
{
class Joystick : public ForceFeedback::ForceFeedbackDevice
{
private:
  class Button : public Input
  {
  public:
    Button(IOHIDElementRef element, IOHIDDeviceRef device) : m_element(element), m_device(device) {}
    std::string GetName() const override;
    ControlState GetState() const override;

  private:
    const IOHIDElementRef m_element;
    const IOHIDDeviceRef m_device;
  };

  class Axis : public Input
  {
  public:
    enum direction
    {
      positive = 0,
      negative
    };

    Axis(IOHIDElementRef element, IOHIDDeviceRef device, direction dir);
    std::string GetName() const override;
    ControlState GetState() const override;

  private:
    const IOHIDElementRef m_element;
    const IOHIDDeviceRef m_device;
    std::string m_name;
    const direction m_direction;
    float m_neutral;
    float m_scale;
  };

  class Hat : public Input
  {
  public:
    enum direction
    {
      up = 0,
      right,
      down,
      left
    };

    Hat(IOHIDElementRef element, IOHIDDeviceRef device, direction dir);
    std::string GetName() const override;
    ControlState GetState() const override;

  private:
    const IOHIDElementRef m_element;
    const IOHIDDeviceRef m_device;
    const char* m_name;
    const direction m_direction;
  };

public:
  Joystick(IOHIDDeviceRef device, std::string name);
  ~Joystick();

  std::string GetName() const override;
  std::string GetSource() const override;
  bool IsSameDevice(const IOHIDDeviceRef) const;

private:
  const IOHIDDeviceRef m_device;
  const std::string m_device_name;

  void AddElements(CFArrayRef elements, std::set<IOHIDElementCookie>& cookies);

  ForceFeedback::FFDeviceAdapterReference m_ff_device;
};
}  // namespace ciface::OSX
