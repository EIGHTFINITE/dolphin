// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

#include "Core/HW/SystemTimers.h"

class IWII_IPC_HLE_Device;
class PointerWrap;

struct IPCCommandResult
{
	bool send_reply;
	u64 reply_delay_ticks;
};

enum IPCCommandType : u32
{
	IPC_CMD_OPEN   = 1,
	IPC_CMD_CLOSE  = 2,
	IPC_CMD_READ   = 3,
	IPC_CMD_WRITE  = 4,
	IPC_CMD_SEEK   = 5,
	IPC_CMD_IOCTL  = 6,
	IPC_CMD_IOCTLV = 7,
	// IPC_REP_ASYNC is used for messages that are automatically
	// sent to an IOS queue when an asynchronous syscall completes.
	// Reference: http://wiibrew.org/wiki/IOS
	IPC_REP_ASYNC  = 8
};

namespace WII_IPC_HLE_Interface
{

#define IPC_FIRST_ID  0x00 // First IPC device ID
#define IPC_MAX_FILES 0x10 // First IPC file ID

// Init
void Init();

// Shutdown
void Shutdown();

// Reset
void Reset(bool _bHard = false);

// Do State
void DoState(PointerWrap &p);

// Set default content file
void SetDefaultContentFile(const std::string& _rFilename);
void ES_DIVerify(const std::vector<u8>& tmd);

void SDIO_EventNotify();


std::shared_ptr<IWII_IPC_HLE_Device> CreateFileIO(u32 _DeviceID, const std::string& _rDeviceName);

std::shared_ptr<IWII_IPC_HLE_Device> GetDeviceByName(const std::string& _rDeviceName);
std::shared_ptr<IWII_IPC_HLE_Device> AccessDeviceByID(u32 _ID);
int getFreeDeviceId();

// Update
void Update();

// Update Devices
void UpdateDevices();

void ExecuteCommand(u32 _Address);

void EnqueueRequest(u32 address);
void EnqueueReply(u32 address, int cycles_in_future = 0);
void EnqueueReply_Threadsafe(u32 address, int cycles_in_future = 0);
void EnqueueReply_Immediate(u32 address);
void EnqueueCommandAcknowledgement(u32 _Address, int cycles_in_future = 0);

} // end of namespace WII_IPC_HLE_Interface
