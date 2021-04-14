// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/USB/Bluetooth/BTBase.h"

class PointerWrap;

namespace IOS::HLE
{
class BluetoothStubDevice final : public BluetoothBaseDevice
{
public:
  using BluetoothBaseDevice::BluetoothBaseDevice;
  std::optional<IPCReply> Open(const OpenRequest& request) override;
  void DoState(PointerWrap& p) override;
};
}  // namespace IOS::HLE
