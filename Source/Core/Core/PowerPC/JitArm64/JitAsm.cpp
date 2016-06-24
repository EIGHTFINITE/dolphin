// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Arm64Emitter.h"
#include "Common/CommonTypes.h"
#include "Common/JitRegister.h"
#include "Common/MathUtil.h"
#include "Core/CoreTiming.h"
#include "Core/HW/CPU.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/JitArm64/Jit.h"
#include "Core/PowerPC/JitCommon/JitAsmCommon.h"
#include "Core/PowerPC/JitCommon/JitCache.h"

using namespace Arm64Gen;

void JitArm64::GenerateAsm()
{
	// This value is all of the callee saved registers that we are required to save.
	// According to the AACPS64 we need to save R19 ~ R30.
	const u32 ALL_CALLEE_SAVED = 0x7FF80000;
	BitSet32 regs_to_save(ALL_CALLEE_SAVED);
	enterCode = GetCodePtr();

	ABI_PushRegisters(regs_to_save);

	MOVI2R(PPC_REG, (u64)&PowerPC::ppcState);
	MOVI2R(MEM_REG, (u64)Memory::logical_base);

	// Load the current PC into DISPATCHER_PC
	LDR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));

	FixupBranch to_dispatcher = B();

	// If we align the dispatcher to a page then we can load its location with one ADRP instruction
	AlignCodePage();
	dispatcher = GetCodePtr();
	WARN_LOG(DYNA_REC, "Dispatcher is %p\n", dispatcher);

	SetJumpTarget(to_dispatcher);

	// Downcount Check
	// The result of slice decrementation should be in flags if somebody jumped here
	// IMPORTANT - We jump on negative, not carry!!!
	FixupBranch bail = B(CC_MI);

		dispatcherNoCheck = GetCodePtr();

		FixupBranch exram, vmem, not_exram, not_vmem;
		ARM64Reg pc_masked = W25;
		ARM64Reg cache_base = X27;

		// VMEM
		not_vmem = TBZ(DISPATCHER_PC, IntLog2(JIT_ICACHE_VMEM_BIT));
		ANDI2R(pc_masked, DISPATCHER_PC, JIT_ICACHE_MASK);
		MOVI2R(cache_base, (u64)jit->GetBlockCache()->iCacheVMEM.data());
		vmem = B();
		SetJumpTarget(not_vmem);

		if (SConfig::GetInstance().bWii)
		{
			// Wii EX-RAM
			not_exram = TBZ(DISPATCHER_PC, IntLog2(JIT_ICACHE_EXRAM_BIT));
			ANDI2R(pc_masked, DISPATCHER_PC, JIT_ICACHEEX_MASK);
			MOVI2R(cache_base, (u64)jit->GetBlockCache()->iCacheEx.data());
			exram = B();
			SetJumpTarget(not_exram);
		}

		// Common memory
		ANDI2R(pc_masked, DISPATCHER_PC, JIT_ICACHE_MASK);
		MOVI2R(cache_base, (u64)jit->GetBlockCache()->iCache.data());

		SetJumpTarget(vmem);
		if (SConfig::GetInstance().bWii)
			SetJumpTarget(exram);

		LDR(W27, cache_base, EncodeRegTo64(pc_masked));

		FixupBranch JitBlock = TBNZ(W27, 7); // Test the 7th bit
			// Success, it is our Jitblock.
			MOVI2R(X30, (u64)jit->GetBlockCache()->GetCodePointers());
			UBFM(X27, X27, 61, 60); // Same as X27 << 3
			LDR(X30, X30, X27); // Load the block address in to R14
			BR(X30);
			// No need to jump anywhere after here, the block will go back to dispatcher start

		SetJumpTarget(JitBlock);

		STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));
		MOVI2R(X30, (u64)&::Jit);
		BLR(X30);

		LDR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));

		B(dispatcherNoCheck);

	SetJumpTarget(bail);
	doTiming = GetCodePtr();
		// Write the current PC out to PPCSTATE
		STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));
		STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(npc));

		MOVI2R(X30, (u64)&CoreTiming::Advance);
		BLR(X30);

		// Load the PC back into DISPATCHER_PC (the exception handler might have changed it)
		LDR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));

		// Check the state pointer to see if we are exiting
		// Gets checked on at the end of every slice
		MOVI2R(X0, (u64)CPU::GetStatePtr());
		LDR(INDEX_UNSIGNED, W0, X0, 0);

		CMP(W0, 0);
		FixupBranch Exit = B(CC_NEQ);

	B(dispatcher);

	SetJumpTarget(Exit);
	STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));

	ABI_PopRegisters(regs_to_save);
	RET(X30);

	JitRegister::Register(enterCode, GetCodePtr(), "JIT_Dispatcher");

	GenerateCommonAsm();

	FlushIcache();
}

void JitArm64::GenerateCommonAsm()
{
	// X0 is the scale
	// X1 is address
	// X2 is a temporary on stores
	// X30 is LR
	// Q0 is the return for loads
	//    is the register for stores
	// Q1 is a temporary
	ARM64Reg addr_reg = X1;
	ARM64Reg scale_reg = X0;
	ARM64FloatEmitter float_emit(this);

	const u8* start = GetCodePtr();
	const u8* loadPairedIllegal = GetCodePtr();
		BRK(100);
	const u8* loadPairedFloatTwo = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LD1(32, 1, D0, addr_reg);
		float_emit.REV32(8, D0, D0);
		RET(X30);
	}
	const u8* loadPairedU8Two = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(16, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.UXTL(8, D0, D0);
		float_emit.UXTL(16, D0, D0);
		float_emit.UCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}
	const u8* loadPairedS8Two = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(16, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.SXTL(8, D0, D0);
		float_emit.SXTL(16, D0, D0);
		float_emit.SCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}
	const u8* loadPairedU16Two = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LD1(16, 1, D0, addr_reg);
		float_emit.REV16(8, D0, D0);
		float_emit.UXTL(16, D0, D0);
		float_emit.UCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}
	const u8* loadPairedS16Two = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LD1(16, 1, D0, addr_reg);
		float_emit.REV16(8, D0, D0);
		float_emit.SXTL(16, D0, D0);
		float_emit.SCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}

	const u8* loadPairedFloatOne = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(32, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.REV32(8, D0, D0);
		RET(X30);
	}
	const u8* loadPairedU8One = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(8, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.UXTL(8, D0, D0);
		float_emit.UXTL(16, D0, D0);
		float_emit.UCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}
	const u8* loadPairedS8One = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(8, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.SXTL(8, D0, D0);
		float_emit.SXTL(16, D0, D0);
		float_emit.SCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}
	const u8* loadPairedU16One = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(16, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.REV16(8, D0, D0);
		float_emit.UXTL(16, D0, D0);
		float_emit.UCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}
	const u8* loadPairedS16One = GetCodePtr();
	{
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.LDR(16, INDEX_UNSIGNED, D0, addr_reg, 0);
		float_emit.REV16(8, D0, D0);
		float_emit.SXTL(16, D0, D0);
		float_emit.SCVTF(32, D0, D0);

		MOVI2R(addr_reg, (u64)&m_dequantizeTableS);
		ADD(scale_reg, addr_reg, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
		float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
		float_emit.FMUL(32, D0, D0, D1, 0);
		RET(X30);
	}

	JitRegister::Register(start, GetCodePtr(), "JIT_QuantizedLoad");

	pairedLoadQuantized = reinterpret_cast<const u8**>(const_cast<u8*>(AlignCode16()));
	ReserveCodeSpace(16 * sizeof(u8*));

	pairedLoadQuantized[0] = loadPairedFloatTwo;
	pairedLoadQuantized[1] = loadPairedIllegal;
	pairedLoadQuantized[2] = loadPairedIllegal;
	pairedLoadQuantized[3] = loadPairedIllegal;
	pairedLoadQuantized[4] = loadPairedU8Two;
	pairedLoadQuantized[5] = loadPairedU16Two;
	pairedLoadQuantized[6] = loadPairedS8Two;
	pairedLoadQuantized[7] = loadPairedS16Two;

	pairedLoadQuantized[8] = loadPairedFloatOne;
	pairedLoadQuantized[9] = loadPairedIllegal;
	pairedLoadQuantized[10] = loadPairedIllegal;
	pairedLoadQuantized[11] = loadPairedIllegal;
	pairedLoadQuantized[12] = loadPairedU8One;
	pairedLoadQuantized[13] = loadPairedU16One;
	pairedLoadQuantized[14] = loadPairedS8One;
	pairedLoadQuantized[15] = loadPairedS16One;

	// Stores
	start = GetCodePtr();
	const u8* storePairedIllegal = GetCodePtr();
		BRK(0x101);
	const u8* storePairedFloat;
	const u8* storePairedFloatSlow;
	{
		storePairedFloat = GetCodePtr();
		float_emit.REV32(8, D0, D0);
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(64, Q0, 0, addr_reg, SP);
		RET(X30);

		storePairedFloatSlow = GetCodePtr();
		float_emit.UMOV(64, X0, Q0, 0);
		ORR(X0, SP, X0, ArithOption(X0, ST_ROR, 32));
		MOVI2R(X2, (u64)PowerPC::Write_U64);
		BR(X2);
	}

	const u8* storePairedU8;
	const u8* storePairedU8Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1, 0);

			float_emit.FCVTZU(32, D0, D0);
			float_emit.UQXTN(16, D0, D0);
			float_emit.UQXTN(8, D0, D0);
		};

		storePairedU8 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(16, Q0, 0, addr_reg, SP);
		RET(X30);

		storePairedU8Slow = GetCodePtr();
		emit_quantize();
		float_emit.UMOV(16, W0, Q0, 0);
		REV16(W0, W0);
		MOVI2R(X2, (u64)PowerPC::Write_U16);
		BR(X2);
	}
	const u8* storePairedS8;
	const u8* storePairedS8Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1, 0);

			float_emit.FCVTZS(32, D0, D0);
			float_emit.SQXTN(16, D0, D0);
			float_emit.SQXTN(8, D0, D0);
		};

		storePairedS8 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(16, Q0, 0, addr_reg, SP);
		RET(X30);

		storePairedS8Slow = GetCodePtr();
		emit_quantize();
		float_emit.UMOV(16, W0, Q0, 0);
		REV16(W0, W0);
		MOVI2R(X2, (u64)PowerPC::Write_U16);
		BR(X2);
	}

	const u8* storePairedU16;
	const u8* storePairedU16Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1, 0);

			float_emit.FCVTZU(32, D0, D0);
			float_emit.UQXTN(16, D0, D0);
			float_emit.REV16(8, D0, D0);
		};

		storePairedU16 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(32, Q0, 0, addr_reg, SP);
		RET(X30);

		storePairedU16Slow = GetCodePtr();
		emit_quantize();
		float_emit.REV32(8, D0, D0);
		float_emit.UMOV(32, W0, Q0, 0);
		MOVI2R(X2, (u64)PowerPC::Write_U32);
		BR(X2);
	}
	const u8* storePairedS16; // Used by Viewtiful Joe's intro movie
	const u8* storePairedS16Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1, 0);

			float_emit.FCVTZS(32, D0, D0);
			float_emit.SQXTN(16, D0, D0);
			float_emit.REV16(8, D0, D0);
		};

		storePairedS16 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(32, Q0, 0, addr_reg, SP);
		RET(X30);

		storePairedS16Slow = GetCodePtr();
		emit_quantize();
		float_emit.REV32(8, D0, D0);
		float_emit.UMOV(32, W0, Q0, 0);
		MOVI2R(X2, (u64)PowerPC::Write_U32);
		BR(X2);
	}

	const u8* storeSingleFloat;
	const u8* storeSingleFloatSlow;
	{
		storeSingleFloat = GetCodePtr();
		float_emit.REV32(8, D0, D0);
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.STR(32, INDEX_UNSIGNED, D0, addr_reg, 0);
		RET(X30);

		storeSingleFloatSlow = GetCodePtr();
		float_emit.UMOV(32, W0, Q0, 0);
		MOVI2R(X2, (u64)&PowerPC::Write_U32);
		BR(X2);
	}
	const u8* storeSingleU8;  // Used by MKWii
	const u8* storeSingleU8Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1);

			float_emit.FCVTZU(32, D0, D0);
			float_emit.UQXTN(16, D0, D0);
			float_emit.UQXTN(8, D0, D0);
		};

		storeSingleU8 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(8, Q0, 0, addr_reg);
		RET(X30);

		storeSingleU8Slow = GetCodePtr();
		emit_quantize();
		float_emit.UMOV(8, W0, Q0, 0);
		MOVI2R(X2, (u64)&PowerPC::Write_U8);
		BR(X2);
	}
	const u8* storeSingleS8;
	const u8* storeSingleS8Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1);

			float_emit.FCVTZS(32, D0, D0);
			float_emit.SQXTN(16, D0, D0);
			float_emit.SQXTN(8, D0, D0);
		};

		storeSingleS8 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.ST1(8, Q0, 0, addr_reg);
		RET(X30);

		storeSingleS8Slow = GetCodePtr();
		emit_quantize();
		float_emit.SMOV(8, W0, Q0, 0);
		MOVI2R(X2, (u64)&PowerPC::Write_U8);
		BR(X2);
	}
	const u8* storeSingleU16;  // Used by MKWii
	const u8* storeSingleU16Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1);

			float_emit.FCVTZU(32, D0, D0);
			float_emit.UQXTN(16, D0, D0);
		};

		storeSingleU16 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.REV16(8, D0, D0);
		float_emit.ST1(16, Q0, 0, addr_reg);
		RET(X30);

		storeSingleU16Slow = GetCodePtr();
		emit_quantize();
		float_emit.UMOV(16, W0, Q0, 0);
		MOVI2R(X2, (u64)&PowerPC::Write_U16);
		BR(X2);
	}
	const u8* storeSingleS16;
	const u8* storeSingleS16Slow;
	{
		auto emit_quantize = [this, &float_emit, scale_reg]()
		{
			MOVI2R(X2, (u64)&m_quantizeTableS);
			ADD(scale_reg, X2, scale_reg, ArithOption(scale_reg, ST_LSL, 3));
			float_emit.LDR(32, INDEX_UNSIGNED, D1, scale_reg, 0);
			float_emit.FMUL(32, D0, D0, D1);

			float_emit.FCVTZS(32, D0, D0);
			float_emit.SQXTN(16, D0, D0);
		};

		storeSingleS16 = GetCodePtr();
		emit_quantize();
		MOVK(addr_reg, ((u64)Memory::logical_base >> 32) & 0xFFFF, SHIFT_32);
		float_emit.REV16(8, D0, D0);
		float_emit.ST1(16, Q0, 0, addr_reg);
		RET(X30);

		storeSingleS16Slow = GetCodePtr();
		emit_quantize();
		float_emit.SMOV(16, W0, Q0, 0);
		MOVI2R(X2, (u64)&PowerPC::Write_U16);
		BR(X2);
	}

	JitRegister::Register(start, GetCodePtr(), "JIT_QuantizedStore");

	pairedStoreQuantized = reinterpret_cast<const u8**>(const_cast<u8*>(AlignCode16()));
	ReserveCodeSpace(32 * sizeof(u8*));

	// Fast
	pairedStoreQuantized[0] = storePairedFloat;
	pairedStoreQuantized[1] = storePairedIllegal;
	pairedStoreQuantized[2] = storePairedIllegal;
	pairedStoreQuantized[3] = storePairedIllegal;
	pairedStoreQuantized[4] = storePairedU8;
	pairedStoreQuantized[5] = storePairedU16;
	pairedStoreQuantized[6] = storePairedS8;
	pairedStoreQuantized[7] = storePairedS16;

	pairedStoreQuantized[8] = storeSingleFloat;
	pairedStoreQuantized[9] = storePairedIllegal;
	pairedStoreQuantized[10] = storePairedIllegal;
	pairedStoreQuantized[11] = storePairedIllegal;
	pairedStoreQuantized[12] = storeSingleU8;
	pairedStoreQuantized[13] = storeSingleU16;
	pairedStoreQuantized[14] = storeSingleS8;
	pairedStoreQuantized[15] = storeSingleS16;

	// Slow
	pairedStoreQuantized[16] = storePairedFloatSlow;
	pairedStoreQuantized[17] = storePairedIllegal;
	pairedStoreQuantized[18] = storePairedIllegal;
	pairedStoreQuantized[19] = storePairedIllegal;
	pairedStoreQuantized[20] = storePairedU8Slow;
	pairedStoreQuantized[21] = storePairedU16Slow;
	pairedStoreQuantized[22] = storePairedS8Slow;
	pairedStoreQuantized[23] = storePairedS16Slow;

	pairedStoreQuantized[24] = storeSingleFloatSlow;
	pairedStoreQuantized[25] = storePairedIllegal;
	pairedStoreQuantized[26] = storePairedIllegal;
	pairedStoreQuantized[27] = storePairedIllegal;
	pairedStoreQuantized[28] = storeSingleU8Slow;
	pairedStoreQuantized[29] = storeSingleU16Slow;
	pairedStoreQuantized[30] = storeSingleS8Slow;
	pairedStoreQuantized[31] = storeSingleS16Slow;

	GetAsmRoutines()->mfcr = AlignCode16();
	GenMfcr();
}

void JitArm64::GenMfcr()
{
	// Input: Nothing
	// Returns: W0
	// Clobbers: X1, X2
	const u8* start = GetCodePtr();
	for (int i = 0; i < 8; i++)
	{
		LDR(INDEX_UNSIGNED, X1, PPC_REG, PPCSTATE_OFF(cr_val) + 8 * i);

		// SO
		if (i == 0)
		{
			UBFX(X0, X1, 61, 1);
		}
		else
		{
			ORR(W0, WZR, W0, ArithOption(W0, ST_LSL, 4));
			UBFX(X2, X1, 61, 1);
			ORR(X0, X0, X2);
		}

		// EQ
		ORR(W2, W0, 32 - 1, 0); // W0 | 1<<1
		CMP(W1, WZR);
		CSEL(W0, W2, W0, CC_EQ);

		// GT
		ORR(W2, W0, 32 - 2, 0); // W0 | 1<<2
		CMP(X1, ZR);
		CSEL(W0, W2, W0, CC_GT);

		// LT
		UBFX(X2, X1, 62, 1);
		ORR(W0, W0, W2, ArithOption(W2, ST_LSL, 3));
	}

	RET(X30);
	JitRegister::Register(start, GetCodePtr(), "JIT_Mfcr");
}
