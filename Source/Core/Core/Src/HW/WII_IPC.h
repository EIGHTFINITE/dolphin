// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _WII_IPC_H_
#define _WII_IPC_H_

#include "Common.h"
class PointerWrap;

namespace WII_IPCInterface
{

enum StarletInterruptCause
{
	INT_CAUSE_TIMER			=    0x1,
	INT_CAUSE_NAND			=    0x2,
	INT_CAUSE_AES			=    0x4,
	INT_CAUSE_SHA1			=    0x8,
	INT_CAUSE_EHCI			=   0x10,
	INT_CAUSE_OHCI0			=   0x20,
	INT_CAUSE_OHCI1			=   0x40,
	INT_CAUSE_SD			=   0x80,
	INT_CAUSE_WIFI			=  0x100,

	INT_CAUSE_GPIO_BROADWAY	=  0x400,
	INT_CAUSE_GPIO_STARLET	=  0x800,

	INT_CAUSE_RST_BUTTON	= 0x40000,

	INT_CAUSE_IPC_BROADWAY	= 0x40000000,
	INT_CAUSE_IPC_STARLET	= 0x80000000
};

void Init();
void Reset();
void Shutdown();
void DoState(PointerWrap &p);

void Read32(u32& _rReturnValue, const u32 _Address);
void Write32(const u32 _Value, const u32 _Address);

void UpdateInterrupts(u64 userdata = 0, int cyclesLate = 0);
void GenerateAck(u32 _Address);
void GenerateReply(u32 _Address);

bool IsReady();

}

#endif
