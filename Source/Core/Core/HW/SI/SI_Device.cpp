// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/SI/SI_Device.h"

#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/HW/SI/SI_DeviceDanceMat.h"
#include "Core/HW/SI/SI_DeviceGBA.h"
#include "Core/HW/SI/SI_DeviceGCAdapter.h"
#include "Core/HW/SI/SI_DeviceGCController.h"
#include "Core/HW/SI/SI_DeviceGCSteeringWheel.h"
#include "Core/HW/SI/SI_DeviceKeyboard.h"
#include "Core/HW/SI/SI_DeviceNull.h"

namespace SerialInterface
{
std::ostream& operator<<(std::ostream& stream, SIDevices device)
{
  stream << static_cast<std::underlying_type_t<SIDevices>>(device);
  return stream;
}

std::istream& operator>>(std::istream& stream, SIDevices& device)
{
  std::underlying_type_t<SIDevices> value;

  if (stream >> value)
  {
    device = static_cast<SIDevices>(value);
  }
  else
  {
    device = SIDevices::SIDEVICE_NONE;
  }

  return stream;
}

ISIDevice::ISIDevice(SIDevices device_type, int device_number)
    : m_device_number(device_number), m_device_type(device_type)
{
}

ISIDevice::~ISIDevice() = default;

int ISIDevice::GetDeviceNumber() const
{
  return m_device_number;
}

SIDevices ISIDevice::GetDeviceType() const
{
  return m_device_type;
}

int ISIDevice::RunBuffer(u8* buffer, int request_length)
{
#ifdef _DEBUG
  DEBUG_LOG_FMT(SERIALINTERFACE, "Send Data Device({}) - Length({})   ", m_device_number,
                request_length);

  std::string temp;
  int num = 0;

  while (num < request_length)
  {
    temp += fmt::format("0x{:02x} ", buffer[num]);
    num++;

    if ((num % 8) == 0)
    {
      DEBUG_LOG_FMT(SERIALINTERFACE, "{}", temp);
      temp.clear();
    }
  }

  DEBUG_LOG_FMT(SERIALINTERFACE, "{}", temp);
#endif
  return 0;
}

int ISIDevice::TransferInterval()
{
  return 0;
}

void ISIDevice::DoState(PointerWrap& p)
{
}

// Check if a device class is inheriting from CSIDevice_GCController
// The goal of this function is to avoid special casing a long list of
// device types when there is no "real" input device, e.g. when playing
// a TAS movie, or netplay input.
bool SIDevice_IsGCController(SIDevices type)
{
  switch (type)
  {
  case SIDEVICE_GC_CONTROLLER:
  case SIDEVICE_WIIU_ADAPTER:
  case SIDEVICE_GC_TARUKONGA:
  case SIDEVICE_DANCEMAT:
  case SIDEVICE_GC_STEERING:
    return true;
  default:
    return false;
  }
}

// F A C T O R Y
std::unique_ptr<ISIDevice> SIDevice_Create(const SIDevices device, const int port_number)
{
  switch (device)
  {
  case SIDEVICE_GC_CONTROLLER:
    return std::make_unique<CSIDevice_GCController>(device, port_number);

  case SIDEVICE_WIIU_ADAPTER:
    return std::make_unique<CSIDevice_GCAdapter>(device, port_number);

  case SIDEVICE_DANCEMAT:
    return std::make_unique<CSIDevice_DanceMat>(device, port_number);

  case SIDEVICE_GC_STEERING:
    return std::make_unique<CSIDevice_GCSteeringWheel>(device, port_number);

  case SIDEVICE_GC_TARUKONGA:
    return std::make_unique<CSIDevice_TaruKonga>(device, port_number);

  case SIDEVICE_GC_GBA:
    return std::make_unique<CSIDevice_GBA>(device, port_number);

  case SIDEVICE_GC_KEYBOARD:
    return std::make_unique<CSIDevice_Keyboard>(device, port_number);

  case SIDEVICE_AM_BASEBOARD:
  case SIDEVICE_NONE:
  default:
    return std::make_unique<CSIDevice_Null>(device, port_number);
  }
}
}  // namespace SerialInterface
