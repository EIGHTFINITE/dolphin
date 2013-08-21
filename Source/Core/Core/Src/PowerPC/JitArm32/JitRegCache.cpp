// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Jit.h"
#include "JitRegCache.h"

ArmRegCache::ArmRegCache()
{
	emit = 0;
}

void ArmRegCache::Init(ARMXEmitter *emitter)
{
	emit = emitter;
	ARMReg *PPCRegs = GetPPCAllocationOrder(NUMPPCREG);
	ARMReg *Regs = GetAllocationOrder(NUMARMREG);

	for(u8 a = 0; a < NUMPPCREG; ++a)
	{
		ArmCRegs[a].PPCReg = 33;
		ArmCRegs[a].Reg = PPCRegs[a];
		ArmCRegs[a].LastLoad = 0;
	}
	for(u8 a = 0; a < NUMARMREG; ++a)
	{
		ArmRegs[a].Reg = Regs[a];
		ArmRegs[a].free = true;
	}
}
void ArmRegCache::Start(PPCAnalyst::BlockRegStats &stats)
{
}

ARMReg *ArmRegCache::GetPPCAllocationOrder(int &count)
{
	// This will return us the allocation order of the registers we can use on
	// the ppc side.
	static ARMReg allocationOrder[] = 
	{
		R0, R1, R2, R3, R4, R5, R6, R7, R8
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}
ARMReg *ArmRegCache::GetAllocationOrder(int &count)
{
	// This will return us the allocation order of the registers we can use on
	// the host side.
	static ARMReg allocationOrder[] = 
	{
		R14, R12, R11, R10
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

ARMReg ArmRegCache::GetReg(bool AutoLock)
{
	for(u8 a = 0; a < NUMARMREG; ++a)
		if(ArmRegs[a].free)
		{
			// Alright, this one is free
			if (AutoLock)
				ArmRegs[a].free = false;
			return ArmRegs[a].Reg;
		}
	// Uh Oh, we have all them locked....
	_assert_msg_(_DYNA_REC_, false, "All available registers are locked dumb dumb");
	return R0;
}

void ArmRegCache::Unlock(ARMReg R0, ARMReg R1, ARMReg R2, ARMReg R3)
{
	for(u8 RegNum = 0; RegNum < NUMARMREG; ++RegNum)
	{
		if(ArmRegs[RegNum].Reg == R0)
		{
			_assert_msg_(_DYNA_REC, !ArmRegs[RegNum].free, "This register is already unlocked");
			ArmRegs[RegNum].free = true;
		}
		if( R1 != INVALID_REG && ArmRegs[RegNum].Reg == R1) ArmRegs[RegNum].free = true;
		if( R2 != INVALID_REG && ArmRegs[RegNum].Reg == R2) ArmRegs[RegNum].free = true;
		if( R3 != INVALID_REG && ArmRegs[RegNum].Reg == R3) ArmRegs[RegNum].free = true;
	}
}
u32 ArmRegCache::GetLeastUsedRegister(bool increment)
{
	u32 HighestUsed = 0;
	u8 lastRegIndex = 0;
	for(u8 a = 0; a < NUMPPCREG; ++a){
		if (increment)
			++ArmCRegs[a].LastLoad;
		if (ArmCRegs[a].LastLoad > HighestUsed)
		{
			HighestUsed = ArmCRegs[a].LastLoad;
			lastRegIndex = a;
		}
	}
	return lastRegIndex;
}
bool ArmRegCache::FindFreeRegister(u32 &regindex)
{
	for (u8 a = 0; a < NUMPPCREG; ++a)
		if (ArmCRegs[a].PPCReg == 33)
		{
			regindex = a;
			return true;
		}
	return false;
}

ARMReg ArmRegCache::R(u32 preg)
{	
	if (regs[preg].GetType() == REG_IMM)
	{
		return BindToRegister(preg);
		//asm ("bkpt #1;");
	}
	u32 lastRegIndex = GetLeastUsedRegister(true);

	// Check if already Loaded
	if(regs[preg].GetType() == REG_REG)
	{
		u8 a = regs[preg].GetRegIndex();
		ArmCRegs[a].LastLoad = 0;
		return ArmCRegs[a].Reg;
	}
	
	// Check if we have a free register
	u32 regindex;
	if (FindFreeRegister(regindex))
	{
		emit->LDR(ArmCRegs[regindex].Reg, R9, PPCSTATE_OFF(gpr) + preg * 4);
		ArmCRegs[regindex].PPCReg = preg;
		ArmCRegs[regindex].LastLoad = 0;

		regs[preg].LoadToReg(regindex);
		return ArmCRegs[regindex].Reg;
	}

	// Alright, we couldn't get a free space, dump that least used register
	emit->STR(ArmCRegs[lastRegIndex].Reg, R9, PPCSTATE_OFF(gpr) + ArmCRegs[lastRegIndex].PPCReg * 4);
	emit->LDR(ArmCRegs[lastRegIndex].Reg, R9, PPCSTATE_OFF(gpr) + preg * 4);

	regs[ArmCRegs[lastRegIndex].PPCReg].Flush();

	ArmCRegs[lastRegIndex].PPCReg = preg;
	ArmCRegs[lastRegIndex].LastLoad = 0;
	
	regs[preg].LoadToReg(lastRegIndex);

	return ArmCRegs[lastRegIndex].Reg;		 
}

ARMReg ArmRegCache::BindToRegister(u32 preg)
{
	_assert_msg_(DYNA_REC, regs[preg].GetType() == REG_IMM, "Can't BindToRegister with a REG");
	u32 lastRegIndex = GetLeastUsedRegister(false);
	u32 freeRegIndex;
	if (FindFreeRegister(freeRegIndex))
	{
		emit->MOVI2R(ArmCRegs[freeRegIndex].Reg, regs[preg].GetImm());
		ArmCRegs[freeRegIndex].PPCReg = preg;
		ArmCRegs[freeRegIndex].LastLoad = 0;
		regs[preg].LoadToReg(freeRegIndex);
		return ArmCRegs[freeRegIndex].Reg;
	}
	else
	{
		emit->STR(ArmCRegs[lastRegIndex].Reg, R9, PPCSTATE_OFF(gpr) + ArmCRegs[lastRegIndex].PPCReg * 4);
		emit->MOVI2R(ArmCRegs[lastRegIndex].Reg, regs[preg].GetImm());

		regs[ArmCRegs[lastRegIndex].PPCReg].Flush();

		ArmCRegs[lastRegIndex].PPCReg = preg;
		ArmCRegs[lastRegIndex].LastLoad = 0;

		regs[preg].LoadToReg(lastRegIndex);
		return ArmCRegs[lastRegIndex].Reg;
	}
}

void ArmRegCache::SetImmediate(u32 preg, u32 imm)
{
	if (regs[preg].GetType() == REG_REG)
	{
		// Dump real reg at this point
		u32 regindex = regs[preg].GetRegIndex();
		ArmCRegs[regindex].PPCReg = 33;
		ArmCRegs[regindex].LastLoad = 0;
	}
	regs[preg].LoadToImm(imm);
}

void ArmRegCache::Flush()
{
	for (u8 a = 0; a < 32; ++a)
	{
		if (regs[a].GetType() == REG_IMM)
			BindToRegister(a);	
		if (regs[a].GetType() == REG_REG)
		{
			u32 regindex = regs[a].GetRegIndex();
			emit->STR(ArmCRegs[regindex].Reg, R9, PPCSTATE_OFF(gpr) + a * 4);
			ArmCRegs[regindex].PPCReg = 33;
			ArmCRegs[regindex].LastLoad = 0;
		}
		
		regs[a].Flush();
	}
}

