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

#include "Common.h"
#include "ArmEmitter.h"
#include "CPUDetect.h"

#include <assert.h>
#include <stdarg.h>

// For cache flushing on Symbian/iOS/Blackberry
#ifdef __SYMBIAN32__
#include <e32std.h>
#endif

#ifdef IOS
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#endif

#ifdef BLACKBERRY
#include <sys/mman.h>
#endif

namespace ArmGen
{

inline u32 RotR(u32 a, int amount) {
	if (!amount) return a;
	return (a >> amount) | (a << (32 - amount));
}

inline u32 RotL(u32 a, int amount) {
	if (!amount) return a;
	return (a << amount) | (a >> (32 - amount));
}

bool TryMakeOperand2(u32 imm, Operand2 &op2) {
	// Just brute force it.
	for (int i = 0; i < 16; i++) {
		int mask = RotR(0xFF, i * 2);
		if ((imm & mask) == imm) {
			op2 = Operand2((u8)(RotL(imm, i * 2)), (u8)i);
			return true;
		}
	}
	return false;
}

bool TryMakeOperand2_AllowInverse(u32 imm, Operand2 &op2, bool *inverse)
{
	if (!TryMakeOperand2(imm, op2)) {
		*inverse = true;
		return TryMakeOperand2(~imm, op2);
	} else {
		*inverse = false;
		return true;
	}
}

bool TryMakeOperand2_AllowNegation(s32 imm, Operand2 &op2, bool *negated)
{
	if (!TryMakeOperand2(imm, op2)) {
		*negated = true;
		return TryMakeOperand2(-imm, op2);
	} else {
		*negated = false;
		return true;
	}
}

Operand2 AssumeMakeOperand2(u32 imm) {
	Operand2 op2;
	bool result = TryMakeOperand2(imm, op2);
	_dbg_assert_msg_(DYNA_REC, result, "Could not make assumed Operand2.");
	return op2;
}

bool ARMXEmitter::TrySetValue_TwoOp(ARMReg reg, u32 val)
{
	int ops = 0;
	for (int i = 0; i < 16; i++)
	{
		if ((val >> (i*2)) & 0x3)
		{
			ops++;
			i+=3;
		}
	}
	if (ops > 2)
		return false;
	
	bool first = true;
	for (int i = 0; i < 16; i++, val >>=2) {
		if (val & 0x3) {
			first ? MOV(reg, Operand2((u8)val, (u8)((16-i) & 0xF)))
				  : ORR(reg, reg, Operand2((u8)val, (u8)((16-i) & 0xF)));
			first = false;
			i+=3;
			val >>= 6;
		}
	}
	return true;
}

void ARMXEmitter::MOVI2F(ARMReg dest, float val, ARMReg tempReg, bool negate)
{
	union {float f; u32 u;} conv;
	conv.f = negate ? -val : val;
	// Try moving directly first if mantisse is empty
	if (cpu_info.bVFPv3 && ((conv.u & 0x7FFFF) == 0))
	{
		// VFP Encoding for Imms: <7> Not(<6>) Repeat(<6>,5) <5:0> Zeros(19)
		bool bit6 = (conv.u & 0x40000000) == 0x40000000;
		bool canEncode = true;
		for (u32 mask = 0x20000000; mask >= 0x2000000; mask >>= 1)
		{
			if (((conv.u & mask) == mask) == bit6)
				canEncode = false;
		}
		if (canEncode)
		{
			u32 imm8 = (conv.u & 0x80000000) >> 24; // sign bit
			imm8 |= (!bit6 << 6);
			imm8 |= (conv.u & 0x1F80000) >> 19;
			VMOV(dest, IMM(imm8));
			return;
		}
	}
	MOVI2R(tempReg, conv.u);
	VMOV(dest, tempReg);
	// Otherwise, possible to use a literal pool and VLDR directly (+- 1020)
}

void ARMXEmitter::ADDI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	Operand2 op2;
	bool negated;
	if (TryMakeOperand2_AllowNegation(val, op2, &negated)) {
		if (!negated)
			ADD(rd, rs, op2);
		else
			SUB(rd, rs, op2);
	} else {
		MOVI2R(scratch, val);
		ADD(rd, rs, scratch);
	}
}

void ARMXEmitter::ANDI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	Operand2 op2;
	bool inverse;
	if (TryMakeOperand2_AllowInverse(val, op2, &inverse)) {
		if (!inverse) {
			AND(rd, rs, op2);
		} else {
			BIC(rd, rs, op2);
		}
	} else {
		MOVI2R(scratch, val);
		AND(rd, rs, scratch);
	}
}

void ARMXEmitter::CMPI2R(ARMReg rs, u32 val, ARMReg scratch)
{
	Operand2 op2;
	bool negated;
	if (TryMakeOperand2_AllowNegation(val, op2, &negated)) {
		if (!negated)
			CMP(rs, op2);
		else
			CMN(rs, op2);
	} else {
		MOVI2R(scratch, val);
		CMP(rs, scratch);
	}
}

void ARMXEmitter::ORI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	Operand2 op2;
	if (TryMakeOperand2(val, op2)) {
		ORR(rd, rs, op2);
	} else {
		MOVI2R(scratch, val);
		ORR(rd, rs, scratch);
	}
}

void ARMXEmitter::FlushLitPool()
{
	for(std::vector<LiteralPool>::iterator it = currentLitPool.begin(); it != currentLitPool.end(); ++it) {
		// Search for duplicates
		for(std::vector<LiteralPool>::iterator old_it = currentLitPool.begin(); old_it != it; ++old_it) {
			if ((*old_it).val == (*it).val)
				(*it).loc = (*old_it).loc;
		}

		// Write the constant to Literal Pool
		if (!(*it).loc)
		{
			(*it).loc = (s32)code;
			Write32((*it).val);
		}
		s32 offset = (*it).loc - (s32)(*it).ldr_address - 8;

		// Backpatch the LDR
		*(u32*)(*it).ldr_address |= (offset >= 0) << 23 | abs(offset);
	}
	// TODO: Save a copy of previous pools in case they are still in range.
	currentLitPool.clear();
}

void ARMXEmitter::AddNewLit(u32 val)
{
	LiteralPool pool_item;
	pool_item.loc = 0;
	pool_item.val = val;
	pool_item.ldr_address = code;
	currentLitPool.push_back(pool_item);
}

void ARMXEmitter::MOVI2R(ARMReg reg, u32 val, bool optimize)
{
	Operand2 op2;
	bool inverse;

	if (cpu_info.bArmV7 && !optimize)
	{
		// For backpatching on ARMv7
		MOVW(reg, val & 0xFFFF);
		MOVT(reg, val, true);
	}
	else if (TryMakeOperand2_AllowInverse(val, op2, &inverse)) {
		inverse ? MVN(reg, op2) : MOV(reg, op2);
	} else {
		if (cpu_info.bArmV7)
		{
			// Use MOVW+MOVT for ARMv7+
			MOVW(reg, val & 0xFFFF);
			if(val & 0xFFFF0000)
				MOVT(reg, val, true);
		} else if (!TrySetValue_TwoOp(reg,val)) {
			// Use literal pool for ARMv6.
			AddNewLit(val);
			LDR(reg, _PC); // To be backpatched later
		}
	}
}

void ARMXEmitter::QuickCallFunction(ARMReg reg, void *func) {
	if (BLInRange(func)) {
		BL(func);
	} else {
		MOVI2R(reg, (u32)(func));
		BL(reg);
	}
}

void ARMXEmitter::SetCodePtr(u8 *ptr)
{
	code = ptr;
	startcode = code;
	lastCacheFlushEnd = ptr;
}

const u8 *ARMXEmitter::GetCodePtr() const
{
	return code;
}

u8 *ARMXEmitter::GetWritableCodePtr()
{
	return code;
}

void ARMXEmitter::ReserveCodeSpace(u32 bytes)
{
	for (u32 i = 0; i < bytes/4; i++)
		Write32(0xE1200070); //bkpt 0
}

const u8 *ARMXEmitter::AlignCode16()
{
	ReserveCodeSpace((-(s32)code) & 15);
	return code;
}

const u8 *ARMXEmitter::AlignCodePage()
{
	ReserveCodeSpace((-(s32)code) & 4095);
	return code;
}

void ARMXEmitter::FlushIcache()
{
	FlushIcacheSection(lastCacheFlushEnd, code);
	lastCacheFlushEnd = code;
}

void ARMXEmitter::FlushIcacheSection(u8 *start, u8 *end)
{
#ifdef __SYMBIAN32__
	User::IMB_Range(start, end);
#elif defined(BLACKBERRY)
	msync(start, end - start, MS_SYNC | MS_INVALIDATE_ICACHE);
#elif defined(IOS)
	// Header file says this is equivalent to: sys_icache_invalidate(start, end - start);
	sys_cache_control(kCacheFunctionPrepareForExecution, start, end - start);
#elif !defined(_WIN32)
#ifdef __clang__
	__clear_cache(start, end);
#else
	__builtin___clear_cache(start, end);
#endif
#endif
}

void ARMXEmitter::SetCC(CCFlags cond)
{
	condition = cond << 28;
}

void ARMXEmitter::NOP(int count)
{
	for (int i = 0; i < count; i++) {
		Write32(condition | 0x01A00000);
	}
}

void ARMXEmitter::SETEND(bool BE)
{
	//SETEND is non-conditional
	Write32( 0xF1010000 | (BE << 9));
}
void ARMXEmitter::BKPT(u16 arg)
{
	Write32(condition | 0x01200070 | (arg << 4 & 0x000FFF00) | (arg & 0x0000000F));
}
void ARMXEmitter::YIELD()
{
	Write32(condition | 0x0320F001);
}

FixupBranch ARMXEmitter::B()
{
	FixupBranch branch;
	branch.type = 0; // Zero for B
	branch.ptr = code;
	branch.condition = condition;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}
FixupBranch ARMXEmitter::BL()
{
	FixupBranch branch;
	branch.type = 1; // Zero for B
	branch.ptr = code;
	branch.condition = condition;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}

FixupBranch ARMXEmitter::B_CC(CCFlags Cond)
{
	FixupBranch branch;
	branch.type = 0; // Zero for B
	branch.ptr = code;
	branch.condition = Cond << 28;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}
void ARMXEmitter::B_CC(CCFlags Cond, const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _dbg_assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "B_CC out of range (%p calls %p)", code, fnptr);

	Write32((Cond << 28) | 0x0A000000 | ((distance >> 2) & 0x00FFFFFF));
}
FixupBranch ARMXEmitter::BL_CC(CCFlags Cond)
{
	FixupBranch branch;
	branch.type = 1; // Zero for B
	branch.ptr = code;
	branch.condition = Cond << 28;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}
void ARMXEmitter::SetJumpTarget(FixupBranch const &branch)
{
	s32 distance =  (s32(code) - 8)  - (s32)branch.ptr;
     _dbg_assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "SetJumpTarget out of range (%p calls %p)", code,
					 branch.ptr);
	if(branch.type == 0) // B
		*(u32*)branch.ptr = (u32)(branch.condition | (10 << 24) | ((distance >> 2) &
		0x00FFFFFF)); 
	else // BL
		*(u32*)branch.ptr =	(u32)(branch.condition | 0x0B000000 | ((distance >> 2)
		& 0x00FFFFFF));
}
void ARMXEmitter::B (const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _dbg_assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "B out of range (%p calls %p)", code, fnptr);

	Write32(condition | 0x0A000000 | ((distance >> 2) & 0x00FFFFFF));
}

void ARMXEmitter::B(ARMReg src)
{
	Write32(condition | 0x12FFF10 | src);
}

bool ARMXEmitter::BLInRange(const void *fnptr) {
	s32 distance = (s32)fnptr - (s32(code) + 8);
	if (distance <= -33554432 || distance > 33554432)
		return false;
	else
		return true;
}

void ARMXEmitter::BL(const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _dbg_assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "BL out of range (%p calls %p)", code, fnptr);
	Write32(condition | 0x0B000000 | ((distance >> 2) & 0x00FFFFFF));
}
void ARMXEmitter::BL(ARMReg src)
{
	Write32(condition | 0x12FFF30 | src);
}
void ARMXEmitter::PUSH(const int num, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, num);
	for (i=0;i<num;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	Write32(condition | (2349 << 16) | RegList);
}
void ARMXEmitter::POP(const int num, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, num);
	for (i=0;i<num;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	Write32(condition | (2237 << 16) | RegList);
}

void ARMXEmitter::WriteShiftedDataOp(u32 op, bool SetFlags, ARMReg dest, ARMReg src, Operand2 op2)
{
	Write32(condition | (13 << 21) | (SetFlags << 20) | (dest << 12) | op2.Imm5() | (op << 4) | src);
}
void ARMXEmitter::WriteShiftedDataOp(u32 op, bool SetFlags, ARMReg dest, ARMReg src, ARMReg op2)
{
	Write32(condition | (13 << 21) | (SetFlags << 20) | (dest << 12) | (op2 << 8) | (op << 4) | src);
}

// IMM, REG, IMMSREG, RSR 
// -1 for invalid if the instruction doesn't support that
const s32 InstOps[][4] = {{16, 0, 0, 0}, // AND(s)
						  {17, 1, 1, 1}, // EOR(s)
						  {18, 2, 2, 2}, // SUB(s)
						  {19, 3, 3, 3}, // RSB(s)
						  {20, 4, 4, 4}, // ADD(s)
						  {21, 5, 5, 5}, // ADC(s)
						  {22, 6, 6, 6}, // SBC(s)
						  {23, 7, 7, 7}, // RSC(s)
						  {24, 8, 8, 8}, // TST
						  {25, 9, 9, 9}, // TEQ
						  {26, 10, 10, 10}, // CMP
						  {27, 11, 11, 11}, // CMN
						  {28, 12, 12, 12}, // ORR(s)
						  {29, 13, 13, 13}, // MOV(s)
						  {30, 14, 14, 14}, // BIC(s)
						  {31, 15, 15, 15}, // MVN(s)
						  {24, -1, -1, -1}, // MOVW
						  {26, -1, -1, -1}, // MOVT
						 }; 

const char *InstNames[] = { "AND",
							"EOR",
							"SUB",
							"RSB",
							"ADD",
							"ADC",
							"SBC",
							"RSC",
							"TST",
							"TEQ",
							"CMP",
							"CMN",
							"ORR",
							"MOV",
							"BIC",
							"MVN"
						  };

void ARMXEmitter::AND (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(0, Rd, Rn, Rm); }
void ARMXEmitter::ANDS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(0, Rd, Rn, Rm, true); }
void ARMXEmitter::EOR (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(1, Rd, Rn, Rm); }
void ARMXEmitter::EORS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(1, Rd, Rn, Rm, true); }
void ARMXEmitter::SUB (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(2, Rd, Rn, Rm); }
void ARMXEmitter::SUBS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(2, Rd, Rn, Rm, true); }
void ARMXEmitter::RSB (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(3, Rd, Rn, Rm); }
void ARMXEmitter::RSBS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(3, Rd, Rn, Rm, true); }
void ARMXEmitter::ADD (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(4, Rd, Rn, Rm); }
void ARMXEmitter::ADDS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(4, Rd, Rn, Rm, true); }
void ARMXEmitter::ADC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(5, Rd, Rn, Rm); }
void ARMXEmitter::ADCS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(5, Rd, Rn, Rm, true); }
void ARMXEmitter::SBC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(6, Rd, Rn, Rm); }
void ARMXEmitter::SBCS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(6, Rd, Rn, Rm, true); }
void ARMXEmitter::RSC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(7, Rd, Rn, Rm); }
void ARMXEmitter::RSCS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(7, Rd, Rn, Rm, true); }
void ARMXEmitter::TST (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(8, R0, Rn, Rm, true); }
void ARMXEmitter::TEQ (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(9, R0, Rn, Rm, true); }
void ARMXEmitter::CMP (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(10, R0, Rn, Rm, true); }
void ARMXEmitter::CMN (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(11, R0, Rn, Rm, true); }
void ARMXEmitter::ORR (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(12, Rd, Rn, Rm); }
void ARMXEmitter::ORRS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(12, Rd, Rn, Rm, true); }
void ARMXEmitter::MOV (ARMReg Rd,			 Operand2 Rm) { WriteInstruction(13, Rd, R0, Rm); }
void ARMXEmitter::MOVS(ARMReg Rd,			 Operand2 Rm) { WriteInstruction(13, Rd, R0, Rm, true); }
void ARMXEmitter::BIC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(14, Rd, Rn, Rm); }
void ARMXEmitter::BICS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(14, Rd, Rn, Rm, true); }
void ARMXEmitter::MVN (ARMReg Rd,			 Operand2 Rm) { WriteInstruction(15, Rd, R0, Rm); }
void ARMXEmitter::MVNS(ARMReg Rd,			 Operand2 Rm) { WriteInstruction(15, Rd, R0, Rm, true); }
void ARMXEmitter::MOVW(ARMReg Rd,			 Operand2 Rm) { WriteInstruction(16, Rd, R0, Rm); }
void ARMXEmitter::MOVT(ARMReg Rd, Operand2 Rm, bool TopBits) { WriteInstruction(17, Rd, R0, TopBits ? Rm.Value >> 16 : Rm); }

void ARMXEmitter::WriteInstruction (u32 Op, ARMReg Rd, ARMReg Rn, Operand2 Rm, bool SetFlags) // This can get renamed later
{
	s32 op = InstOps[Op][Rm.GetType()]; // Type always decided by last operand
	u32 Data = Rm.GetData();
	if (Rm.GetType() == TYPE_IMM)
	{
		switch (Op)
		{
			// MOV cases that support IMM16
			case 16:
			case 17:
				Data = Rm.Imm16();
			break;
			default:
			break;
		}
	}
	if (op == -1)
		_dbg_assert_msg_(DYNA_REC, false, "%s not yet support %d", InstNames[Op], Rm.GetType()); 
	Write32(condition | (op << 21) | (SetFlags ? (1 << 20) : 0) | Rn << 16 | Rd << 12 | Data);
}

// Data Operations
void ARMXEmitter::WriteSignedMultiply(u32 Op, u32 Op2, u32 Op3, ARMReg dest, ARMReg r1, ARMReg r2)
{
	Write32(condition | (0x7 << 24) | (Op << 20) | (dest << 16) | (Op2 << 12) | (r1 << 8) | (Op3 << 5) | (1 << 4) | r2);
}
void ARMXEmitter::UDIV(ARMReg dest, ARMReg dividend, ARMReg divisor)
{
	if (!cpu_info.bIDIVa)
		PanicAlert("Trying to use integer divide on hardware that doesn't support it. Bad programmer.");
	WriteSignedMultiply(3, 0xF, 0, dest, divisor, dividend);
}
void ARMXEmitter::SDIV(ARMReg dest, ARMReg dividend, ARMReg divisor)
{
	if (!cpu_info.bIDIVa)
		PanicAlert("Trying to use integer divide on hardware that doesn't support it. Bad programmer.");
	WriteSignedMultiply(1, 0xF, 0, dest, divisor, dividend);
}
void ARMXEmitter::LSL (ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(0, false, dest, src, op2);}
void ARMXEmitter::LSLS(ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(0, true, dest, src, op2);}
void ARMXEmitter::LSL (ARMReg dest, ARMReg src, ARMReg op2)	  { WriteShiftedDataOp(1, false, dest, src, op2);} 
void ARMXEmitter::LSLS(ARMReg dest, ARMReg src, ARMReg op2)	  { WriteShiftedDataOp(1, true, dest, src, op2);}
void ARMXEmitter::LSR (ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(3, false, dest, src, op2);}
void ARMXEmitter::ASR (ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(4, false, dest, src, op2);}
void ARMXEmitter::ASRS(ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(4, true, dest, src, op2);}
void ARMXEmitter::MUL (ARMReg dest,	ARMReg src, ARMReg op2)
{
	Write32(condition | (dest << 16) | (src << 8) | (9 << 4) | op2);
}
void ARMXEmitter::MULS(ARMReg dest,	ARMReg src, ARMReg op2)
{
	Write32(condition | (1 << 20) | (dest << 16) | (src << 8) | (9 << 4) | op2);
}

void ARMXEmitter::Write4OpMultiply(u32 op, ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn) {
	Write32(condition | (op << 20) | (destHi << 16) | (destLo << 12) | (rm << 8) | (9 << 4) | rn);
}

void ARMXEmitter::UMULL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0x8, destLo, destHi, rn, rm);
}

void ARMXEmitter::UMULLS(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0x9, destLo, destHi, rn, rm);
}

void ARMXEmitter::SMULL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0xC, destLo, destHi, rn, rm);
}

void ARMXEmitter::UMLAL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0xA, destLo, destHi, rn, rm);
}

void ARMXEmitter::SMLAL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0xE, destLo, destHi, rn, rm);
}

void ARMXEmitter::UBFX(ARMReg dest, ARMReg rn, u8 lsb, u8 width)
{
	Write32(condition | (0x7E0 << 16) | ((width - 1) << 16) | (dest << 12) | (lsb << 7) | (5 << 4) | rn);
}

void ARMXEmitter::CLZ(ARMReg rd, ARMReg rm)
{
	Write32(condition | (0x16F << 16) | (rd << 12) | (0xF1 << 4) | rm);
}

void ARMXEmitter::BFI(ARMReg rd, ARMReg rn, u8 lsb, u8 width)
{
	u32 msb = (lsb + width - 1);
	if (msb > 31) msb = 31;
	Write32(condition | (0x7C0 << 16) | (msb << 16) | (rd << 12) | (lsb << 7) | (1 << 4) | rn);
}

void ARMXEmitter::SXTB (ARMReg dest, ARMReg op2)
{
	Write32(condition | (0x6AF << 16) | (dest << 12) | (7 << 4) | op2);
}

void ARMXEmitter::SXTH (ARMReg dest, ARMReg op2, u8 rotation)
{
	SXTAH(dest, (ARMReg)15, op2, rotation);
}
void ARMXEmitter::SXTAH(ARMReg dest, ARMReg src, ARMReg op2, u8 rotation) 
{
	// bits ten and 11 are the rotation amount, see 8.8.232 for more
	// information
	Write32(condition | (0x6B << 20) | (src << 16) | (dest << 12) | (rotation << 10) | (7 << 4) | op2);
}
void ARMXEmitter::RBIT(ARMReg dest, ARMReg src)
{
	Write32(condition | (0x6F << 20) | (0xF << 16) | (dest << 12) | (0xF3 << 4) | src);
}
void ARMXEmitter::REV (ARMReg dest, ARMReg src) 
{
	Write32(condition | (0x6BF << 16) | (dest << 12) | (0xF3 << 4) | src);
}
void ARMXEmitter::REV16(ARMReg dest, ARMReg src)
{
	Write32(condition | (0x6BF << 16) | (dest << 12) | (0xFB << 4) | src);
}

void ARMXEmitter::_MSR (bool write_nzcvq, bool write_g,		Operand2 op2)
{
	Write32(condition | (0x320F << 12) | (write_nzcvq << 19) | (write_g << 18) | op2.Imm12Mod());
}
void ARMXEmitter::_MSR (bool write_nzcvq, bool write_g,		ARMReg src)
{
	Write32(condition | (0x120F << 12) | (write_nzcvq << 19) | (write_g << 18) | src);
}
void ARMXEmitter::MRS (ARMReg dest)
{
	Write32(condition | (16 << 20) | (15 << 16) | (dest << 12));
}
void ARMXEmitter::LDREX(ARMReg dest, ARMReg base)
{
	Write32(condition | (25 << 20) | (base << 16) | (dest << 12) | 0xF9F);
}
void ARMXEmitter::STREX(ARMReg result, ARMReg base, ARMReg op)
{
	_dbg_assert_msg_(DYNA_REC, (result != base && result != op), "STREX dest can't be other two registers");
	Write32(condition | (24 << 20) | (base << 16) | (result << 12) | (0xF9 << 4) | op);
}
void ARMXEmitter::DMB ()
{
	Write32(0xF57FF05E);
}
void ARMXEmitter::SVC(Operand2 op)
{
	Write32(condition | (0x0F << 24) | op.Imm24());
}

// IMM, REG, IMMSREG, RSR
// -1 for invalid if the instruction doesn't support that
const s32 LoadStoreOps[][4] = {
	{0x40, 0x60, 0x60, -1}, // STR
	{0x41, 0x61, 0x61, -1}, // LDR
	{0x44, 0x64, 0x64, -1}, // STRB
	{0x45, 0x65, 0x65, -1}, // LDRB
	// Special encodings
	{ 0x4,  0x0,  -1, -1}, // STRH
	{ 0x5,  0x1,  -1, -1}, // LDRH
	{ 0x5,  0x1,  -1, -1}, // LDRSB
	{ 0x5,  0x1,  -1, -1}, // LDRSH
};
const char *LoadStoreNames[] = {
	"STR",
	"LDR",
	"STRB",
	"LDRB",
	"STRH",
	"LDRH",
	"LDRSB",
	"LDRSH",
};

void ARMXEmitter::WriteStoreOp(u32 Op, ARMReg Rt, ARMReg Rn, Operand2 Rm, bool RegAdd)
{
	s32 op = LoadStoreOps[Op][Rm.GetType()]; // Type always decided by last operand
	u32 Data;

	// Qualcomm chipsets get /really/ angry if you don't use index, even if the offset is zero.
	// Some of these encodings require Index at all times anyway. Doesn't really matter.
	// bool Index = op2 != 0 ? true : false;
	bool Index = true;
	bool Add = false;

	// Special Encoding (misc addressing mode)
	bool SpecialOp = false;
	bool Half = false;
	bool SignedLoad = false;

	if (op == -1)
		_dbg_assert_msg_(DYNA_REC, false, "%s does not support %d", LoadStoreNames[Op], Rm.GetType()); 

	switch (Op)
	{
		case 4: // STRH
			SpecialOp = true;
			Half = true;
			SignedLoad = false;
		break;
		case 5: // LDRH
			SpecialOp = true;
			Half = true;
			SignedLoad = false;
		break;
		case 6: // LDRSB
			SpecialOp = true;
			Half = false;
			SignedLoad = true;
		break;
		case 7: // LDRSH
			SpecialOp = true;
			Half = true;
			SignedLoad = true;
		break;
	}
	switch (Rm.GetType())
	{
		case TYPE_IMM:
		{
			s32 Temp = (s32)Rm.Value;
			Data = abs(Temp);
			// The offset is encoded differently on this one.
			if (SpecialOp)
				Data = (Data & 0xF0 << 4) | (Data & 0xF);
			if (Temp >= 0) Add = true;
		}
		break;
		case TYPE_REG:
			Data = Rm.GetData();
			Add = RegAdd;
			break;
		case TYPE_IMMSREG:
			if (!SpecialOp)
			{
				Data = Rm.GetData();
				Add = RegAdd;
				break;
			}
			// Intentional fallthrough: TYPE_IMMSREG not supported for misc addressing.
		default:
			// RSR not supported for any of these
			// We already have the warning above
			BKPT(0x2);
			return;
		break;
	}
	if (SpecialOp)
	{
		// Add SpecialOp things
		Data = (0x9 << 4) | (SignedLoad << 6) | (Half << 5) | Data;
	}
	Write32(condition | (op << 20) | (Index << 24) | (Add << 23) | (Rn << 16) | (Rt << 12) | Data);
}

void ARMXEmitter::LDR (ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(1, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRB(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(3, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRH(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(5, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRSB(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(6, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRSH(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(7, dest, base, op2, RegAdd);}
void ARMXEmitter::STR  (ARMReg result, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(0, result, base, op2, RegAdd);}
void ARMXEmitter::STRH (ARMReg result, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(4, result, base, op2, RegAdd);}
void ARMXEmitter::STRB (ARMReg result, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(2, result, base, op2, RegAdd);}

void ARMXEmitter::WriteRegStoreOp(u32 op, ARMReg dest, bool WriteBack, u16 RegList)
{
	Write32(condition | (op << 20) | (WriteBack << 21) | (dest << 16) | RegList);
}
void ARMXEmitter::STMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, Regnum);
	for (i=0;i<Regnum;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	WriteRegStoreOp(0x90, dest, WriteBack, RegList);
}
void ARMXEmitter::LDMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, Regnum);
	for (i=0;i<Regnum;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	WriteRegStoreOp(0x89, dest, WriteBack, RegList);
}

ARMReg ARMXEmitter::SubBase(ARMReg Reg)
{
	if (Reg >= S0)
	{
		if (Reg >= D0)
		{
			if (Reg >= Q0)
				return (ARMReg)((Reg - Q0) * 2); // Always gets encoded as a double register
			return (ARMReg)(Reg - D0);
		}
		return (ARMReg)(Reg - S0);
	}
	return Reg;
}

// NEON Specific
void ARMXEmitter::VABD(IntegerSize Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(DYNA_REC, Vd >= D0, "Pass invalid register to VABD(float)");
	_dbg_assert_msg_(DYNA_REC, cpu_info.bNEON, "Can't use VABD(float) when CPU doesn't support it");
	bool register_quad = Vd >= Q0;

	// Gets encoded as a double register
	Vd = SubBase(Vd);
	Vn = SubBase(Vn);
	Vm = SubBase(Vm);

	Write32((0xF3 << 24) | ((Vd & 0x10) << 18) | (Size << 20) | ((Vn & 0xF) << 16) \
		| ((Vd & 0xF) << 12) | (0xD << 8) | ((Vn & 0x10) << 3) | (register_quad << 6) \
		| ((Vm & 0x10) << 2) | (Vm & 0xF));
}
void ARMXEmitter::VADD(IntegerSize Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(DYNA_REC, Vd >= D0, "Pass invalid register to VADD(integer)");
	_dbg_assert_msg_(DYNA_REC, cpu_info.bNEON, "Can't use VADD(integer) when CPU doesn't support it");

	bool register_quad = Vd >= Q0;

	// Gets encoded as a double register
	Vd = SubBase(Vd);
	Vn = SubBase(Vn);
	Vm = SubBase(Vm);

	Write32((0xF2 << 24) | ((Vd & 0x10) << 18) | (Size << 20) | ((Vn & 0xF) << 16) \
		| ((Vd & 0xF) << 12) | (0x8 << 8) | ((Vn & 0x10) << 3) | (register_quad << 6) \
		| ((Vm & 0x10) << 1) | (Vm & 0xF));

}
void ARMXEmitter::VSUB(IntegerSize Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(DYNA_REC, Vd >= Q0, "Pass invalid register to VSUB(integer)");
	_dbg_assert_msg_(DYNA_REC, cpu_info.bNEON, "Can't use VSUB(integer) when CPU doesn't support it");

	// Gets encoded as a double register
	Vd = SubBase(Vd);
	Vn = SubBase(Vn);
	Vm = SubBase(Vm);

	Write32((0xF3 << 24) | ((Vd & 0x10) << 18) | (Size << 20) | ((Vn & 0xF) << 16) \
		| ((Vd & 0xF) << 12) | (0x8 << 8) | ((Vn & 0x10) << 3) | (1 << 6) \
		| ((Vm & 0x10) << 2) | (Vm & 0xF));
}

// Double/single, Neon
extern const VFPEnc VFPOps[16][2] = {
	{{0xE0, 0xA0}, {0x20, 0xD1}}, // 0: VMLA
	{{0xE1, 0xA4}, {  -1,   -1}}, // 1: VNMLA
	{{0xE0, 0xA4}, {0x22, 0xD1}}, // 2: VMLS
	{{0xE1, 0xA0}, {  -1,   -1}}, // 3: VNMLS
	{{0xE3, 0xA0}, {0x20, 0xD0}}, // 4: VADD
	{{0xE3, 0xA4}, {0x22, 0xD0}}, // 5: VSUB
	{{0xE2, 0xA0}, {0x30, 0xD1}}, // 6: VMUL
	{{0xE2, 0xA4}, {  -1,   -1}}, // 7: VNMUL
	{{0xEB, 0xAC}, {  -1 /* 0x3B */,  -1 /* 0x70 */}}, // 8: VABS(Vn(0x0) used for encoding)
	{{0xE8, 0xA0}, {  -1,   -1}}, // 9: VDIV
	{{0xEB, 0xA4}, {  -1 /* 0x3B */,   -1 /* 0x78 */}}, // 10: VNEG(Vn(0x1) used for encoding)
	{{0xEB, 0xAC}, {  -1,   -1}}, // 11: VSQRT (Vn(0x1) used for encoding)
	{{0xEB, 0xA4}, {  -1,   -1}}, // 12: VCMP (Vn(0x4 | #0 ? 1 : 0) used for encoding)
	{{0xEB, 0xAC}, {  -1,   -1}}, // 13: VCMPE (Vn(0x4 | #0 ? 1 : 0) used for encoding)
	{{  -1,   -1}, {0x3B, 0x30}}, // 14: VABSi
	};

extern const char *VFPOpNames[16] = {
	"VMLA",
	"VNMLA",
	"VMLS",
	"VNMLS",
	"VADD",
	"VSUB",
	"VMUL",
	"VNMUL",
	"VABS",
	"VDIV",
	"VNEG",
	"VSQRT",
	"VCMP",
	"VCMPE",
	"VABSi",
};

u32 ARMXEmitter::EncodeVd(ARMReg Vd)
{
	bool quad_reg = Vd >= Q0;
	bool double_reg = Vd >= D0;

	ARMReg Reg = SubBase(Vd);

	if (quad_reg)
		return ((Reg & 0x10) << 18) | ((Reg & 0xF) << 12);
	else
		if (double_reg)
			return ((Reg & 0x10) << 18) | ((Reg & 0xF) << 12);
		else
			return ((Reg & 0x1) << 22) | ((Reg & 0x1E) << 11);
}
u32 ARMXEmitter::EncodeVn(ARMReg Vn)
{
	bool quad_reg = Vn >= Q0;
	bool double_reg = Vn >= D0;
	
	ARMReg Reg = SubBase(Vn);
	if (quad_reg)
		return ((Reg & 0xF) << 16) | ((Reg & 0x10) << 3);
	else
		if (double_reg)
			return ((Reg & 0xF) << 16) | ((Reg & 0x10) << 3);
		else
			return ((Reg & 0x1E) << 15) | ((Reg & 0x1) << 7);
}
u32 ARMXEmitter::EncodeVm(ARMReg Vm)
{
	bool quad_reg = Vm >= Q0;
	bool double_reg = Vm >= D0;

	ARMReg Reg = SubBase(Vm);

	if (quad_reg)
		return ((Reg & 0x10) << 2) | (Reg & 0xF);
	else
		if (double_reg)
			return ((Reg & 0x10) << 2) | (Reg & 0xF);
		else
			return ((Reg & 0x1) << 5) | (Reg >> 1);
}

void ARMXEmitter::WriteVFPDataOp(u32 Op, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	bool quad_reg = Vd >= Q0;
	bool double_reg = Vd >= D0 && Vd < Q0;

	VFPEnc enc = VFPOps[Op][quad_reg];
	if (enc.opc1 == -1 && enc.opc2 == -1)
		_dbg_assert_msg_(DYNA_REC, false, "%s does not support %s", VFPOpNames[Op], quad_reg ? "NEON" : "VFP"); 
	u32 VdEnc = EncodeVd(Vd);
	u32 VnEnc = EncodeVn(Vn);
	u32 VmEnc = EncodeVm(Vm);
	u32 cond = quad_reg ? (0xF << 28) : condition;

	Write32(cond | (enc.opc1 << 20) | VnEnc | VdEnc | (enc.opc2 << 4) | (quad_reg << 6) | (double_reg << 8) | VmEnc);
}
void ARMXEmitter::VMLA(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(0, Vd, Vn, Vm); }
void ARMXEmitter::VNMLA(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(1, Vd, Vn, Vm); }
void ARMXEmitter::VMLS(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(2, Vd, Vn, Vm); }
void ARMXEmitter::VNMLS(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(3, Vd, Vn, Vm); }
void ARMXEmitter::VADD(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(4, Vd, Vn, Vm); }
void ARMXEmitter::VSUB(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(5, Vd, Vn, Vm); }
void ARMXEmitter::VMUL(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(6, Vd, Vn, Vm); }
void ARMXEmitter::VNMUL(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(7, Vd, Vn, Vm); }
void ARMXEmitter::VABS(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(8, Vd, D0, Vm); }
void ARMXEmitter::VDIV(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(9, Vd, Vn, Vm); }
void ARMXEmitter::VNEG(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(10, Vd, D1, Vm); }
void ARMXEmitter::VSQRT(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(11, Vd, D1, Vm); }
void ARMXEmitter::VCMP(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(12, Vd, D4, Vm); }
void ARMXEmitter::VCMPE(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(13, Vd, D4, Vm); }
void ARMXEmitter::VCMP(ARMReg Vd){ WriteVFPDataOp(12, Vd, D5, D0); }
void ARMXEmitter::VCMPE(ARMReg Vd){ WriteVFPDataOp(13, Vd, D5, D0); }

void ARMXEmitter::VLDR(ARMReg Dest, ARMReg Base, s16 offset)
{
	_dbg_assert_msg_(DYNA_REC, Dest >= S0 && Dest <= D31, "Passed Invalid dest register to VLDR");
	_dbg_assert_msg_(DYNA_REC, Base <= R15, "Passed invalid Base register to VLDR");

	bool Add = offset >= 0 ? true : false;
	u32 imm = abs(offset);

	_dbg_assert_msg_(DYNA_REC, (imm & 0xC03) == 0, "VLDR: Offset needs to be word aligned and small enough");

	if (imm & 0xC03)
		ERROR_LOG(DYNA_REC, "VLDR: Bad offset %08x", imm);

	bool single_reg = Dest < D0;

	Dest = SubBase(Dest);

	if (single_reg)
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Dest & 0x1) << 22) | (1 << 20) | (Base << 16) \
			| ((Dest & 0x1E) << 11) | (10 << 8) | (imm >> 2));
	}
	else
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Dest & 0x10) << 18) | (1 << 20) | (Base << 16) \
			| ((Dest & 0xF) << 12) | (11 << 8) | (imm >> 2));
	}
}
void ARMXEmitter::VSTR(ARMReg Src, ARMReg Base, s16 offset)
{
	_dbg_assert_msg_(DYNA_REC, Src >= S0 && Src <= D31, "Passed invalid src register to VSTR");
	_dbg_assert_msg_(DYNA_REC, Base <= R15, "Passed invalid base register to VSTR");

	bool Add = offset >= 0 ? true : false;
	u32 imm = abs(offset);

	_dbg_assert_msg_(DYNA_REC, (imm & 0xC03) == 0, "VSTR: Offset needs to be word aligned and small enough");

	if (imm & 0xC03)
		ERROR_LOG(DYNA_REC, "VSTR: Bad offset %08x", imm);

	bool single_reg = Src < D0;

	Src = SubBase(Src);

	if (single_reg)
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Src & 0x1) << 22) | (Base << 16) \
			| ((Src & 0x1E) << 11) | (10 << 8) | (imm >> 2));
	}
	else
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Src & 0x10) << 18) | (Base << 16) \
			| ((Src & 0xF) << 12) | (11 << 8) | (imm >> 2));
	}
}

void ARMXEmitter::VMRS_APSR() {
	Write32(condition | 0xEF10A10 | (15 << 12));
}
void ARMXEmitter::VMRS(ARMReg Rt) {
	Write32(condition | (0xEF << 20) | (1 << 16) | (Rt << 12) | 0xA10);
}
void ARMXEmitter::VMSR(ARMReg Rt) {
	Write32(condition | (0xEE << 20) | (1 << 16) | (Rt << 12) | 0xA10);
}

// VFP and ASIMD
void ARMXEmitter::VMOV(ARMReg Dest, Operand2 op2)
{
	_dbg_assert_msg_(DYNA_REC, cpu_info.bVFPv3, "VMOV #imm requires VFPv3");
	Write32(condition | (0xEB << 20) | EncodeVd(Dest) | (0xA << 8) | op2.Imm8VFP());
}
void ARMXEmitter::VMOV(ARMReg Dest, ARMReg Src, bool high)
{
	_dbg_assert_msg_(DYNA_REC, Src < S0, "This VMOV doesn't support SRC other than ARM Reg");
	_dbg_assert_msg_(DYNA_REC, Dest >= D0, "This VMOV doesn't support DEST other than VFP");

	Dest = SubBase(Dest);

	Write32(condition | (0xE << 24) | (high << 21) | ((Dest & 0xF) << 16) | (Src << 12) \
		| (0xB << 8) | ((Dest & 0x10) << 3) | (1 << 4));
}

void ARMXEmitter::VMOV(ARMReg Dest, ARMReg Src)
{
	if (Dest > R15)
	{
		if (Src < S0)
		{
			if (Dest < D0)
			{
				// Moving to a Neon register FROM ARM Reg
				Dest = (ARMReg)(Dest - S0); 
				Write32(condition | (0xE0 << 20) | ((Dest & 0x1E) << 15) | (Src << 12) \
						| (0xA << 8) | ((Dest & 0x1) << 7) | (1 << 4));
				return;
			}
			else
			{
				// Move 64bit from Arm reg
				_dbg_assert_msg_(DYNA_REC, false, "This VMOV doesn't support moving 64bit ARM to NEON");
				return;
			}
		}
	}
	else
	{
		if (Src > R15)
		{
			if (Src < D0)
			{
				// Moving to ARM Reg from Neon Register
				Src = (ARMReg)(Src - S0);
				Write32(condition | (0xE1 << 20) | ((Src & 0x1E) << 15) | (Dest << 12) \
						| (0xA << 8) | ((Src & 0x1) << 7) | (1 << 4));
				return;
			}
			else
			{
				// Move 64bit To Arm reg
				_dbg_assert_msg_(DYNA_REC, false, "This VMOV doesn't support moving 64bit ARM From NEON");
				return;
			}
		}
		else
		{
			// Move Arm reg to Arm reg
			_dbg_assert_msg_(DYNA_REC, false, "VMOV doesn't support moving ARM registers");
		}
	}
	// Moving NEON registers
	int SrcSize = Src < D0 ? 1 : Src < Q0 ? 2 : 4;
	int DestSize = Dest < D0 ? 1 : Dest < Q0 ? 2 : 4;
	bool Single = DestSize == 1;
	bool Quad = DestSize == 4;

	_dbg_assert_msg_(DYNA_REC, SrcSize == DestSize, "VMOV doesn't support moving different register sizes");

	Dest = SubBase(Dest);
	Src = SubBase(Src);

	if (Single)
	{
		Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x3 << 20) | ((Dest & 0x1E) << 11) \
				| (0x5 << 9) | (1 << 6) | ((Src & 0x1) << 5) | ((Src & 0x1E) >> 1));
	}
	else
	{
		// Double and quad
		if (Quad)
		{
			_dbg_assert_msg_(DYNA_REC, cpu_info.bNEON, "Trying to use quad registers when you don't support ASIMD."); 
			// Gets encoded as a Double register
			Write32((0xF2 << 24) | ((Dest & 0x10) << 18) | (2 << 20) | ((Src & 0xF) << 16) \
				| ((Dest & 0xF) << 12) | (1 << 8) | ((Src & 0x10) << 3) | (1 << 6) \
				| ((Src & 0x10) << 1) | (1 << 4) | (Src & 0xF));

		}
		else
		{
			Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x3 << 20) | ((Dest & 0xF) << 12) \
				| (0x2D << 6) | ((Src & 0x10) << 1) | (Src & 0xF));
		}
	}
}

void ARMXEmitter::VCVT(ARMReg Dest, ARMReg Source, int flags)
{
	bool single_reg = (Dest < D0) && (Source < D0);
	bool single_double = !single_reg && (Source < D0 || Dest < D0);
	bool single_to_double = Source < D0;
	int op  = ((flags & TO_INT) ? (flags & ROUND_TO_ZERO) : (flags & IS_SIGNED)) ? 1 : 0;
	int op2 = ((flags & TO_INT) ? (flags & IS_SIGNED) : 0) ? 1 : 0;
	Dest = SubBase(Dest);
	Source = SubBase(Source);

	if (single_double)
	{
		// S32<->F64
		if ((flags & TO_INT) || (flags & TO_FLOAT))
		{
			if (single_to_double)
			{
				Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x7 << 19) \
					| ((Dest & 0xF) << 12) | (op << 7) | (0x2D << 6) | ((Source & 0x1) << 5) | (Source >> 1));
			} else {
				Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x7 << 19) | ((flags & TO_INT) << 18) | (op2 << 16) \
					| ((Dest & 0x1E) << 11) | (op << 7) | (0x2D << 6) | ((Source & 0x10) << 1) | (Source & 0xF));
			}
		}
		// F32<->F64
		else {
			if (single_to_double)
			{
				Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x3 << 20) | (0x7 << 16) \
					| ((Dest & 0xF) << 12) | (0x2B << 6) | ((Source & 0x1) << 5) | (Source >> 1));
			} else {
				Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x3 << 20) | (0x7 << 16) \
					| ((Dest & 0x1E) << 11) | (0x2F << 6) | ((Source & 0x10) << 1) | (Source & 0xF));
			}
		}
	} else if (single_reg) {
		Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x7 << 19) | ((flags & TO_INT) << 18) | (op2 << 16) \
			| ((Dest & 0x1E) << 11) | (op << 7) | (0x29 << 6) | ((Source & 0x1) << 5) | (Source >> 1));
	} else {
		Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x7 << 19) | ((flags & TO_INT) << 18) | (op2 << 16) \
			| ((Dest & 0xF) << 12) | (1 << 8) | (op << 7) | (0x29 << 6) | ((Source & 0x10) << 1) | (Source & 0xF));
	}
}

}
