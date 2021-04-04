// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/IOS/USB/Bluetooth/BTStub.h"

#include "Common/ChunkFile.h"
#include "Common/MsgHandler.h"
#include "Core/Core.h"

namespace IOS::HLE
{
std::optional<IPCReply> BluetoothStubDevice::Open(const OpenRequest& request)
{
  PanicAlertFmtT("Bluetooth passthrough mode is enabled, but Dolphin was built without libusb."
                 " Passthrough mode cannot be used.");
  return IPCReply(IPC_ENOENT);
}

void BluetoothStubDevice::DoState(PointerWrap& p)
{
  Core::DisplayMessage("The current IPC_HLE_Device_usb is a stub. Aborting load.", 4000);
  p.SetMode(PointerWrap::MODE_VERIFY);
}
}  // namespace IOS::HLE
