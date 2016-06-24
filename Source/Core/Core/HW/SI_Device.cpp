// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <memory>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/StringUtil.h"
#include "Common/Logging/Log.h"
#include "Core/HW/SI_Device.h"
#include "Core/HW/SI_DeviceAMBaseboard.h"
#include "Core/HW/SI_DeviceDanceMat.h"
#include "Core/HW/SI_DeviceGBA.h"
#include "Core/HW/SI_DeviceGCAdapter.h"
#include "Core/HW/SI_DeviceGCController.h"
#include "Core/HW/SI_DeviceGCSteeringWheel.h"
#include "Core/HW/SI_DeviceKeyboard.h"

// --- interface ISIDevice ---
int ISIDevice::RunBuffer(u8* _pBuffer, int _iLength)
{
#ifdef _DEBUG
	DEBUG_LOG(SERIALINTERFACE, "Send Data Device(%i) - Length(%i)   ", ISIDevice::m_iDeviceNumber, _iLength);

	std::string temp;
	int num = 0;

	while (num < _iLength)
	{
		temp += StringFromFormat("0x%02x ", _pBuffer[num^3]);
		num++;

		if ((num % 8) == 0)
		{
			DEBUG_LOG(SERIALINTERFACE, "%s", temp.c_str());
			temp.clear();
		}
	}

	DEBUG_LOG(SERIALINTERFACE, "%s", temp.c_str());
#endif
	return 0;
}

int ISIDevice::TransferInterval()
{
	return 0;
}

// Stub class for saying nothing is attached, and not having to deal with null pointers :)
class CSIDevice_Null : public ISIDevice
{
public:
	CSIDevice_Null(SIDevices device, int _iDeviceNumber) : ISIDevice(device, _iDeviceNumber) {}
	virtual ~CSIDevice_Null() {}

	int RunBuffer(u8* _pBuffer, int _iLength) override {
		reinterpret_cast<u32*>(_pBuffer)[0] = SI_ERROR_NO_RESPONSE;
		return 4;
	}
	bool GetData(u32& _Hi, u32& _Low) override {
		_Hi = 0x80000000;
		return true;
	}
	void SendCommand(u32 _Cmd, u8 _Poll) override {}
};


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
		return std::make_unique<CSIDevice_AMBaseboard>(device, port_number);

	case SIDEVICE_NONE:
	default:
		return std::make_unique<CSIDevice_Null>(device, port_number);
	}
}
