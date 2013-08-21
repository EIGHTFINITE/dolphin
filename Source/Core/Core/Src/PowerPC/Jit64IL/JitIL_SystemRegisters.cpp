// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"

#include "../../Core.h"
#include "../../CoreTiming.h"
#include "../../HW/SystemTimers.h"
#include "../PowerPC.h"
#include "../PPCTables.h"
#include "x64Emitter.h"
#include "x64ABI.h"
#include "Thunk.h"

#include "JitIL.h"

//#define INSTRUCTION_START Default(inst); return;
#define INSTRUCTION_START

void JitIL::mtspr(UGeckoInstruction inst)
{
	INSTRUCTION_START
		JITDISABLE(SystemRegisters)
		u32 iIndex = (inst.SPRU << 5) | (inst.SPRL & 0x1F);
	switch(iIndex) {
		case SPR_TL:
		case SPR_TU:
			Default(inst);
			return;
		case SPR_LR:
			ibuild.EmitStoreLink(ibuild.EmitLoadGReg(inst.RD));
			return;
		case SPR_CTR:
			ibuild.EmitStoreCTR(ibuild.EmitLoadGReg(inst.RD));
			return;
		case SPR_GQR0:
		case SPR_GQR0 + 1:
		case SPR_GQR0 + 2:
		case SPR_GQR0 + 3:
		case SPR_GQR0 + 4:
		case SPR_GQR0 + 5:
		case SPR_GQR0 + 6:
		case SPR_GQR0 + 7:
			ibuild.EmitStoreGQR(ibuild.EmitLoadGReg(inst.RD), iIndex - SPR_GQR0);
			return;
		case SPR_SRR0:
		case SPR_SRR1:
			ibuild.EmitStoreSRR(ibuild.EmitLoadGReg(inst.RD), iIndex - SPR_SRR0);
			return;
		default:
			Default(inst);
			return;
	}
}

void JitIL::mfspr(UGeckoInstruction inst)
{
	INSTRUCTION_START
		JITDISABLE(SystemRegisters)
		u32 iIndex = (inst.SPRU << 5) | (inst.SPRL & 0x1F);
	switch (iIndex)
	{
	case SPR_TL:
	case SPR_TU:
		Default(inst);
		return;
	case SPR_LR:
		ibuild.EmitStoreGReg(ibuild.EmitLoadLink(), inst.RD);
		return;
	case SPR_CTR:
		ibuild.EmitStoreGReg(ibuild.EmitLoadCTR(), inst.RD);
		return;
	case SPR_GQR0:
	case SPR_GQR0 + 1:
	case SPR_GQR0 + 2:
	case SPR_GQR0 + 3:
	case SPR_GQR0 + 4:
	case SPR_GQR0 + 5:
	case SPR_GQR0 + 6:
	case SPR_GQR0 + 7:
		ibuild.EmitStoreGReg(ibuild.EmitLoadGQR(iIndex - SPR_GQR0), inst.RD);
		return;
	default:
		Default(inst);
		return;
	}
}


// =======================================================================================
// Don't interpret this, if we do we get thrown out
// --------------
void JitIL::mtmsr(UGeckoInstruction inst)
{
	ibuild.EmitStoreMSR(ibuild.EmitLoadGReg(inst.RS), ibuild.EmitIntConst(js.compilerPC));
	ibuild.EmitBranchUncond(ibuild.EmitIntConst(js.compilerPC + 4));
}
// ==============


void JitIL::mfmsr(UGeckoInstruction inst)
{
	INSTRUCTION_START
		JITDISABLE(SystemRegisters)
		ibuild.EmitStoreGReg(ibuild.EmitLoadMSR(), inst.RD);
}

void JitIL::mftb(UGeckoInstruction inst)
{
	INSTRUCTION_START;
	JITDISABLE(SystemRegisters)
		mfspr(inst);
}

void JitIL::mfcr(UGeckoInstruction inst)
{
	INSTRUCTION_START;
	JITDISABLE(SystemRegisters)

	IREmitter::InstLoc d = ibuild.EmitIntConst(0);
	for (int i = 0; i < 8; ++i)
	{
		d = ibuild.EmitShl(d, ibuild.EmitIntConst(4));
		d = ibuild.EmitOr(d, ibuild.EmitLoadCR(i));
	}
	ibuild.EmitStoreGReg(d, inst.RD);
}

void JitIL::mtcrf(UGeckoInstruction inst)
{
	INSTRUCTION_START;
	JITDISABLE(SystemRegisters)

	IREmitter::InstLoc s = ibuild.EmitLoadGReg(inst.RS);
	for (int i = 0; i < 8; ++i)
	{
		if (inst.CRM & (0x80 >> i))
		{
			IREmitter::InstLoc value;
			value = ibuild.EmitShrl(s, ibuild.EmitIntConst(28 - i * 4));
			value = ibuild.EmitAnd(value, ibuild.EmitIntConst(0xF));
			ibuild.EmitStoreCR(value, i);
		}
	}
}

void JitIL::mcrf(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(SystemRegisters)

	if (inst.CRFS != inst.CRFD)
	{
		ibuild.EmitStoreCR(ibuild.EmitLoadCR(inst.CRFS), inst.CRFD);
	}
}

void JitIL::crXX(UGeckoInstruction inst)
{
	// Ported from Jit_SystemRegister.cpp

	// Get bit CRBA in EAX aligned with bit CRBD
	const int shiftA = (inst.CRBD & 3) - (inst.CRBA & 3);
	IREmitter::InstLoc eax = ibuild.EmitLoadCR(inst.CRBA >> 2);
	if (shiftA < 0)
		eax = ibuild.EmitShl(eax, ibuild.EmitIntConst(-shiftA));
	else if (shiftA > 0)
		eax = ibuild.EmitShrl(eax, ibuild.EmitIntConst(shiftA));

	// Get bit CRBB in ECX aligned with bit CRBD
	const int shiftB = (inst.CRBD & 3) - (inst.CRBB & 3);
	IREmitter::InstLoc ecx = ibuild.EmitLoadCR(inst.CRBB >> 2);
	if (shiftB < 0)
		ecx = ibuild.EmitShl(ecx, ibuild.EmitIntConst(-shiftB));
	else if (shiftB > 0)
		ecx = ibuild.EmitShrl(ecx, ibuild.EmitIntConst(shiftB));

	// Compute combined bit
	const unsigned subop = inst.SUBOP10;
	switch (subop) {
		case 257:
			// crand
			eax = ibuild.EmitAnd(eax, ecx);	
			break;
		case 129:
			// crandc
			ecx = ibuild.EmitNot(ecx);
			eax = ibuild.EmitAnd(eax, ecx);	
			break;
		case 289:
			// creqv
			eax = ibuild.EmitXor(eax, ecx);
			eax = ibuild.EmitNot(eax);
			break;
		case 225:
			// crnand
			eax = ibuild.EmitAnd(eax, ecx);
			eax = ibuild.EmitNot(eax);
			break;
		case 33:
			// crnor
			eax = ibuild.EmitOr(eax, ecx);
			eax = ibuild.EmitNot(eax);
			break;
		case 449:
			// cror
			eax = ibuild.EmitOr(eax, ecx);
			break;
		case 417:
			// crorc
			ecx = ibuild.EmitNot(ecx);
			eax = ibuild.EmitOr(eax, ecx);
			break;
		case 193:
			// crxor
			eax = ibuild.EmitXor(eax, ecx);
			break;
		default:
			PanicAlert("crXX: invalid instruction");
			break;
	}

	// Store result bit in CRBD
	eax = ibuild.EmitAnd(eax, ibuild.EmitIntConst(0x8 >> (inst.CRBD & 3)));
	IREmitter::InstLoc bd = ibuild.EmitLoadCR(inst.CRBD >> 2);
	bd = ibuild.EmitAnd(bd, ibuild.EmitIntConst(~(0x8 >> (inst.CRBD & 3))));
	bd = ibuild.EmitOr(bd, eax);
	ibuild.EmitStoreCR(bd, inst.CRBD >> 2);
}
