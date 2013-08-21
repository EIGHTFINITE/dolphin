// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _BPMEMLOADER_H_
#define _BPMEMLOADER_H_


#include "Common.h"
#include "BPMemory.h"

void InitBPMemory();
void SWBPWritten(int address, int newvalue);
void SWLoadBPReg(u32 value);

#endif
