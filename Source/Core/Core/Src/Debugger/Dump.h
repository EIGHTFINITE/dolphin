// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// Purpose: uncompress the dumps from costis GC-Debugger tool
//
//
#ifndef _DUMP_H
#define _DUMP_H

#include "Common.h"

class CDump
{
public:

	CDump(const char* _szFilename);
	~CDump();
	
	int GetNumberOfSteps();
	u32 GetGPR(int _step, int _gpr);
	u32 GetPC(int _step);

private:
	enum
	{
		OFFSET_GPR		= 0x4,
		OFFSET_PC		= 0x194,
		STRUCTUR_SIZE	= 0x2BC
	};

	u8 *m_pData;

	bool m_bInit;
	size_t m_size;

	u32 Read32(u32 _pos);
};

#endif
