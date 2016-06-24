// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Arm64Emitter.h"
#include "Common/Assert.h"
#include "Common/CommonTypes.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCTables.h"
#include "Core/PowerPC/JitArm64/Jit.h"

FixupBranch JitArm64::JumpIfCRFieldBit(int field, int bit, bool jump_if_set)
{
	ARM64Reg WA = gpr.GetReg();
	ARM64Reg XA = EncodeRegTo64(WA);

	FixupBranch branch;
	switch (bit)
	{
	case CR_SO_BIT:  // check bit 61 set
		LDR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val[field]));
		branch = jump_if_set ? TBNZ(XA, 61) : TBZ(XA, 61);
	break;
	case CR_EQ_BIT:  // check bits 31-0 == 0
		LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(cr_val[field]));
		branch = jump_if_set ? CBZ(WA) : CBNZ(WA);
	break;
	case CR_GT_BIT:  // check val > 0
		LDR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val[field]));
		CMP(XA, SP);
		branch = B(jump_if_set ? CC_GT : CC_LE);
	break;
	case CR_LT_BIT:  // check bit 62 set
		LDR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val[field]));
		branch = jump_if_set ? TBNZ(XA, 62) : TBZ(XA, 62);
	break;
	default:
		_assert_msg_(DYNA_REC, false, "Invalid CR bit");
	}

	gpr.Unlock(WA);
	return branch;
}

void JitArm64::mtmsr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	gpr.BindToRegister(inst.RS, true);
	STR(INDEX_UNSIGNED, gpr.R(inst.RS), PPC_REG, PPCSTATE_OFF(msr));

	gpr.Flush(FlushMode::FLUSH_ALL);
	fpr.Flush(FlushMode::FLUSH_ALL);

	WriteExceptionExit(js.compilerPC + 4, true);
}

void JitArm64::mfmsr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	gpr.BindToRegister(inst.RD, false);
	LDR(INDEX_UNSIGNED, gpr.R(inst.RD), PPC_REG, PPCSTATE_OFF(msr));
}

void JitArm64::mcrf(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	if (inst.CRFS != inst.CRFD)
	{
		ARM64Reg WA = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		LDR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val[inst.CRFS]));
		STR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val[inst.CRFD]));
		gpr.Unlock(WA);
	}
}

void JitArm64::mfsr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	gpr.BindToRegister(inst.RD, false);
	LDR(INDEX_UNSIGNED, gpr.R(inst.RD), PPC_REG, PPCSTATE_OFF(sr[inst.SR]));
}

void JitArm64::mtsr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	gpr.BindToRegister(inst.RS, true);
	STR(INDEX_UNSIGNED, gpr.R(inst.RS), PPC_REG, PPCSTATE_OFF(sr[inst.SR]));
}

void JitArm64::mfsrin(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	u32 b = inst.RB, d = inst.RD;
	gpr.BindToRegister(d, d == b);

	ARM64Reg index = gpr.GetReg();
	ARM64Reg index64 = EncodeRegTo64(index);
	ARM64Reg RB = gpr.R(b);

	UBFM(index, RB, 28, 31);
	ADD(index64, PPC_REG, index64, ArithOption(index64, ST_LSL, 2));
	LDR(INDEX_UNSIGNED, gpr.R(d), index64, PPCSTATE_OFF(sr[0]));

	gpr.Unlock(index);
}

void JitArm64::mtsrin(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	u32 b = inst.RB, d = inst.RD;
	gpr.BindToRegister(d, d == b);

	ARM64Reg index = gpr.GetReg();
	ARM64Reg index64 = EncodeRegTo64(index);
	ARM64Reg RB = gpr.R(b);

	UBFM(index, RB, 28, 31);
	ADD(index64, PPC_REG, index64, ArithOption(index64, ST_LSL, 2));
	STR(INDEX_UNSIGNED, gpr.R(d), index64, PPCSTATE_OFF(sr[0]));

	gpr.Unlock(index);
}

void JitArm64::twx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	s32 a = inst.RA;

	ARM64Reg WA = gpr.GetReg();

	if (inst.OPCD == 3) // twi
	{
		if (inst.SIMM_16 >= 0 && inst.SIMM_16 < 4096)
		{
			// Can fit in immediate in to the instruction encoding
			CMP(gpr.R(a), inst.SIMM_16);
		}
		else
		{
			MOVI2R(WA, (s32)(s16)inst.SIMM_16);
			CMP(gpr.R(a), WA);
		}
	}
	else // tw
	{
		CMP(gpr.R(a), gpr.R(inst.RB));
	}

	std::vector<FixupBranch> fixups;
	CCFlags conditions[] = { CC_LT, CC_GT, CC_EQ, CC_VC, CC_VS };

	for (int i = 0; i < 5; i++)
	{
		if (inst.TO & (1 << i))
		{
			FixupBranch f = B(conditions[i]);
			fixups.push_back(f);
		}
	}
	FixupBranch dont_trap = B();

	for (const FixupBranch& fixup : fixups)
	{
		SetJumpTarget(fixup);
	}

	FixupBranch far = B();
	SwitchToFarCode();
	SetJumpTarget(far);

	gpr.Flush(FlushMode::FLUSH_MAINTAIN_STATE);
	fpr.Flush(FlushMode::FLUSH_MAINTAIN_STATE);

	LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
	ORR(WA, WA, 24, 0); // Same as WA | EXCEPTION_PROGRAM
	STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
	gpr.Unlock(WA);

	WriteExceptionExit(js.compilerPC);

	SwitchToNearCode();

	SetJumpTarget(dont_trap);

	if (!analyzer.HasOption(PPCAnalyst::PPCAnalyzer::OPTION_CONDITIONAL_CONTINUE))
	{
		gpr.Flush(FlushMode::FLUSH_ALL);
		fpr.Flush(FlushMode::FLUSH_ALL);
		WriteExit(js.compilerPC + 4);
	}
}

void JitArm64::mfspr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	u32 iIndex = (inst.SPRU << 5) | (inst.SPRL & 0x1F);
	int d = inst.RD;
	switch (iIndex)
	{
	case SPR_TL:
	case SPR_TU:
	{
		// The inline implementation here is inaccurate and out of date as of PR3601
		FALLBACK_IF(true); // Fallback to interpreted version.

		/*
		ARM64Reg WA = gpr.GetReg();
		ARM64Reg WB = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		ARM64Reg XB = EncodeRegTo64(WB);

		// An inline implementation of CoreTiming::GetFakeTimeBase, since in timer-heavy games the
		// cost of calling out to C for this is actually significant.
		MOVI2R(XA, (u64)&CoreTiming::g_globalTimer);
		LDR(INDEX_UNSIGNED, XA, XA, 0);
		MOVI2R(XB, (u64)&CoreTiming::g_fakeTBStartTicks);
		LDR(INDEX_UNSIGNED, XB, XB, 0);
		SUB(XA, XA, XB);

		// It might seem convenient to correct the timer for the block position here for even more accurate
		// timing, but as of currently, this can break games. If we end up reading a time *after* the time
		// at which an interrupt was supposed to occur, e.g. because we're 100 cycles into a block with only
		// 50 downcount remaining, some games don't function correctly, such as Karaoke Party Revolution,
		// which won't get past the loading screen.
		// a / 12 = (a * 0xAAAAAAAAAAAAAAAB) >> 67
		ORR(XB, SP, 1, 60);
		ADD(XB, XB, 1);
		UMULH(XA, XA, XB);

		MOVI2R(XB, (u64)&CoreTiming::g_fakeTBStartValue);
		LDR(INDEX_UNSIGNED, XB, XB, 0);
		ADD(XA, XB, XA, ArithOption(XA, ST_LSR, 3));
		STR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(spr[SPR_TL]));

		if (MergeAllowedNextInstructions(1))
		{
			const UGeckoInstruction& next = js.op[1].inst;
			// Two calls of TU/TL next to each other are extremely common in typical usage, so merge them
			// if we can.
			u32 nextIndex = (next.SPRU << 5) | (next.SPRL & 0x1F);
			// Be careful; the actual opcode is for mftb (371), not mfspr (339)
			int n = next.RD;
			if (next.OPCD == 31 && next.SUBOP10 == 371 && (nextIndex == SPR_TU || nextIndex == SPR_TL) && n != d)
			{
				js.downcountAmount++;
				js.skipInstructions = 1;
				gpr.BindToRegister(d, false);
				gpr.BindToRegister(n, false);
				if (iIndex == SPR_TL)
					MOV(gpr.R(d), WA);
				else
					ORR(EncodeRegTo64(gpr.R(d)), SP, XA, ArithOption(XA, ST_LSR, 32));

				if (nextIndex == SPR_TL)
					MOV(gpr.R(n), WA);
				else
					ORR(EncodeRegTo64(gpr.R(n)), SP, XA, ArithOption(XA, ST_LSR, 32));

				gpr.Unlock(WA, WB);
				break;
			}
		}
		gpr.BindToRegister(d, false);
		if (iIndex == SPR_TU)
			ORR(EncodeRegTo64(gpr.R(d)), SP, XA, ArithOption(XA, ST_LSR, 32));
		else
			MOV(gpr.R(d), WA);
		gpr.Unlock(WA, WB);*/
	}
	break;
	case SPR_XER:
	{
		gpr.BindToRegister(d, false);
		ARM64Reg RD = gpr.R(d);
		ARM64Reg WA = gpr.GetReg();
		LDRH(INDEX_UNSIGNED, RD, PPC_REG, PPCSTATE_OFF(xer_stringctrl));
		LDRB(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(xer_ca));
		ORR(RD, RD, WA, ArithOption(WA, ST_LSL, XER_CA_SHIFT));
		LDRB(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(xer_so_ov));
		ORR(RD, RD, WA, ArithOption(WA, ST_LSL, XER_OV_SHIFT));
		gpr.Unlock(WA);
	}
	break;
	case SPR_WPAR:
	case SPR_DEC:
		FALLBACK_IF(true);
	default:
		gpr.BindToRegister(d, false);
		ARM64Reg RD = gpr.R(d);
		LDR(INDEX_UNSIGNED, RD, PPC_REG, PPCSTATE_OFF(spr) + iIndex * 4);
		break;
	}
}

void JitArm64::mftb(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);
	mfspr(inst);
}

void JitArm64::mtspr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	u32 iIndex = (inst.SPRU << 5) | (inst.SPRL & 0x1F);

	switch (iIndex)
	{
	case SPR_DMAU:

	case SPR_SPRG0:
	case SPR_SPRG1:
	case SPR_SPRG2:
	case SPR_SPRG3:

	case SPR_SRR0:
	case SPR_SRR1:
		// These are safe to do the easy way, see the bottom of this function.
	break;

	case SPR_LR:
	case SPR_CTR:
	case SPR_GQR0:
	case SPR_GQR0 + 1:
	case SPR_GQR0 + 2:
	case SPR_GQR0 + 3:
	case SPR_GQR0 + 4:
	case SPR_GQR0 + 5:
	case SPR_GQR0 + 6:
	case SPR_GQR0 + 7:
		// These are safe to do the easy way, see the bottom of this function.
	break;
	case SPR_XER:
	{
		ARM64Reg RD = gpr.R(inst.RD);
		ARM64Reg WA = gpr.GetReg();
		AND(WA, RD, 24, 30);
		STRH(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(xer_stringctrl));
		UBFM(WA, RD, XER_CA_SHIFT, XER_CA_SHIFT + 1);
		STRB(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(xer_ca));
		UBFM(WA, RD, XER_OV_SHIFT, 31); // Same as WA = RD >> XER_OV_SHIFT
		STRB(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(xer_so_ov));
		gpr.Unlock(WA);
	}
	break;
	default:
		FALLBACK_IF(true);
	}

	// OK, this is easy.
	ARM64Reg RD = gpr.R(inst.RD);
	STR(INDEX_UNSIGNED, RD, PPC_REG,  PPCSTATE_OFF(spr) + iIndex * 4);
}

void JitArm64::crXXX(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	// Special case: crclr
	if (inst.CRBA == inst.CRBB && inst.CRBA == inst.CRBD && inst.SUBOP10 == 193)
	{
		// Clear CR field bit
		int field = inst.CRBD >> 2;
		int bit = 3 - (inst.CRBD & 3);

		ARM64Reg WA = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		LDR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);
		switch (bit)
		{
		case CR_SO_BIT:
			AND(XA, XA, 64 - 62, 62, true); // XA & ~(1<<61)
			break;

		case CR_EQ_BIT:
			ORR(XA, XA, 0, 0, true); // XA | 1<<0
			break;

		case CR_GT_BIT:
			ORR(XA, XA, 64 - 63, 0, true); // XA | 1<<63
			break;

		case CR_LT_BIT:
			AND(XA, XA, 64 - 63, 62, true); // XA & ~(1<<62)
			break;
		}
		STR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);
		gpr.Unlock(WA);
		return;
	}

	// Special case: crset
	if (inst.CRBA == inst.CRBB && inst.CRBA == inst.CRBD && inst.SUBOP10 == 289)
	{
		// SetCRFieldBit
		int field = inst.CRBD >> 2;
		int bit = 3 - (inst.CRBD & 3);

		ARM64Reg WA = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		LDR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);

		if (bit != CR_GT_BIT)
		{
			ARM64Reg WB = gpr.GetReg();
			ARM64Reg XB = EncodeRegTo64(WB);
			ORR(XB, XA, 64 - 63, 0, true); // XA | 1<<63
			CMP(XA, ZR);
			CSEL(XA, XA, XB, CC_NEQ);
			gpr.Unlock(WB);
		}

		switch (bit)
		{
		case CR_SO_BIT:
			ORR(XA, XA, 64 - 61, 0, true); // XA | 1<<61
			break;

		case CR_EQ_BIT:
			AND(XA, XA, 32, 31, true); // Clear lower 32bits
			break;

		case CR_GT_BIT:
			AND(XA, XA, 0, 62, true); // XA & ~(1<<63)
			break;

		case CR_LT_BIT:
			ORR(XA, XA, 64 - 62, 0, true); // XA | 1<<62
			break;
		}

		ORR(XA, XA, 32, 0, true); // XA | 1<<32

		STR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);
		gpr.Unlock(WA);
		return;
	}

	ARM64Reg WA = gpr.GetReg();
	ARM64Reg XA = EncodeRegTo64(WA);
	ARM64Reg WB = gpr.GetReg();
	ARM64Reg XB = EncodeRegTo64(WB);

	// creqv or crnand or crnor
	bool negateA = inst.SUBOP10 == 289 || inst.SUBOP10 == 225 || inst.SUBOP10 == 33;
	// crandc or crorc or crnand or crnor
	bool negateB = inst.SUBOP10 == 129 || inst.SUBOP10 == 417 || inst.SUBOP10 == 225 || inst.SUBOP10 == 33;

	// GetCRFieldBit
	for (int i = 0; i < 2; i++)
	{
		int field = i ? inst.CRBB >> 2 : inst.CRBA >> 2;
		int bit = i ? 3 - (inst.CRBB & 3) : 3 - (inst.CRBA & 3);
		ARM64Reg out = i ? XB : XA;
		bool negate = i ? negateB : negateA;

		ARM64Reg WC = gpr.GetReg();
		ARM64Reg XC = EncodeRegTo64(WC);
		LDR(INDEX_UNSIGNED, XC, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);
		switch (bit)
		{
		case CR_SO_BIT:  // check bit 61 set
			UBFX(out, XC, 61, 1);
			if (negate)
				EOR(out, out, 0, 0, true); // XC ^ 1
			break;

		case CR_EQ_BIT:  // check bits 31-0 == 0
			CMP(WC, WZR);
			CSET(out, negate ? CC_NEQ : CC_EQ);
			break;

		case CR_GT_BIT:  // check val > 0
			CMP(XC, ZR);
			CSET(out, negate ? CC_LE : CC_GT);
			break;

		case CR_LT_BIT:  // check bit 62 set
			UBFX(out, XC, 62, 1);
			if (negate)
				EOR(out, out, 0, 0, true); // XC ^ 1
			break;

		default:
			_assert_msg_(DYNA_REC, false, "Invalid CR bit");
		}
		gpr.Unlock(WC);
	}


	// Compute combined bit
	switch (inst.SUBOP10)
	{
	case 33:  // crnor: ~(A || B) == (~A && ~B)
	case 129: // crandc: A && ~B
	case 257: // crand:  A && B
		AND(XA, XA, XB);
		break;

	case 193: // crxor: A ^ B
	case 289: // creqv: ~(A ^ B) = ~A ^ B
		EOR(XA, XA, XB);
		break;

	case 225: // crnand: ~(A && B) == (~A || ~B)
	case 417: // crorc: A || ~B
	case 449: // cror:  A || B
		ORR(XA, XA, XB);
		break;
	}

	// Store result bit in CRBD
	int field = inst.CRBD >> 2;
	int bit = 3 - (inst.CRBD & 3);

	LDR(INDEX_UNSIGNED, XB, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);

	// Gross but necessary; if the input is totally zero and we set SO or LT,
	// or even just add the (1<<32), GT will suddenly end up set without us
	// intending to. This can break actual games, so fix it up.
	if (bit != CR_GT_BIT)
	{
		ARM64Reg WC = gpr.GetReg();
		ARM64Reg XC = EncodeRegTo64(WC);
		ORR(XC, XB, 64 - 63, 0, true); // XB | 1<<63
		CMP(XB, ZR);
		CSEL(XB, XB, XC, CC_NEQ);
		gpr.Unlock(WC);
	}

	switch (bit)
	{
	case CR_SO_BIT:  // set bit 61 to input
		BFI(XB, XA, 61, 1);
		break;

	case CR_EQ_BIT:  // clear low 32 bits, set bit 0 to !input
		AND(XB, XB, 32, 31, true); // Clear lower 32bits
		EOR(XA, XA, 0, 0); // XA ^ 1<<0
		ORR(XB, XB, XA);
		break;

	case CR_GT_BIT:  // set bit 63 to !input
		EOR(XA, XA, 0, 0); // XA ^ 1<<0
		BFI(XB, XA, 63, 1);
		break;

	case CR_LT_BIT:  // set bit 62 to input
		BFI(XB, XA, 62, 1);
		break;
	}

	ORR(XA, XA, 32, 0, true); // XA | 1<<32
	STR(INDEX_UNSIGNED, XB, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * field);

	gpr.Unlock(WA);
	gpr.Unlock(WB);
}

void JitArm64::mfcr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	gpr.Lock(W0, W1, W2, W30);
	BL(GetAsmRoutines()->mfcr);
	gpr.Unlock(W1, W2, W30);

	gpr.BindToRegister(inst.RD, false);
	MOV(gpr.R(inst.RD), W0);

	gpr.Unlock(W0);
}

void JitArm64::mtcrf(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITSystemRegistersOff);

	u32 crm = inst.CRM;
	if (crm != 0)
	{
		ARM64Reg RS = gpr.R(inst.RS);
		ARM64Reg WA = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		ARM64Reg WB = gpr.GetReg();
		ARM64Reg XB = EncodeRegTo64(WB);
		MOVI2R(XB, (u64)m_crTable);
		for (int i = 0; i < 8; ++i)
		{
			if ((crm & (0x80 >> i)) != 0)
			{
				if (i != 7)
					LSR(WA, RS, 28 - i * 4);
				if (i != 0)
				{
					if (i != 7)
						UBFX(WA, WA, 0, 4);
					else
						UBFX(WA, RS, 0, 4);
				}

				LDR(XA, XB, ArithOption(XA, true));
				STR(INDEX_UNSIGNED, XA, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * i);
			}
		}
		gpr.Unlock(WA, WB);
	}
}

