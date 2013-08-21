// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _SIDEVICE_H
#define _SIDEVICE_H

#include "Common.h"

class PointerWrap;

// Devices can reply with these
#define SI_ERROR_NO_RESPONSE    0x0008 // Nothing is attached
#define SI_ERROR_UNKNOWN        0x0040 // Unknown device is attached
#define SI_ERROR_BUSY           0x0080 // Still detecting

// Device types
#define SI_TYPE_MASK            0x18000000u // ???
#define SI_TYPE_GC              0x08000000u

// GC Controller types
#define SI_GC_NOMOTOR           0x20000000u // No rumble motor
#define SI_GC_STANDARD          0x01000000u

// SI Device IDs for emulator use
enum TSIDevices
{
	SI_NONE				= SI_ERROR_NO_RESPONSE,
	SI_N64_MIC			= 0x00010000,
	SI_N64_KEYBOARD		= 0x00020000,
	SI_N64_MOUSE		= 0x02000000,
	SI_N64_CONTROLLER	= 0x05000000,
	SI_GBA				= 0x00040000,
	SI_GC_CONTROLLER	= (SI_TYPE_GC | SI_GC_STANDARD),
	SI_GC_KEYBOARD		= (SI_TYPE_GC | 0x00200000),
	SI_GC_STEERING		= SI_TYPE_GC, // (shuffle2)I think the "chainsaw" is the same (Or else it's just standard)
	SI_DANCEMAT			= (SI_TYPE_GC | SI_GC_STANDARD | 0x00000300),
	SI_AM_BASEBOARD		= 0x10110800 // gets ORd with dipswitch state
};

// For configuration use, since some devices can have the same SI Device ID
enum SIDevices
{
	SIDEVICE_NONE,
	SIDEVICE_N64_MIC,
	SIDEVICE_N64_KEYBOARD,
	SIDEVICE_N64_MOUSE,
	SIDEVICE_N64_CONTROLLER,
	SIDEVICE_GC_GBA,
	SIDEVICE_GC_CONTROLLER,
	SIDEVICE_GC_KEYBOARD,
	SIDEVICE_GC_STEERING,
	SIDEVICE_DANCEMAT,
	SIDEVICE_GC_TARUKONGA,
	SIDEVICE_AM_BASEBOARD
};


class ISIDevice
{
protected:
	int m_iDeviceNumber;
	SIDevices m_deviceType;

public:
	// Constructor
	ISIDevice(SIDevices deviceType, int _iDeviceNumber)
		: m_iDeviceNumber(_iDeviceNumber)
		, m_deviceType(deviceType)
	{}

	// Destructor
	virtual ~ISIDevice() {}

	// Run the SI Buffer
	virtual int RunBuffer(u8* _pBuffer, int _iLength);

	// Return true on new data
	virtual bool GetData(u32& _Hi, u32& _Low) = 0;

	// Send a command directly (no detour per buffer)
	virtual void SendCommand(u32 _Cmd, u8 _Poll) = 0;

	// Savestate support
	virtual void DoState(PointerWrap& p) {}


	int GetDeviceNumber()
	{
		return m_iDeviceNumber;
	}

	SIDevices GetDeviceType()
	{
		return m_deviceType;
	}
};

extern ISIDevice* SIDevice_Create(const SIDevices device, const int port_number);

#endif
