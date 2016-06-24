// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Arm64Emitter.h"
#include "Common/BitSet.h"
#include "Common/CommonTypes.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/DSP.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/MMIO.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCTables.h"
#include "Core/PowerPC/JitArm64/Jit.h"
#include "Core/PowerPC/JitArm64/Jit_Util.h"
#include "Core/PowerPC/JitArm64/JitArm64_RegCache.h"

using namespace Arm64Gen;

void JitArm64::SafeLoadToReg(u32 dest, s32 addr, s32 offsetReg, u32 flags, s32 offset, bool update)
{
	// We want to make sure to not get LR as a temp register
	gpr.Lock(W0, W30);

	gpr.BindToRegister(dest, dest == (u32)addr || dest == (u32)offsetReg);
	ARM64Reg dest_reg = gpr.R(dest);
	ARM64Reg up_reg = INVALID_REG;
	ARM64Reg off_reg = INVALID_REG;

	if (addr != -1 && !gpr.IsImm(addr))
		up_reg = gpr.R(addr);

	if (offsetReg != -1 && !gpr.IsImm(offsetReg))
		off_reg = gpr.R(offsetReg);

	BitSet32 regs_in_use = gpr.GetCallerSavedUsed();
	BitSet32 fprs_in_use = fpr.GetCallerSavedUsed();
	regs_in_use[W0] = 0;
	regs_in_use[dest_reg] = 0;

	ARM64Reg addr_reg = W0;
	u32 imm_addr = 0;
	bool is_immediate = false;

	if (offsetReg == -1)
	{
		if (addr != -1)
		{
			if (gpr.IsImm(addr))
			{
				is_immediate = true;
				imm_addr = gpr.GetImm(addr) + offset;
			}
			else
			{
				if (offset >= 0 && offset < 4096)
				{
					ADD(addr_reg, up_reg, offset);
				}
				else if (offset < 0 && offset > -4096)
				{
					SUB(addr_reg, up_reg, std::abs(offset));
				}
				else
				{
					MOVI2R(addr_reg, offset);
					ADD(addr_reg, addr_reg, up_reg);
				}
			}
		}
		else
		{
			is_immediate = true;
			imm_addr = offset;
		}
	}
	else
	{
		if (addr != -1)
		{
			if (gpr.IsImm(addr) && gpr.IsImm(offsetReg))
			{
				is_immediate = true;
				imm_addr = gpr.GetImm(addr) + gpr.GetImm(offsetReg);
			}
			else if (gpr.IsImm(addr) && !gpr.IsImm(offsetReg))
			{
				u32 reg_offset = gpr.GetImm(addr);
				if (reg_offset < 4096)
				{
					ADD(addr_reg, off_reg, reg_offset);
				}
				else
				{
					MOVI2R(addr_reg, gpr.GetImm(addr));
					ADD(addr_reg, addr_reg, off_reg);
				}
			}
			else if (!gpr.IsImm(addr) && gpr.IsImm(offsetReg))
			{
				u32 reg_offset = gpr.GetImm(offsetReg);
				if (reg_offset < 4096)
				{
					ADD(addr_reg, up_reg, reg_offset);
				}
				else
				{
					MOVI2R(addr_reg, gpr.GetImm(offsetReg));
					ADD(addr_reg, addr_reg, up_reg);
				}
			}
			else
			{
				ADD(addr_reg, up_reg, off_reg);
			}
		}
		else
		{
			if (gpr.IsImm(offsetReg))
			{
				is_immediate = true;
				imm_addr = gpr.GetImm(offsetReg);
			}
			else
			{
				MOV(addr_reg, off_reg);
			}
		}
	}

	ARM64Reg XA = EncodeRegTo64(addr_reg);

	if (is_immediate)
		MOVI2R(XA, imm_addr);

	if (update)
	{
		gpr.BindToRegister(addr, false);
		MOV(gpr.R(addr), addr_reg);
	}

	u32 access_size = BackPatchInfo::GetFlagSize(flags);
	u32 mmio_address = 0;
	if (is_immediate)
		mmio_address = PowerPC::IsOptimizableMMIOAccess(imm_addr, access_size);

	if (is_immediate && PowerPC::IsOptimizableRAMAddress(imm_addr))
	{
		EmitBackpatchRoutine(flags, true, false, dest_reg, XA, BitSet32(0), BitSet32(0));
	}
	else if (mmio_address)
	{
		MMIOLoadToReg(Memory::mmio_mapping.get(), this,
		              regs_in_use, fprs_in_use, dest_reg,
		              mmio_address, flags);
	}
	else
	{
		EmitBackpatchRoutine(flags,
			jo.fastmem,
			jo.fastmem,
			dest_reg, XA,
			regs_in_use, fprs_in_use);
	}

	gpr.Unlock(W0, W30);
}

void JitArm64::SafeStoreFromReg(s32 dest, u32 value, s32 regOffset, u32 flags, s32 offset)
{
	// We want to make sure to not get LR as a temp register
	gpr.Lock(W0, W1, W30);

	ARM64Reg RS = gpr.R(value);

	ARM64Reg reg_dest = INVALID_REG;
	ARM64Reg reg_off = INVALID_REG;

	if (regOffset != -1 && !gpr.IsImm(regOffset))
		reg_off = gpr.R(regOffset);
	if (dest != -1 && !gpr.IsImm(dest))
		reg_dest = gpr.R(dest);

	BitSet32 regs_in_use = gpr.GetCallerSavedUsed();
	BitSet32 fprs_in_use = fpr.GetCallerSavedUsed();
	regs_in_use[W0] = 0;
	regs_in_use[W1] = 0;

	ARM64Reg addr_reg = W1;

	u32 imm_addr = 0;
	bool is_immediate = false;

	if (regOffset == -1)
	{
		if (dest != -1)
		{
			if (gpr.IsImm(dest))
			{
				is_immediate = true;
				imm_addr = gpr.GetImm(dest) + offset;
			}
			else
			{
				if (offset >= 0 && offset < 4096)
				{
					ADD(addr_reg, reg_dest, offset);
				}
				else if (offset < 0 && offset > -4096)
				{
					SUB(addr_reg, reg_dest, std::abs(offset));
				}
				else
				{
					MOVI2R(addr_reg, offset);
					ADD(addr_reg, addr_reg, reg_dest);
				}
			}
		}
		else
		{
			is_immediate = true;
			imm_addr = offset;
		}
	}
	else
	{
		if (dest != -1)
		{
			if (gpr.IsImm(dest) && gpr.IsImm(regOffset))
			{
				is_immediate = true;
				imm_addr = gpr.GetImm(dest) + gpr.GetImm(regOffset);
			}
			else if (gpr.IsImm(dest) && !gpr.IsImm(regOffset))
			{
				u32 reg_offset = gpr.GetImm(dest);
				if (reg_offset < 4096)
				{
					ADD(addr_reg, reg_off, reg_offset);
				}
				else
				{
					MOVI2R(addr_reg, reg_offset);
					ADD(addr_reg, addr_reg, reg_off);
				}
			}
			else if (!gpr.IsImm(dest) && gpr.IsImm(regOffset))
			{
				u32 reg_offset = gpr.GetImm(regOffset);
				if (reg_offset < 4096)
				{
					ADD(addr_reg, reg_dest, reg_offset);
				}
				else
				{
					MOVI2R(addr_reg, gpr.GetImm(regOffset));
					ADD(addr_reg, addr_reg, reg_dest);
				}
			}
			else
			{
				ADD(addr_reg, reg_dest, reg_off);
			}
		}
		else
		{
			if (gpr.IsImm(regOffset))
			{
				is_immediate = true;
				imm_addr = gpr.GetImm(regOffset);
			}
			else
			{
				MOV(addr_reg, reg_off);
			}
		}
	}

	ARM64Reg XA = EncodeRegTo64(addr_reg);

	u32 access_size = BackPatchInfo::GetFlagSize(flags);
	u32 mmio_address = 0;
	if (is_immediate)
		mmio_address = PowerPC::IsOptimizableMMIOAccess(imm_addr, access_size);

	if (is_immediate && jo.optimizeGatherPipe && PowerPC::IsOptimizableGatherPipeWrite(imm_addr))
	{
		ARM64Reg WA = INVALID_REG;
		int accessSize;
		if (flags & BackPatchInfo::FLAG_SIZE_32)
			accessSize = 32;
		else if (flags & BackPatchInfo::FLAG_SIZE_16)
			accessSize = 16;
		else
			accessSize = 8;

		if (accessSize != 8)
			WA = gpr.GetReg();

		u64 base_ptr = std::min((u64)&GPFifo::m_gatherPipeCount, (u64)&GPFifo::m_gatherPipe);
		u32 count_off = (u64)&GPFifo::m_gatherPipeCount - base_ptr;
		u32 pipe_off = (u64)&GPFifo::m_gatherPipe - base_ptr;

		MOVI2R(X30, base_ptr);

		if (pipe_off)
			ADD(X1, X30, pipe_off);

		LDR(INDEX_UNSIGNED, W0, X30, count_off);
		if (accessSize == 32)
		{
			REV32(WA, RS);
			if (pipe_off)
				STR(WA, X1, ArithOption(X0));
			else
				STR(WA, X30, ArithOption(X0));
		}
		else if (accessSize == 16)
		{
			REV16(WA, RS);
			if (pipe_off)
				STRH(WA, X1, ArithOption(X0));
			else
				STRH(WA, X30, ArithOption(X0));
		}
		else
		{
			if (pipe_off)
				STRB(RS, X1, ArithOption(X0));
			else
				STRB(RS, X30, ArithOption(X0));

		}
		ADD(W0, W0, accessSize >> 3);
		STR(INDEX_UNSIGNED, W0, X30, count_off);
		js.fifoBytesThisBlock += accessSize >> 3;

		if (accessSize != 8)
			gpr.Unlock(WA);
	}
	else if (is_immediate && PowerPC::IsOptimizableRAMAddress(imm_addr))
	{
		MOVI2R(XA, imm_addr);
		EmitBackpatchRoutine(flags, true, false, RS, XA, BitSet32(0), BitSet32(0));
	}
	else if (mmio_address && !(flags & BackPatchInfo::FLAG_REVERSE))
	{
		MMIOWriteRegToAddr(Memory::mmio_mapping.get(), this,
		                   regs_in_use, fprs_in_use, RS,
		                   mmio_address, flags);
	}
	else
	{
		if (is_immediate)
			MOVI2R(XA, imm_addr);

		EmitBackpatchRoutine(flags,
			jo.fastmem,
			jo.fastmem,
			RS, XA,
			regs_in_use,
			fprs_in_use);
	}

	gpr.Unlock(W0, W1, W30);
}

void JitArm64::lXX(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);
	FALLBACK_IF(jo.memcheck);

	u32 a = inst.RA, b = inst.RB, d = inst.RD;
	s32 offset = inst.SIMM_16;
	s32 offsetReg = -1;
	u32 flags = BackPatchInfo::FLAG_LOAD;
	bool update = false;

	switch (inst.OPCD)
	{
		case 31:
			offsetReg = b;
			switch (inst.SUBOP10)
			{
				case 55: // lwzux
					update = true;
				case 23: // lwzx
					flags |= BackPatchInfo::FLAG_SIZE_32;
				break;
				case 119: //lbzux
					update = true;
				case 87: // lbzx
					flags |= BackPatchInfo::FLAG_SIZE_8;
				break;
				case 311: // lhzux
					update = true;
				case 279: // lhzx
					flags |= BackPatchInfo::FLAG_SIZE_16;
				break;
				case 375: // lhaux
					update = true;
				case 343: // lhax
					flags |= BackPatchInfo::FLAG_EXTEND |
					         BackPatchInfo::FLAG_SIZE_16;
				break;
				case 534: // lwbrx
					flags |= BackPatchInfo::FLAG_REVERSE |
					         BackPatchInfo::FLAG_SIZE_32;
				break;
				case 790: // lhbrx
					flags |= BackPatchInfo::FLAG_REVERSE |
					         BackPatchInfo::FLAG_SIZE_16;
				break;
			}
		break;
		case 33: // lwzu
			update = true;
		case 32: // lwz
			flags |= BackPatchInfo::FLAG_SIZE_32;
		break;
		case 35: // lbzu
			update = true;
		case 34: // lbz
			flags |= BackPatchInfo::FLAG_SIZE_8;
		break;
		case 41: // lhzu
			update = true;
		case 40: // lhz
			flags |= BackPatchInfo::FLAG_SIZE_16;
		break;
		case 43: // lhau
			update = true;
		case 42: // lha
			flags |= BackPatchInfo::FLAG_EXTEND |
			        BackPatchInfo::FLAG_SIZE_16;
		break;
	}

	SafeLoadToReg(d, update ? a : (a ? a : -1), offsetReg, flags, offset, update);

	// LWZ idle skipping
	if (SConfig::GetInstance().bSkipIdle &&
	    inst.OPCD == 32 && MergeAllowedNextInstructions(2) &&
	    (inst.hex & 0xFFFF0000) == 0x800D0000 && // lwz r0, XXXX(r13)
	    (js.op[1].inst.hex == 0x28000000 ||
	    (SConfig::GetInstance().bWii && js.op[1].inst.hex == 0x2C000000)) && // cmpXwi r0,0
	    js.op[2].inst.hex == 0x4182fff8) // beq -8
	{
		// if it's still 0, we can wait until the next event
		FixupBranch noIdle = CBNZ(gpr.R(d));

		FixupBranch far = B();
		SwitchToFarCode();
		SetJumpTarget(far);

		gpr.Flush(FLUSH_MAINTAIN_STATE);
		fpr.Flush(FLUSH_MAINTAIN_STATE);

		ARM64Reg WA = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		MOVI2R(XA, (u64)&CoreTiming::Idle);
		BLR(XA);
		gpr.Unlock(WA);

		WriteExceptionExit(js.compilerPC);

		SwitchToNearCode();

		SetJumpTarget(noIdle);
	}
}

void JitArm64::stX(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);
	FALLBACK_IF(jo.memcheck);

	u32 a = inst.RA, b = inst.RB, s = inst.RS;
	s32 offset = inst.SIMM_16;
	s32 regOffset = -1;
	u32 flags = BackPatchInfo::FLAG_STORE;
	bool update = false;
	switch (inst.OPCD)
	{
		case 31:
			switch (inst.SUBOP10)
			{
				case 183: // stwux
					update = true;
				case 151: // stwx
					flags |= BackPatchInfo::FLAG_SIZE_32;
					regOffset = b;
				break;
				case 247: // stbux
					update = true;
				case 215: // stbx
					flags |= BackPatchInfo::FLAG_SIZE_8;
					regOffset = b;
				break;
				case 439: // sthux
					update = true;
				case 407: // sthx
					flags |= BackPatchInfo::FLAG_SIZE_16;
					regOffset = b;
				break;
			}
		break;
		case 37: // stwu
			update = true;
		case 36: // stw
			flags |= BackPatchInfo::FLAG_SIZE_32;
		break;
		case 39: // stbu
			update = true;
		case 38: // stb
			flags |= BackPatchInfo::FLAG_SIZE_8;
		break;
		case 45: // sthu
			update = true;
		case 44: // sth
			flags |= BackPatchInfo::FLAG_SIZE_16;
		break;

	}

	SafeStoreFromReg(update ? a : (a ? a : -1), s, regOffset, flags, offset);

	if (update)
	{
		gpr.BindToRegister(a, false);

		ARM64Reg WA = gpr.GetReg();
		ARM64Reg RB;
		ARM64Reg RA = gpr.R(a);
		if (regOffset != -1)
			RB = gpr.R(regOffset);
		if (regOffset == -1)
		{
			MOVI2R(WA, offset);
			ADD(RA, RA, WA);
		}
		else
		{
			ADD(RA, RA, RB);
		}
		gpr.Unlock(WA);
	}
}

void JitArm64::lmw(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);
	FALLBACK_IF(!jo.fastmem || jo.memcheck);

	u32 a = inst.RA;

	ARM64Reg WA = gpr.GetReg();
	ARM64Reg XA = EncodeRegTo64(WA);
	if (a)
	{
		bool add = inst.SIMM_16 >= 0;
		u16 off = std::abs(inst.SIMM_16);
		if (off < 4096)
		{
			if (add)
				ADD(WA, gpr.R(a), off);
			else
				SUB(WA, gpr.R(a), off);
		}
		else
		{
			u16 remaining = off >> 12;
			if (add)
			{
				ADD(WA, gpr.R(a), off & 0xFFF);
				ADD(WA, WA, remaining, true);
			}
			else
			{
				SUB(WA, gpr.R(a), off & 0xFFF);
				SUB(WA, WA, remaining, true);
			}
		}
	}
	else
	{
		MOVI2R(WA, (u32)(s32)(s16)inst.SIMM_16);
	}

	ADD(XA, XA, MEM_REG);

	for (int i = inst.RD; i < 32; i++)
	{
		int remaining = 32 - i;
		if (remaining >= 4)
		{
			gpr.BindToRegister(i + 3, false);
			gpr.BindToRegister(i + 2, false);
			gpr.BindToRegister(i + 1, false);
			gpr.BindToRegister(i, false);
			ARM64Reg RX4 = gpr.R(i + 3);
			ARM64Reg RX3 = gpr.R(i + 2);
			ARM64Reg RX2 = gpr.R(i + 1);
			ARM64Reg RX1 = gpr.R(i);
			LDP(INDEX_POST, EncodeRegTo64(RX1), EncodeRegTo64(RX3), XA, 16);
			REV32(EncodeRegTo64(RX1), EncodeRegTo64(RX1));
			REV32(EncodeRegTo64(RX3), EncodeRegTo64(RX3));
			ORR(EncodeRegTo64(RX2), ZR, EncodeRegTo64(RX1), ArithOption(EncodeRegTo64(RX1), ST_LSR, 32));
			ORR(EncodeRegTo64(RX4), ZR, EncodeRegTo64(RX3), ArithOption(EncodeRegTo64(RX3), ST_LSR, 32));
			i+=3;
		}
		else if (remaining >= 2)
		{
			gpr.BindToRegister(i + 1, false);
			gpr.BindToRegister(i, false);
			ARM64Reg RX2 = gpr.R(i + 1);
			ARM64Reg RX1 = gpr.R(i);
			LDP(INDEX_POST, RX1, RX2, XA, 8);
			REV32(RX1, RX1);
			REV32(RX2, RX2);
			++i;
		}
		else
		{
			gpr.BindToRegister(i, false);
			ARM64Reg RX = gpr.R(i);
			LDR(INDEX_POST, RX, XA, 4);
			REV32(RX, RX);
		}
	}

	gpr.Unlock(WA);
}

void JitArm64::stmw(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);
	FALLBACK_IF(!jo.fastmem || jo.memcheck);

	u32 a = inst.RA;

	ARM64Reg WA = gpr.GetReg();
	ARM64Reg XA = EncodeRegTo64(WA);
	ARM64Reg WB = gpr.GetReg();

	if (a)
	{
		bool add = inst.SIMM_16 >= 0;
		u16 off = std::abs(inst.SIMM_16);
		if (off < 4096)
		{
			if (add)
				ADD(WA, gpr.R(a), off);
			else
				SUB(WA, gpr.R(a), off);
		}
		else
		{
			u16 remaining = off >> 12;
			if (add)
			{
				ADD(WA, gpr.R(a), off & 0xFFF);
				ADD(WA, WA, remaining, true);
			}
			else
			{
				SUB(WA, gpr.R(a), off & 0xFFF);
				SUB(WA, WA, remaining, true);
			}
		}
	}
	else
	{
		MOVI2R(WA, (u32)(s32)(s16)inst.SIMM_16);
	}

	u8* base = UReg_MSR(MSR).DR ? Memory::logical_base : Memory::physical_base;
	MOVK(XA, ((u64)base >> 32) & 0xFFFF, SHIFT_32);

	for (int i = inst.RD; i < 32; i++)
	{
		ARM64Reg RX = gpr.R(i);
		REV32(WB, RX);
		STR(INDEX_UNSIGNED, WB, XA, (i - inst.RD) * 4);
	}

	gpr.Unlock(WA, WB);
}

void JitArm64::dcbx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);

	gpr.Lock(W30);

	ARM64Reg addr = gpr.GetReg();
	ARM64Reg value = gpr.GetReg();
	ARM64Reg WA = W30;

	u32 a = inst.RA, b = inst.RB;

	if (a)
		ADD(addr, gpr.R(a), gpr.R(b));
	else
		MOV(addr, gpr.R(b));

	// Check whether a JIT cache line needs to be invalidated.
	AND(value, addr, 32 - 10, 28 - 10); // upper three bits and last 10 bit are masked for the bitset of cachelines, 0x1ffffc00
	LSR(value, value, 5 + 5); // >> 5 for cache line size, >> 5 for width of bitset
	MOVI2R(EncodeRegTo64(WA), (u64)jit->GetBlockCache()->GetBlockBitSet());
	LDR(value, EncodeRegTo64(WA), ArithOption(EncodeRegTo64(value), true));

	LSR(addr, addr, 5); // mask sizeof cacheline, & 0x1f is the position within the bitset

	LSR(value, value, addr); // move current bit to bit 0

	FixupBranch bit_not_set = TBZ(value, 0);
	FixupBranch far = B();
	SwitchToFarCode();
	SetJumpTarget(far);

	BitSet32 gprs_to_push = gpr.GetCallerSavedUsed();
	BitSet32 fprs_to_push = fpr.GetCallerSavedUsed();

	ABI_PushRegisters(gprs_to_push);
	m_float_emit.ABI_PushRegisters(fprs_to_push, X30);

	LSL(W0, addr, 5);
	MOVI2R(X1, 32);
	MOVI2R(X2, 0);
	MOVI2R(X3, (u64)(void*)JitInterface::InvalidateICache);
	BLR(X3);

	m_float_emit.ABI_PopRegisters(fprs_to_push, X30);
	ABI_PopRegisters(gprs_to_push);

	FixupBranch near = B();
	SwitchToNearCode();
	SetJumpTarget(bit_not_set);
	SetJumpTarget(near);

	// dcbi
	if (inst.SUBOP10 == 470)
	{
		// Flush DSP DMA if DMAState bit is set
		MOVI2R(EncodeRegTo64(WA), (u64)&DSP::g_dspState);
		LDRH(INDEX_UNSIGNED, WA, EncodeRegTo64(WA), 0);

		bit_not_set = TBZ(WA, 9);
		far = B();
		SwitchToFarCode();
		SetJumpTarget(far);

		ABI_PushRegisters(gprs_to_push);
		m_float_emit.ABI_PushRegisters(fprs_to_push, X30);

		LSL(W0, addr, 5);
		MOVI2R(X1, (u64)DSP::FlushInstantDMA);
		BLR(X1);

		m_float_emit.ABI_PopRegisters(fprs_to_push, X30);
		ABI_PopRegisters(gprs_to_push);

		near = B();
		SwitchToNearCode();
		SetJumpTarget(near);
		SetJumpTarget(bit_not_set);
	}

	gpr.Unlock(addr, value, W30);
}

void JitArm64::dcbt(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);

	// Prefetch. Since we don't emulate the data cache, we don't need to do anything.

	// If a dcbst follows a dcbt, it probably isn't a case of dynamic code
	// modification, so don't bother invalidating the jit block cache.
	// This is important because invalidating the block cache when we don't
	// need to is terrible for performance.
	// (Invalidating the jit block cache on dcbst is a heuristic.)
	if (MergeAllowedNextInstructions(1) &&
	    js.op[1].inst.OPCD == 31 && js.op[1].inst.SUBOP10 == 54 &&
	    js.op[1].inst.RA == inst.RA && js.op[1].inst.RB == inst.RB)
	{
		js.skipInstructions = 1;
	}
}

void JitArm64::dcbz(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITLoadStoreOff);
	FALLBACK_IF(jo.memcheck);

	int a = inst.RA, b = inst.RB;

	u32 mem_mask = Memory::ADDR_MASK_HW_ACCESS;

	// The following masks the region used by the GC/Wii virtual memory lib
	mem_mask |= Memory::ADDR_MASK_MEM1;

	gpr.Lock(W0);

	ARM64Reg addr_reg = W0;

	if (a)
	{
		bool is_imm_a, is_imm_b;
		is_imm_a = gpr.IsImm(a);
		is_imm_b = gpr.IsImm(b);
		if (is_imm_a && is_imm_b)
		{
			// full imm_addr
			u32 imm_addr = gpr.GetImm(b) + gpr.GetImm(a);
			MOVI2R(addr_reg, imm_addr);
		}
		else if (is_imm_a || is_imm_b)
		{
			// Only one register is an immediate
			ARM64Reg base = is_imm_a ? gpr.R(b) : gpr.R(a);
			u32 imm_offset = is_imm_a ? gpr.GetImm(a) : gpr.GetImm(b);
			if (imm_offset < 4096)
			{
				ADD(addr_reg, base, imm_offset);
			}
			else
			{
				MOVI2R(addr_reg, imm_offset);
				ADD(addr_reg, addr_reg, base);
			}
		}
		else
		{
			// Both are registers
			ADD(addr_reg, gpr.R(a), gpr.R(b));
		}
	}
	else
	{
		// RA isn't used, only RB
		if (gpr.IsImm(b))
		{
			u32 imm_addr = gpr.GetImm(b);
			MOVI2R(addr_reg, imm_addr);
		}
		else
		{
			MOV(addr_reg, gpr.R(b));
		}
	}

	// We don't care about being /too/ terribly efficient here
	// As long as we aren't falling back to interpreter we're winning a lot

	BitSet32 gprs_to_push = gpr.GetCallerSavedUsed();
	BitSet32 fprs_to_push = fpr.GetCallerSavedUsed();
	gprs_to_push[W0] = 0;

	EmitBackpatchRoutine(BackPatchInfo::FLAG_ZERO_256, true, true, W0, EncodeRegTo64(addr_reg), gprs_to_push, fprs_to_push);

	gpr.Unlock(W0);

}
