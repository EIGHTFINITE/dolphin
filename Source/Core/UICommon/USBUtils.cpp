// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "UICommon/USBUtils.h"

#include <string_view>

#include <fmt/format.h>
#ifdef __LIBUSB__
#include <libusb.h>
#endif

#include "Common/CommonTypes.h"
#include "Core/LibusbUtils.h"

// Because opening and getting the device name from devices is slow, especially on Windows
// with usbdk, we cannot do that for every single device. We should however still show
// device names for known Wii peripherals.
static const std::map<std::pair<u16, u16>, std::string_view> s_wii_peripherals{{
    {{0x046d, 0x0a03}, "Logitech Microphone"},
    {{0x057e, 0x0308}, "Wii Speak"},
    {{0x057e, 0x0309}, "Nintendo USB Microphone"},
    {{0x057e, 0x030a}, "Ubisoft Motion Tracking Camera"},
    {{0x0e6f, 0x0129}, "Disney Infinity Reader (Portal Device)"},
    {{0x1430, 0x0100}, "Tony Hawk Ride Skateboard"},
    {{0x1430, 0x0150}, "Skylanders Portal"},
    {{0x1bad, 0x0004}, "Harmonix Guitar Controller"},
    {{0x1bad, 0x3110}, "Rock Band 3 Mustang Guitar Dongle"},
    {{0x1bad, 0x3138}, "Harmonix Drum Controller for Nintendo Wii"},
    {{0x1bad, 0x3338}, "Harmonix RB3 MIDI Keyboard Interface for Nintendo Wii"},
    {{0x1bad, 0x3538}, "Harmonix RB3 MIDI Guitar Interface for Nintendo Wii"},
    {{0x1bad, 0x3430}, "Rock Band Drum Set"},
    {{0x21a4, 0xac40}, "EA Active NFL"},
}};

namespace USBUtils
{
std::map<std::pair<u16, u16>, std::string> GetInsertedDevices()
{
  std::map<std::pair<u16, u16>, std::string> devices;

#ifdef __LIBUSB__
  LibusbUtils::Context context;
  if (!context.IsValid())
    return devices;

  context.GetDeviceList([&](libusb_device* device) {
    libusb_device_descriptor descr;
    libusb_get_device_descriptor(device, &descr);
    const std::pair<u16, u16> vid_pid{descr.idVendor, descr.idProduct};
    devices[vid_pid] = GetDeviceName(vid_pid);
    return true;
  });
#endif

  return devices;
}

std::string GetDeviceName(const std::pair<u16, u16> vid_pid)
{
  const auto iter = s_wii_peripherals.find(vid_pid);
  const std::string_view device_name = iter == s_wii_peripherals.cend() ? "Unknown" : iter->second;
  return fmt::format("{:04x}:{:04x} - {}", vid_pid.first, vid_pid.second, device_name);
}
}  // namespace USBUtils
