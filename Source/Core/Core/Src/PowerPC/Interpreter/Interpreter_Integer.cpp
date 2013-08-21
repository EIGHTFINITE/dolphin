// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Interpreter.h"
#include "../../Core.h"
#include "Atomic.h"

void Interpreter::Helper_UpdateCR0(u32 _uValue)
{
	u32 new_cr0;
	int sValue = (int)_uValue;
	if (sValue > 0)
		new_cr0 = 0x4;
	else if (sValue < 0)
		new_cr0 = 0x8;
	else
		new_cr0 = 0x2;
	new_cr0 |= GetXER_SO();
	SetCRField(0, new_cr0);
}

void Interpreter::Helper_UpdateCRx(int _x, u32 _uValue)
{
	u32 new_crX;
	int sValue = (int)_uValue;
	if (sValue > 0)
		new_crX = 0x4;
	else if (sValue < 0)
		new_crX = 0x8;
	else
		new_crX = 0x2;
	new_crX |= GetXER_SO();
	SetCRField(_x, new_crX);
}

u32 Interpreter::Helper_Carry(u32 _uValue1, u32 _uValue2)
{
	return _uValue2 > (~_uValue1);
}

u32 Interpreter::Helper_Mask(int mb, int me)
{
	//first make 001111111111111 part
	u32 begin = 0xFFFFFFFF >> mb;
	//then make 000000000001111 part, which is used to flip the bits of the first one
	u32 end = me < 31 ? (0xFFFFFFFF >> (me + 1)) : 0;
	//do the bitflip
	u32 mask = begin ^ end;
	//and invert if backwards
	if (me < mb) 
		return ~mask;
	else
		return mask;
}

void Interpreter::addi(UGeckoInstruction _inst)
{
	if (_inst.RA)
		m_GPR[_inst.RD] = m_GPR[_inst.RA] + _inst.SIMM_16;
	else
		m_GPR[_inst.RD] = _inst.SIMM_16;
}

void Interpreter::addic(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 imm = (u32)(s32)_inst.SIMM_16;
	// TODO(ector): verify this thing 
	m_GPR[_inst.RD] = a + imm;
	SetCarry(Helper_Carry(a, imm));
}

void Interpreter::addic_rc(UGeckoInstruction _inst)
{
	addic(_inst);
	Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::addis(UGeckoInstruction _inst)
{
	if (_inst.RA)
		m_GPR[_inst.RD] = m_GPR[_inst.RA] + (_inst.SIMM_16 << 16);
	else
		m_GPR[_inst.RD] = (_inst.SIMM_16 << 16);
}

void Interpreter::andi_rc(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] & _inst.UIMM;
	Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::andis_rc(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] & ((u32)_inst.UIMM<<16);
	Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::cmpi(UGeckoInstruction _inst)
{
	Helper_UpdateCRx(_inst.CRFD, m_GPR[_inst.RA] - _inst.SIMM_16);
}

void Interpreter::cmpli(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = _inst.UIMM;
	int f;
	if (a < b)      f = 0x8;
	else if (a > b) f = 0x4;
	else            f = 0x2; //equals
	if (GetXER_SO()) f |= 0x1;
	SetCRField(_inst.CRFD, f);
}

void Interpreter::mulli(UGeckoInstruction _inst)
{
	m_GPR[_inst.RD] = (s32)m_GPR[_inst.RA] * _inst.SIMM_16;
}

void Interpreter::ori(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] | _inst.UIMM;
}

void Interpreter::oris(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] | (_inst.UIMM << 16);
}

void Interpreter::subfic(UGeckoInstruction _inst)
{
/*	u32 rra = ~m_GPR[_inst.RA];
	s32 immediate = (s16)_inst.SIMM_16 + 1;

//	#define CALC_XER_CA(X,Y) (((X) + (Y) < X) ? SET_XER_CA : CLEAR_XER_CA)
	if ((rra + immediate) < rra)
		SetCarry(1);
	else 
		SetCarry(0);

	m_GPR[_inst.RD] = rra - immediate;
*/

	s32 immediate = _inst.SIMM_16;
	m_GPR[_inst.RD] = immediate - (signed)m_GPR[_inst.RA];
	SetCarry((m_GPR[_inst.RA] == 0) || (Helper_Carry(0-m_GPR[_inst.RA], immediate)));
}

void Interpreter::twi(UGeckoInstruction _inst) 
{
	s32 a = m_GPR[_inst.RA];
	s32 b = _inst.SIMM_16;
	s32 TO = _inst.TO;

	ERROR_LOG(POWERPC, "twi rA %x SIMM %x TO %0x", a, b, TO);

	if (   ((a < b) && (TO & 0x10))
		|| ((a > b) && (TO & 0x08))
		|| ((a ==b) && (TO & 0x04))
		|| (((u32)a <(u32)b) && (TO & 0x02))
		|| (((u32)a >(u32)b) && (TO & 0x01)))
	{
		Common::AtomicOr(PowerPC::ppcState.Exceptions, EXCEPTION_PROGRAM);
		PowerPC::CheckExceptions();
		m_EndBlock = true; // Dunno about this
	}
}

void Interpreter::xori(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] ^ _inst.UIMM;
}

void Interpreter::xoris(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] ^ (_inst.UIMM << 16);
}

void Interpreter::rlwimix(UGeckoInstruction _inst)
{
	u32 mask = Helper_Mask(_inst.MB,_inst.ME);
	m_GPR[_inst.RA] = (m_GPR[_inst.RA] & ~mask) | (_rotl(m_GPR[_inst.RS],_inst.SH) & mask);
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::rlwinmx(UGeckoInstruction _inst)
{
	u32 mask = Helper_Mask(_inst.MB,_inst.ME);
	m_GPR[_inst.RA] = _rotl(m_GPR[_inst.RS],_inst.SH) & mask;
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::rlwnmx(UGeckoInstruction _inst)
{
	u32 mask = Helper_Mask(_inst.MB,_inst.ME);
	m_GPR[_inst.RA] = _rotl(m_GPR[_inst.RS], m_GPR[_inst.RB] & 0x1F) & mask;

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::andx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] & m_GPR[_inst.RB];

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::andcx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] & ~m_GPR[_inst.RB];

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::cmp(UGeckoInstruction _inst)
{
	s32 a = (s32)m_GPR[_inst.RA];
	s32 b = (s32)m_GPR[_inst.RB];
	int fTemp = 0x8; // a < b 
		//	if (a < b)  fTemp = 0x8; else 
	if (a > b)  fTemp = 0x4;
	else if (a == b) fTemp = 0x2;
	if (GetXER_SO()) PanicAlert("cmp getting overflow flag"); // fTemp |= 0x1
	SetCRField(_inst.CRFD, fTemp);
}

void Interpreter::cmpl(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	u32 fTemp = 0x8; // a < b

		//	if (a < b)  fTemp = 0x8;else 
	if (a > b)  fTemp = 0x4;
	else if (a == b) fTemp = 0x2;
	if (GetXER_SO()) PanicAlert("cmpl getting overflow flag"); // fTemp |= 0x1;
	SetCRField(_inst.CRFD, fTemp);
}

void Interpreter::cntlzwx(UGeckoInstruction _inst)
{
	u32 val = m_GPR[_inst.RS];
	u32 mask = 0x80000000;
	int i = 0;
	for (; i < 32; i++, mask >>= 1)
		if (val & mask) 
			break;
	m_GPR[_inst.RA] = i;
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::eqvx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = ~(m_GPR[_inst.RS] ^ m_GPR[_inst.RB]);

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::extsbx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = (u32)(s32)(s8)m_GPR[_inst.RS];

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::extshx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = (u32)(s32)(s16)m_GPR[_inst.RS];

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::nandx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = ~(m_GPR[_inst.RS] & m_GPR[_inst.RB]);

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::norx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = ~(m_GPR[_inst.RS] | m_GPR[_inst.RB]);

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::orx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] | m_GPR[_inst.RB];

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::orcx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] | (~m_GPR[_inst.RB]);

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::slwx(UGeckoInstruction _inst)
{
	u32 amount = m_GPR[_inst.RB];
	m_GPR[_inst.RA] = (amount & 0x20) ? 0 : m_GPR[_inst.RS] << amount;

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::srawx(UGeckoInstruction _inst)
{
	int rb = m_GPR[_inst.RB];
	if (rb & 0x20)
	{
		if (m_GPR[_inst.RS] & 0x80000000)
		{
			m_GPR[_inst.RA] = 0xFFFFFFFF;
			SetCarry(1);
		}
		else
		{
			m_GPR[_inst.RA] = 0x00000000;
			SetCarry(0);
		}
	}
	else
	{
		int amount = rb & 0x1f;
		if (amount == 0)
		{
			m_GPR[_inst.RA] = m_GPR[_inst.RS];
			SetCarry(0);
		}
		else
		{
			m_GPR[_inst.RA] = (u32)((s32)m_GPR[_inst.RS] >> amount);
			if (m_GPR[_inst.RS] & 0x80000000)
				SetCarry(1);
			else
				SetCarry(0);
		}
	}

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::srawix(UGeckoInstruction _inst)
{
	int amount = _inst.SH;

	if (amount != 0)
	{
		s32 rrs = m_GPR[_inst.RS];
		m_GPR[_inst.RA] = rrs >> amount;

		if ((rrs < 0) && (rrs << (32 - amount)))
			SetCarry(1);
		else
			SetCarry(0);
	}
	else
	{
		SetCarry(0);
		m_GPR[_inst.RA] = m_GPR[_inst.RS];
	}

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::srwx(UGeckoInstruction _inst)
{
	u32 amount = m_GPR[_inst.RB];
	m_GPR[_inst.RA] = (amount & 0x20) ? 0 : (m_GPR[_inst.RS] >> (amount & 0x1f));

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::tw(UGeckoInstruction _inst)  
{
	s32 a = m_GPR[_inst.RA];
	s32 b = m_GPR[_inst.RB];
	s32 TO = _inst.TO;

	ERROR_LOG(POWERPC, "tw rA %0x rB %0x TO %0x", a, b, TO);

	if (   ((a < b) && (TO & 0x10))
		|| ((a > b) && (TO & 0x08))
		|| ((a ==b) && (TO & 0x04))
		|| (((u32)a <(u32)b) && (TO & 0x02))
		|| (((u32)a >(u32)b) && (TO & 0x01)))
	{
		Common::AtomicOr(PowerPC::ppcState.Exceptions, EXCEPTION_PROGRAM);
		PowerPC::CheckExceptions();
		m_EndBlock = true; // Dunno about this
	}
}

void Interpreter::xorx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RA] = m_GPR[_inst.RS] ^ m_GPR[_inst.RB];

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RA]);
}

void Interpreter::addx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RD] = m_GPR[_inst.RA] + m_GPR[_inst.RB];

	if (_inst.OE) PanicAlert("OE: addx");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::addcx(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	m_GPR[_inst.RD] = a + b;
	SetCarry(Helper_Carry(a,b));

	if (_inst.OE) PanicAlert("OE: addcx");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::addex(UGeckoInstruction _inst)
{
	int carry = GetCarry();
	int a = m_GPR[_inst.RA];
	int b = m_GPR[_inst.RB];
	m_GPR[_inst.RD] = a + b + carry;
	SetCarry(Helper_Carry(a, b) || (carry != 0 && Helper_Carry(a + b, carry)));

	if (_inst.OE) PanicAlert("OE: addex");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::addmex(UGeckoInstruction _inst)
{
	int carry = GetCarry();
	int a = m_GPR[_inst.RA];
	m_GPR[_inst.RD] = a + carry - 1;
	SetCarry(Helper_Carry(a, carry - 1));

	if (_inst.OE) PanicAlert("OE: addmex");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::addzex(UGeckoInstruction _inst)
{
	int carry = GetCarry();
	int a = m_GPR[_inst.RA];
	m_GPR[_inst.RD] = a + carry;
	SetCarry(Helper_Carry(a, carry));

	if (_inst.OE) PanicAlert("OE: addzex");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::divwx(UGeckoInstruction _inst)
{
	s32 a = m_GPR[_inst.RA];
	s32 b = m_GPR[_inst.RB];
	if (b == 0 || ((u32)a == 0x80000000 && b == -1))
	{
		if (_inst.OE)
			// should set OV
			PanicAlert("OE: divwx");
		if (((u32)a & 0x80000000) && b == 0)
			m_GPR[_inst.RD] = -1;
		else
			m_GPR[_inst.RD] = 0;
	}
	else
		m_GPR[_inst.RD] = (u32)(a / b);

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}


void Interpreter::divwux(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];

	if (b == 0)
	{
		if (_inst.OE)
			// should set OV
			PanicAlert("OE: divwux");
		m_GPR[_inst.RD] = 0;
	}
	else
		m_GPR[_inst.RD] = a / b;

	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::mulhwx(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	u32 d = (u32)((u64)(((s64)(s32)a * (s64)(s32)b) ) >> 32);  // This can be done better. Not in plain C/C++ though.
	m_GPR[_inst.RD] = d;
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::mulhwux(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	u32 d = (u32)(((u64)a * (u64)b) >> 32);
	m_GPR[_inst.RD] = d;
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::mullwx(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	u32 d = (u32)((s32)a * (s32)b);
	m_GPR[_inst.RD] = d;

	if (_inst.OE) PanicAlert("OE: mullwx");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::negx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RD] = (~m_GPR[_inst.RA]) + 1;
	if (m_GPR[_inst.RD] == 0x80000000)
	{
		if (_inst.OE) PanicAlert("OE: negx");
	}
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::subfx(UGeckoInstruction _inst)
{
	m_GPR[_inst.RD] = m_GPR[_inst.RB] - m_GPR[_inst.RA];

	if (_inst.OE) PanicAlert("OE: subfx");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::subfcx(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	m_GPR[_inst.RD] = b - a;
	SetCarry(a == 0 || Helper_Carry(b, 0-a));

	if (_inst.OE) PanicAlert("OE: subfcx");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

void Interpreter::subfex(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	u32 b = m_GPR[_inst.RB];
	int carry = GetCarry();
	m_GPR[_inst.RD] = (~a) + b + carry;
	SetCarry(Helper_Carry(~a, b) || Helper_Carry((~a) + b, carry));

	if (_inst.OE) PanicAlert("OE: subfex");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

// sub from minus one
void Interpreter::subfmex(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	int carry = GetCarry();
	m_GPR[_inst.RD] = (~a) + carry - 1;
	SetCarry(Helper_Carry(~a, carry - 1));

	if (_inst.OE) PanicAlert("OE: subfmex");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}

// sub from zero
void Interpreter::subfzex(UGeckoInstruction _inst)
{
	u32 a = m_GPR[_inst.RA];
	int carry = GetCarry();
	m_GPR[_inst.RD] = (~a) + carry;
	SetCarry(Helper_Carry(~a, carry));

	if (_inst.OE) PanicAlert("OE: subfzex");
	if (_inst.Rc) Helper_UpdateCR0(m_GPR[_inst.RD]);
}
