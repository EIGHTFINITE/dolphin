// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.
//
// Additional copyrights go to Duddie and Tratax (c) 2004

#include "Core/DSP/Interpreter/DSPIntCCUtil.h"
#include "Core/DSP/Interpreter/DSPIntUtil.h"
#include "Core/DSP/Interpreter/DSPInterpreter.h"

// Arithmetic and accumulator control.

namespace DSP::Interpreter
{
// CLR $acR
// 1000 r001 xxxx xxxx
// Clears accumulator $acR
//
// flags out: --10 0100
void Interpreter::clr(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 11) & 0x1;

  SetLongAcc(reg, 0);
  UpdateSR64(0);
  ZeroWriteBackLog();
}

// CLRL $acR.l
// 1111 110r xxxx xxxx
// Clears (and rounds!) $acR.l - low 16 bits of accumulator $acR.
//
// flags out: --xx xx00
void Interpreter::clrl(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 8) & 0x1;
  const s64 acc = dsp_round_long_acc(GetLongAcc(reg));

  ZeroWriteBackLog();

  SetLongAcc(reg, acc);
  UpdateSR64(acc);
}

//----

// ANDCF $acD.m, #I
// 0000 001r 1100 0000
// iiii iiii iiii iiii
// Set logic zero (LZ) flag in status register $sr if result of logic AND of
// accumulator mid part $acD.m with immediate value I is equal I.
//
// flags out: -x-- ----
void Interpreter::andcf(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = m_dsp_core.DSPState().FetchInstruction();
  const u16 val = GetAccMid(reg);

  UpdateSRLogicZero((val & imm) == imm);
}

// ANDF $acD.m, #I
// 0000 001r 1010 0000
// iiii iiii iiii iiii
// Set logic zero (LZ) flag in status register $sr if result of logical AND
// operation of accumulator mid part $acD.m with immediate value I is equal
// immediate value 0.
//
// flags out: -x-- ----
void Interpreter::andf(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = m_dsp_core.DSPState().FetchInstruction();
  const u16 val = GetAccMid(reg);

  UpdateSRLogicZero((val & imm) == 0);
}

//----

// TST
// 1011 r001 xxxx xxxx
// Test accumulator %acR.
//
// flags out: --xx xx00
void Interpreter::tst(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 11) & 0x1;
  const s64 acc = GetLongAcc(reg);

  UpdateSR64(acc);
  ZeroWriteBackLog();
}

// TSTAXH $axR.h
// 1000 011r xxxx xxxx
// Test high part of secondary accumulator $axR.h.
//
// flags out: --x0 xx00
void Interpreter::tstaxh(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 8) & 0x1;
  const s16 val = GetAXHigh(reg);

  UpdateSR16(val);
  ZeroWriteBackLog();
}

//----

// CMP
// 1000 0010 xxxx xxxx
// Compares accumulator $ac0 with accumulator $ac1.
//
// flags out: x-xx xxxx
void Interpreter::cmp(const UDSPInstruction)
{
  const s64 acc0 = GetLongAcc(0);
  const s64 acc1 = GetLongAcc(1);
  const s64 res = dsp_convert_long_acc(acc0 - acc1);

  UpdateSR64(res, isCarry2(acc0, res),
             isOverflow(acc0, -acc1, res));  // CF -> influence on ABS/0xa100
  ZeroWriteBackLog();
}

// CMPAR $acS axR.h
// 110r s001 xxxx xxxx
// Compares accumulator $acS with accumulator axR.h.
// Not described by Duddie's doc - at least not as a separate instruction.
//
// flags out: x-xx xxxx
void Interpreter::cmpar(const UDSPInstruction opc)
{
  const u8 rreg = (opc >> 12) & 0x1;
  const u8 sreg = (opc >> 11) & 0x1;

  const s64 sr = GetLongAcc(sreg);
  s64 rr = GetAXHigh(rreg);
  rr <<= 16;
  const s64 res = dsp_convert_long_acc(sr - rr);

  UpdateSR64(res, isCarry2(sr, res), isOverflow(sr, -rr, res));
  ZeroWriteBackLog();
}

// CMPI $amD, #I
// 0000 001r 1000 0000
// iiii iiii iiii iiii
// Compares mid accumulator $acD.hm ($amD) with sign extended immediate value I.
// Although flags are being set regarding whole accumulator register.
//
// flags out: x-xx xxxx
void Interpreter::cmpi(const UDSPInstruction opc)
{
  const u8 reg = (opc >> 8) & 0x1;
  auto& state = m_dsp_core.DSPState();

  const s64 val = GetLongAcc(reg);
  // Immediate is considered to be at M level in the 40-bit accumulator.
  const s64 imm = (s64)(s16)state.FetchInstruction() << 16;
  const s64 res = dsp_convert_long_acc(val - imm);

  UpdateSR64(res, isCarry2(val, res), isOverflow(val, -imm, res));
}

// CMPIS $acD, #I
// 0000 011d iiii iiii
// Compares accumulator with short immediate. Comaprison is executed
// by subtracting short immediate (8bit sign extended) from mid accumulator
// $acD.hm and computing flags based on whole accumulator $acD.
//
// flags out: x-xx xxxx
void Interpreter::cmpis(const UDSPInstruction opc)
{
  const u8 areg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(areg);
  s64 val = (s8)opc;
  val <<= 16;
  const s64 res = dsp_convert_long_acc(acc - val);

  UpdateSR64(res, isCarry2(acc, res), isOverflow(acc, -val, res));
}

//----

// XORR $acD.m, $axS.h
// 0011 00sd 0xxx xxxx
// Logic XOR (exclusive or) middle part of accumulator $acD.m with
// high part of secondary accumulator $axS.h.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::xorr(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;
  auto& state = m_dsp_core.DSPState();

  const u16 accm = state.r.ac[dreg].m ^ state.r.ax[sreg].h;
  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// ANDR $acD.m, $axS.h
// 0011 01sd 0xxx xxxx
// Logic AND middle part of accumulator $acD.m with high part of
// secondary accumulator $axS.h.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::andr(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;
  auto& state = m_dsp_core.DSPState();

  const u16 accm = state.r.ac[dreg].m & state.r.ax[sreg].h;
  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// ORR $acD.m, $axS.h
// 0011 10sd 0xxx xxxx
// Logic OR middle part of accumulator $acD.m with high part of
// secondary accumulator $axS.h.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::orr(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;
  auto& state = m_dsp_core.DSPState();

  const u16 accm = state.r.ac[dreg].m | state.r.ax[sreg].h;
  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// ANDC $acD.m, $ac(1-D).m
// 0011 110d 0xxx xxxx
// Logic AND middle part of accumulator $acD.m with middle part of
// accumulator $ac(1-D).m
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::andc(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = state.r.ac[dreg].m & state.r.ac[1 - dreg].m;

  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// ORC $acD.m, $ac(1-D).m
// 0011 111d 0xxx xxxx
// Logic OR middle part of accumulator $acD.m with middle part of
// accumulator $ac(1-D).m.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::orc(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = state.r.ac[dreg].m | state.r.ac[1 - dreg].m;

  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// XORC $acD.m
// 0011 000d 1xxx xxxx
// Logic XOR (exclusive or) middle part of accumulator $acD.m with $ac(1-D).m
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::xorc(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = state.r.ac[dreg].m ^ state.r.ac[1 - dreg].m;

  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// NOT $acD.m
// 0011 001d 1xxx xxxx
// Invert all bits in dest reg, aka xor with 0xffff
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::notc(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = state.r.ac[dreg].m ^ 0xffff;

  ZeroWriteBackLogPreserveAcc(dreg);

  state.r.ac[dreg].m = accm;
  UpdateSR16(static_cast<s16>(accm), false, false, isOverS32(GetLongAcc(dreg)));
}

// XORI $acD.m, #I
// 0000 001r 0010 0000
// iiii iiii iiii iiii
// Logic exclusive or (XOR) of accumulator mid part $acD.m with
// immediate value I.
//
// flags out: --xx xx00
void Interpreter::xori(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = state.FetchInstruction();
  state.r.ac[reg].m ^= imm;

  UpdateSR16(static_cast<s16>(state.r.ac[reg].m), false, false, isOverS32(GetLongAcc(reg)));
}

// ANDI $acD.m, #I
// 0000 001r 0100 0000
// iiii iiii iiii iiii
// Logic AND of accumulator mid part $acD.m with immediate value I.
//
// flags out: --xx xx00
void Interpreter::andi(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = state.FetchInstruction();

  state.r.ac[reg].m &= imm;

  UpdateSR16(static_cast<s16>(state.r.ac[reg].m), false, false, isOverS32(GetLongAcc(reg)));
}

// ORI $acD.m, #I
// 0000 001r 0110 0000
// iiii iiii iiii iiii
// Logic OR of accumulator mid part $acD.m with immediate value I.
//
// flags out: --xx xx00
void Interpreter::ori(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = state.FetchInstruction();

  state.r.ac[reg].m |= imm;

  UpdateSR16(static_cast<s16>(state.r.ac[reg].m), false, false, isOverS32(GetLongAcc(reg)));
}

//----

// ADDR $acD.M, $axS.L
// 0100 0ssd xxxx xxxx
// Adds register $axS.L to accumulator $acD.M register.
//
// flags out: x-xx xxxx
void Interpreter::addr(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 9) & 0x3) + DSP_REG_AXL0;

  const s64 acc = GetLongAcc(dreg);
  s64 ax = 0;

  switch (sreg)
  {
  case DSP_REG_AXL0:
  case DSP_REG_AXL1:
    ax = static_cast<s16>(state.r.ax[sreg - DSP_REG_AXL0].l);
    break;
  case DSP_REG_AXH0:
  case DSP_REG_AXH1:
    ax = static_cast<s16>(state.r.ax[sreg - DSP_REG_AXH0].h);
    break;
  default:
    ax = 0;
    break;
  }

  ax <<= 16;
  s64 res = acc + ax;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, ax, res));
}

// ADDAX $acD, $axS
// 0100 10sd xxxx xxxx
// Adds secondary accumulator $axS to accumulator register $acD.
//
// flags out: x-xx xxxx
void Interpreter::addax(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 ax = GetLongACX(sreg);
  s64 res = acc + ax;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, ax, res));
}

// ADD $acD, $ac(1-D)
// 0100 110d xxxx xxxx
// Adds accumulator $ac(1-D) to accumulator register $acD.
//
// flags out: x-xx xxxx
void Interpreter::add(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc0 = GetLongAcc(dreg);
  const s64 acc1 = GetLongAcc(1 - dreg);
  s64 res = acc0 + acc1;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc0, res), isOverflow(acc0, acc1, res));
}

// ADDP $acD
// 0100 111d xxxx xxxx
// Adds product register to accumulator register.
//
// flags out: x-xx xxxx
void Interpreter::addp(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 prod = GetLongProduct();
  s64 res = acc + prod;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, prod, res));
}

// ADDAXL $acD, $axS.l
// 0111 00sd xxxx xxxx
// Adds secondary accumulator $axS.l to accumulator register $acD.
// should be unsigned values!!
//
// flags out: x-xx xxxx
void Interpreter::addaxl(const UDSPInstruction opc)
{
  const u8 sreg = (opc >> 9) & 0x1;
  const u8 dreg = (opc >> 8) & 0x1;

  const u64 acc = GetLongAcc(dreg);
  const u16 acx = static_cast<u16>(GetAXLow(sreg));

  u64 res = acc + acx;

  ZeroWriteBackLog();

  SetLongAcc(dreg, static_cast<s64>(res));
  res = GetLongAcc(dreg);
  UpdateSR64(static_cast<s64>(res), isCarry(acc, res),
             isOverflow(static_cast<s64>(acc), static_cast<s64>(acx), static_cast<s64>(res)));
}

// ADDI $amR, #I
// 0000 001r 0000 0000
// iiii iiii iiii iiii
// Adds immediate (16-bit sign extended) to mid accumulator $acD.hm.
//
// flags out: x-xx xxxx
void Interpreter::addi(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 areg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(areg);
  s64 imm = static_cast<s16>(state.FetchInstruction());
  imm <<= 16;
  s64 res = acc + imm;

  SetLongAcc(areg, res);
  res = GetLongAcc(areg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, imm, res));
}

// ADDIS $acD, #I
// 0000 010d iiii iiii
// Adds short immediate (8-bit sign extended) to mid accumulator $acD.hm.
//
// flags out: x-xx xxxx
void Interpreter::addis(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  s64 imm = static_cast<s8>(static_cast<u8>(opc));
  imm <<= 16;
  s64 res = acc + imm;

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, imm, res));
}

// INCM $acsD
// 0111 010d xxxx xxxx
// Increment 24-bit mid-accumulator $acsD.
//
// flags out: x-xx xxxx
void Interpreter::incm(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 sub = 0x10000;
  const s64 acc = GetLongAcc(dreg);
  s64 res = acc + sub;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, sub, res));
}

// INC $acD
// 0111 011d xxxx xxxx
// Increment accumulator $acD.
//
// flags out: x-xx xxxx
void Interpreter::inc(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  s64 res = acc + 1;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry(acc, res), isOverflow(acc, 1, res));
}

//----

// SUBR $acD.M, $axS.L
// 0101 0ssd xxxx xxxx
// Subtracts register $axS.L from accumulator $acD.M register.
//
// flags out: x-xx xxxx
void Interpreter::subr(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 9) & 0x3) + DSP_REG_AXL0;

  const s64 acc = GetLongAcc(dreg);
  s64 ax = 0;

  switch (sreg)
  {
  case DSP_REG_AXL0:
  case DSP_REG_AXL1:
    ax = static_cast<s16>(state.r.ax[sreg - DSP_REG_AXL0].l);
    break;
  case DSP_REG_AXH0:
  case DSP_REG_AXH1:
    ax = static_cast<s16>(state.r.ax[sreg - DSP_REG_AXH0].h);
    break;
  default:
    ax = 0;
    break;
  }

  ax <<= 16;
  s64 res = acc - ax;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry2(acc, res), isOverflow(acc, -ax, res));
}

// SUBAX $acD, $axS
// 0101 10sd xxxx xxxx
// Subtracts secondary accumulator $axS from accumulator register $acD.
//
// flags out: x-xx xxxx
void Interpreter::subax(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 acx = GetLongACX(sreg);
  s64 res = acc - acx;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry2(acc, res), isOverflow(acc, -acx, res));
}

// SUB $acD, $ac(1-D)
// 0101 110d xxxx xxxx
// Subtracts accumulator $ac(1-D) from accumulator register $acD.
//
// flags out: x-xx xxxx
void Interpreter::sub(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc1 = GetLongAcc(dreg);
  const s64 acc2 = GetLongAcc(1 - dreg);
  s64 res = acc1 - acc2;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry2(acc1, res), isOverflow(acc1, -acc2, res));
}

// SUBP $acD
// 0101 111d xxxx xxxx
// Subtracts product register from accumulator register.
//
// flags out: x-xx xxxx
void Interpreter::subp(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 prod = GetLongProduct();
  s64 res = acc - prod;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry2(acc, res), isOverflow(acc, -prod, res));
}

// DECM $acsD
// 0111 100d xxxx xxxx
// Decrement 24-bit mid-accumulator $acsD.
//
// flags out: x-xx xxxx
void Interpreter::decm(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x01;

  const s64 sub = 0x10000;
  const s64 acc = GetLongAcc(dreg);
  s64 res = acc - sub;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry2(acc, res), isOverflow(acc, -sub, res));
}

// DEC $acD
// 0111 101d xxxx xxxx
// Decrement accumulator $acD.
//
// flags out: x-xx xxxx
void Interpreter::dec(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x01;

  const s64 acc = GetLongAcc(dreg);
  s64 res = acc - 1;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarry2(acc, res), isOverflow(acc, -1, res));
}

//----

// NEG $acD
// 0111 110d xxxx xxxx
// Negate accumulator $acD.
//
// flags out: --xx xx00
void Interpreter::neg(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  s64 acc = GetLongAcc(dreg);
  acc = 0 - acc;

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}

// ABS  $acD
// 1010 d001 xxxx xxxx
// absolute value of $acD
//
// flags out: --xx xx00
void Interpreter::abs(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 11) & 0x1;

  s64 acc = GetLongAcc(dreg);

  if (acc < 0)
    acc = 0 - acc;

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}
//----

// MOVR $acD, $axS.R
// 0110 0srd xxxx xxxx
// Moves register $axS.R (sign extended) to middle accumulator $acD.hm.
// Sets $acD.l to 0.
//
// flags out: --xx xx00
void Interpreter::movr(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 areg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 9) & 0x3) + DSP_REG_AXL0;

  s64 ax = 0;
  switch (sreg)
  {
  case DSP_REG_AXL0:
  case DSP_REG_AXL1:
    ax = static_cast<s16>(state.r.ax[sreg - DSP_REG_AXL0].l);
    break;
  case DSP_REG_AXH0:
  case DSP_REG_AXH1:
    ax = static_cast<s16>(state.r.ax[sreg - DSP_REG_AXH0].h);
    break;
  default:
    ax = 0;
    break;
  }
  ax <<= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, ax);
  UpdateSR64(ax);
}

// MOVAX $acD, $axS
// 0110 10sd xxxx xxxx
// Moves secondary accumulator $axS to accumulator $axD.
//
// flags out: --xx xx00
void Interpreter::movax(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 acx = GetLongACX(sreg);

  ZeroWriteBackLog();

  SetLongAcc(dreg, acx);
  UpdateSR64(acx);
}

// MOV $acD, $ac(1-D)
// 0110 110d xxxx xxxx
// Moves accumulator $ax(1-D) to accumulator $axD.
//
// flags out: --x0 xx00
void Interpreter::mov(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u64 acc = GetLongAcc(1 - dreg);

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(acc);
}

//----

// LSL16 $acR
// 1111 000r xxxx xxxx
// Logically shifts left accumulator $acR by 16.
//
// flags out: --xx xx00
void Interpreter::lsl16(const UDSPInstruction opc)
{
  const u8 areg = (opc >> 8) & 0x1;

  s64 acc = GetLongAcc(areg);
  acc <<= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, acc);
  UpdateSR64(GetLongAcc(areg));
}

// LSR16 $acR
// 1111 010r xxxx xxxx
// Logically shifts right accumulator $acR by 16.
//
// flags out: --xx xx00
void Interpreter::lsr16(const UDSPInstruction opc)
{
  const u8 areg = (opc >> 8) & 0x1;

  u64 acc = GetLongAcc(areg);
  // Lop off the extraneous sign extension our 64-bit fake accum causes
  acc &= 0x000000FFFFFFFFFFULL;
  acc >>= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, static_cast<s64>(acc));
  UpdateSR64(GetLongAcc(areg));
}

// ASR16 $acR
// 1001 r001 xxxx xxxx
// Arithmetically shifts right accumulator $acR by 16.
//
// flags out: --xx xx00
void Interpreter::asr16(const UDSPInstruction opc)
{
  const u8 areg = (opc >> 11) & 0x1;

  s64 acc = GetLongAcc(areg);
  acc >>= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, acc);
  UpdateSR64(GetLongAcc(areg));
}

// LSL $acR, #I
// 0001 010r 00ii iiii
// Logically shifts left accumulator $acR by number specified by value I.
//
// flags out: --xx xx00
void Interpreter::lsl(const UDSPInstruction opc)
{
  const u8 rreg = (opc >> 8) & 0x01;
  const u16 shift = opc & 0x3f;
  u64 acc = GetLongAcc(rreg);

  acc <<= shift;

  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// LSR $acR, #I
// 0001 010r 01ii iiii
// Logically shifts right accumulator $acR by number specified by value
// calculated by negating sign extended bits 0-6.
//
// flags out: --xx xx00
void Interpreter::lsr(const UDSPInstruction opc)
{
  const u8 rreg = (opc >> 8) & 0x01;
  u16 shift;
  u64 acc = GetLongAcc(rreg);
  // Lop off the extraneous sign extension our 64-bit fake accum causes
  acc &= 0x000000FFFFFFFFFFULL;

  if ((opc & 0x3f) == 0)
    shift = 0;
  else
    shift = 0x40 - (opc & 0x3f);

  acc >>= shift;

  SetLongAcc(rreg, static_cast<s64>(acc));
  UpdateSR64(GetLongAcc(rreg));
}

// ASL $acR, #I
// 0001 010r 10ii iiii
// Logically shifts left accumulator $acR by number specified by value I.
//
// flags out: --xx xx00
void Interpreter::asl(const UDSPInstruction opc)
{
  const u8 rreg = (opc >> 8) & 0x01;
  const u16 shift = opc & 0x3f;
  u64 acc = GetLongAcc(rreg);

  acc <<= shift;

  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// ASR $acR, #I
// 0001 010r 11ii iiii
// Arithmetically shifts right accumulator $acR by number specified by
// value calculated by negating sign extended bits 0-6.
//
// flags out: --xx xx00
void Interpreter::asr(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x01;
  u16 shift;

  if ((opc & 0x3f) == 0)
    shift = 0;
  else
    shift = 0x40 - (opc & 0x3f);

  // arithmetic shift
  s64 acc = GetLongAcc(dreg);
  acc >>= shift;

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}

// LSRN  (fixed parameters)
// 0000 0010 1100 1010
// Logically shifts right accumulator $ACC0 by lower 7-bit (signed) value in $AC1.M
// (if value negative, becomes left shift).
//
// flags out: --xx xx00
void Interpreter::lsrn(const UDSPInstruction opc)
{
  s16 shift;
  const u16 accm = static_cast<u16>(GetAccMid(1));
  u64 acc = GetLongAcc(0);
  acc &= 0x000000FFFFFFFFFFULL;

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
  {
    acc >>= shift;
  }
  else if (shift < 0)
  {
    acc <<= -shift;
  }

  SetLongAcc(0, static_cast<s64>(acc));
  UpdateSR64(GetLongAcc(0));
}

// ASRN  (fixed parameters)
// 0000 0010 1100 1011
// Arithmetically shifts right accumulator $ACC0 by lower 7-bit (signed) value in $AC1.M
// (if value negative, becomes left shift).
//
// flags out: --xx xx00
void Interpreter::asrn(const UDSPInstruction opc)
{
  s16 shift;
  const u16 accm = static_cast<u16>(GetAccMid(1));
  s64 acc = GetLongAcc(0);

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
  {
    acc >>= shift;
  }
  else if (shift < 0)
  {
    acc <<= -shift;
  }

  SetLongAcc(0, acc);
  UpdateSR64(GetLongAcc(0));
}

// LSRNRX $acD, $axS.h
// 0011 01sd 1xxx xxxx
// Logically shifts left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AX[S].H
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::lsrnrx(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  s16 shift;
  const u16 axh = state.r.ax[sreg].h;
  u64 acc = GetLongAcc(dreg);
  acc &= 0x000000FFFFFFFFFFULL;

  if ((axh & 0x3f) == 0)
    shift = 0;
  else if ((axh & 0x40) != 0)
    shift = -0x40 + (axh & 0x3f);
  else
    shift = axh & 0x3f;

  if (shift > 0)
  {
    acc <<= shift;
  }
  else if (shift < 0)
  {
    acc >>= -shift;
  }

  ZeroWriteBackLog();

  SetLongAcc(dreg, static_cast<s64>(acc));
  UpdateSR64(GetLongAcc(dreg));
}

// ASRNRX $acD, $axS.h
// 0011 10sd 1xxx xxxx
// Arithmetically shifts left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AX[S].H
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::asrnrx(const UDSPInstruction opc)
{
  auto& state = m_dsp_core.DSPState();
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  s16 shift;
  const u16 axh = state.r.ax[sreg].h;
  s64 acc = GetLongAcc(dreg);

  if ((axh & 0x3f) == 0)
    shift = 0;
  else if ((axh & 0x40) != 0)
    shift = -0x40 + (axh & 0x3f);
  else
    shift = axh & 0x3f;

  if (shift > 0)
  {
    acc <<= shift;
  }
  else if (shift < 0)
  {
    acc >>= -shift;
  }

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}

// LSRNR  $acD
// 0011 110d 1xxx xxxx
// Logically shifts left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AC[1-D].M
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::lsrnr(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  s16 shift;
  const u16 accm = static_cast<u16>(GetAccMid(1 - dreg));
  u64 acc = GetLongAcc(dreg);
  acc &= 0x000000FFFFFFFFFFULL;

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
    acc <<= shift;
  else if (shift < 0)
    acc >>= -shift;

  ZeroWriteBackLog();

  SetLongAcc(dreg, static_cast<s64>(acc));
  UpdateSR64(GetLongAcc(dreg));
}

// ASRNR  $acD
// 0011 111d 1xxx xxxx
// Arithmetically shift left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AC[1-D].M
// x = extension (7 bits!!)
//
// flags out: --xx xx00
void Interpreter::asrnr(const UDSPInstruction opc)
{
  const u8 dreg = (opc >> 8) & 0x1;

  s16 shift;
  const u16 accm = static_cast<u16>(GetAccMid(1 - dreg));
  s64 acc = GetLongAcc(dreg);

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
    acc <<= shift;
  else if (shift < 0)
    acc >>= -shift;

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}
}  // namespace DSP::Interpreter
