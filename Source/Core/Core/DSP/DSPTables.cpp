// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// Additional copyrights go to Duddie (c) 2005 (duddie@walla.com)

#include "Common/CommonTypes.h"

#include "Core/DSP/DSPEmitter.h"
#include "Core/DSP/DSPInterpreter.h"
#include "Core/DSP/DSPIntExtOps.h"
#include "Core/DSP/DSPTables.h"

void nop(const UDSPInstruction opc)
{
	// The real nop is 0. Anything else is bad.
	if (opc)
	{
		ERROR_LOG(DSPLLE, "LLE: Unrecognized opcode 0x%04x", opc);
	}
}

const DSPOPCTemplate opcodes[] =
{
	//                                                         # of parameters----+   {type, size, loc, lshift, mask}                                                               branch        reads PC       // instruction approximation
	// name      opcode  mask    interpreter function     JIT function    size-V  V   param 1                       param 2                       param 3                    extendable    uncond.       updates SR
	{"NOP",      0x0000, 0xfffc, nop,                     &DSPEmitter::nop,    1, 0, {},                                                                                     false, false, false, false, false}, // no operation

	{"DAR",      0x0004, 0xfffc, DSPInterpreter::dar,     &DSPEmitter::dar,    1, 1, {{P_REG, 1, 0, 0, 0x0003}},                                                             false, false, false, false, false}, // $arD--
	{"IAR",      0x0008, 0xfffc, DSPInterpreter::iar,     &DSPEmitter::iar,    1, 1, {{P_REG, 1, 0, 0, 0x0003}},                                                             false, false, false, false, false}, // $arD++
	{"SUBARN",   0x000c, 0xfffc, DSPInterpreter::subarn,  &DSPEmitter::subarn, 1, 1, {{P_REG, 1, 0, 0, 0x0003}},                                                             false, false, false, false, false}, // $arD -= $ixS
	{"ADDARN",   0x0010, 0xfff0, DSPInterpreter::addarn,  &DSPEmitter::addarn, 1, 2, {{P_REG, 1, 0, 0, 0x0003},     {P_REG04, 1, 0, 2, 0x000c}},                             false, false, false, false, false}, // $arD += $ixS

	{"HALT",     0x0021, 0xffff, DSPInterpreter::halt,    &DSPEmitter::halt,   1, 0, {},                                                                                     false, true, true, false, false}, // halt until reset

	{"RETGE",    0x02d0, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if greater or equal
	{"RETL",     0x02d1, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if less
	{"RETG",     0x02d2, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if greater
	{"RETLE",    0x02d3, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if less or equal
	{"RETNZ",    0x02d4, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if not zero
	{"RETZ",     0x02d5, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if zero
	{"RETNC",    0x02d6, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if not carry
	{"RETC",     0x02d7, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if carry
	{"RETx8",    0x02d8, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if TODO
	{"RETx9",    0x02d9, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if TODO
	{"RETxA",    0x02da, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if TODO
	{"RETxB",    0x02db, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if TODO
	{"RETLNZ",   0x02dc, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if logic not zero
	{"RETLZ",    0x02dd, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if logic zero
	{"RETO",     0x02de, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, false, true, false}, // return if overflow
	{"RET",      0x02df, 0xffff, DSPInterpreter::ret,     &DSPEmitter::ret,    1, 0, {},                                                                                     false, true, true, false, false}, // unconditional return

	{"RTI",      0x02ff, 0xffff, DSPInterpreter::rti,     &DSPEmitter::rti,    1, 0, {},                                                                                     false, true, true, false, false}, // return from interrupt

	{"CALLGE",   0x02b0, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if greater or equal
	{"CALLL",    0x02b1, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if less
	{"CALLG",    0x02b2, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if greater
	{"CALLLE",   0x02b3, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if less or equal
	{"CALLNZ",   0x02b4, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if not zero
	{"CALLZ",    0x02b5, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if zero
	{"CALLNC",   0x02b6, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if not carry
	{"CALLC",    0x02b7, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if carry
	{"CALLx8",   0x02b8, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if TODO
	{"CALLx9",   0x02b9, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if TODO
	{"CALLxA",   0x02ba, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if TODO
	{"CALLxB",   0x02bb, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if TODO
	{"CALLLNZ",  0x02bc, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if logic not zero
	{"CALLLZ",   0x02bd, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if logic zero
	{"CALLO",    0x02be, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // call if overflow
	{"CALL",     0x02bf, 0xffff, DSPInterpreter::call,    &DSPEmitter::call,   2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, true, true, false},  // unconditional call

	{"IFGE",     0x0270, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if greater or equal
	{"IFL",      0x0271, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if less
	{"IFG",      0x0272, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if greater
	{"IFLE",     0x0273, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if less or equal
	{"IFNZ",     0x0274, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if not zero
	{"IFZ",      0x0275, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if zero
	{"IFNC",     0x0276, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if not carry
	{"IFC",      0x0277, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if carry
	{"IFx8",     0x0278, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if TODO
	{"IFx9",     0x0279, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if TODO
	{"IFxA",     0x027a, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if TODO
	{"IFxB",     0x027b, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if TODO
	{"IFLNZ",    0x027c, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if logic not zero
	{"IFLZ",     0x027d, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if logic zero
	{"IFO",      0x027e, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, false, true, false}, // if overflow
	{"IF",       0x027f, 0xffff, DSPInterpreter::ifcc,    &DSPEmitter::ifcc,   1, 0, {},                                                                                     false, true, true, true, false},  // what is this, I don't even...

	{"JGE",      0x0290, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if greater or equal
	{"JL",       0x0291, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if less
	{"JG",       0x0292, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if greater
	{"JLE",      0x0293, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if less or equal
	{"JNZ",      0x0294, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if not zero
	{"JZ",       0x0295, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if zero
	{"JNC",      0x0296, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if not carry
	{"JC",       0x0297, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if carry
	{"JMPx8",    0x0298, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if TODO
	{"JMPx9",    0x0299, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if TODO
	{"JMPxA",    0x029a, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if TODO
	{"JMPxB",    0x029b, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if TODO
	{"JLNZ",     0x029c, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if logic not zero
	{"JLZ",      0x029d, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if logic zero
	{"JO",       0x029e, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, false, true, false}, // jump if overflow
	{"JMP",      0x029f, 0xffff, DSPInterpreter::jcc,     &DSPEmitter::jcc,    2, 1, {{P_ADDR_I, 2, 1, 0, 0xffff}},                                                          false, true, true, true, false},  // unconditional jump

	{"JRGE",     0x1700, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if greater or equal
	{"JRL",      0x1701, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if less
	{"JRG",      0x1702, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if greater
	{"JRLE",     0x1703, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if less or equal
	{"JRNZ",     0x1704, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if not zero
	{"JRZ",      0x1705, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if zero
	{"JRNC",     0x1706, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if not carry
	{"JRC",      0x1707, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if carry
	{"JMPRx8",   0x1708, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if TODO
	{"JMPRx9",   0x1709, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if TODO
	{"JMPRxA",   0x170a, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if TODO
	{"JMPRxB",   0x170b, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if TODO
	{"JRLNZ",    0x170c, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if logic not zero
	{"JRLZ",     0x170d, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if logic zero
	{"JRO",      0x170e, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, false, false}, // jump to $R if overflow
	{"JMPR",     0x170f, 0xff1f, DSPInterpreter::jmprcc,  &DSPEmitter::jmprcc, 1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, true, false, false},  // jump to $R

	{"CALLRGE",  0x1710, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if greater or equal
	{"CALLRL",   0x1711, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if less
	{"CALLRG",   0x1712, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if greater
	{"CALLRLE",  0x1713, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if less or equal
	{"CALLRNZ",  0x1714, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if not zero
	{"CALLRZ",   0x1715, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if zero
	{"CALLRNC",  0x1716, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if not carry
	{"CALLRC",   0x1717, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if carry
	{"CALLRx8",  0x1718, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if TODO
	{"CALLRx9",  0x1719, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if TODO
	{"CALLRxA",  0x171a, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if TODO
	{"CALLRxB",  0x171b, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if TODO
	{"CALLRLNZ", 0x171c, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if logic not zero
	{"CALLRLZ",  0x171d, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if logic zero
	{"CALLRO",   0x171e, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, false, true, false}, // call $R if overflow
	{"CALLR",    0x171f, 0xff1f, DSPInterpreter::callr,   &DSPEmitter::callr,  1, 1, {{P_REG, 1, 0, 5, 0x00e0}},                                                             false, true, true, true, false},  // call $R

	{"SBCLR",    0x1200, 0xff00, DSPInterpreter::sbclr,   &DSPEmitter::sbclr,  1, 1, {{P_IMM, 1, 0, 0, 0x0007}},                                                             false, false, false, false, false}, // $sr &= ~(I + 6)
	{"SBSET",    0x1300, 0xff00, DSPInterpreter::sbset,   &DSPEmitter::sbset,  1, 1, {{P_IMM, 1, 0, 0, 0x0007}},                                                             false, false, false, false, false}, // $sr |= (I + 6)

	{"LSL",      0x1400, 0xfec0, DSPInterpreter::lsl,     &DSPEmitter::lsl,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_IMM, 1, 0, 0, 0x003f}},                               false, false, false, false, true}, // $acR <<= I
	{"LSR",      0x1440, 0xfec0, DSPInterpreter::lsr,     &DSPEmitter::lsr,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_IMM, 1, 0, 0, 0x003f}},                               false, false, false, false, true}, // $acR >>= I (shifting in zeros)
	{"ASL",      0x1480, 0xfec0, DSPInterpreter::asl,     &DSPEmitter::asl,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_IMM, 1, 0, 0, 0x003f}},                               false, false, false, false, true}, // $acR <<= I
	{"ASR",      0x14c0, 0xfec0, DSPInterpreter::asr,     &DSPEmitter::asr,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_IMM, 1, 0, 0, 0x003f}},                               false, false, false, false, true}, // $acR >>= I (shifting in sign bits)

	// these two were discovered by ector
	{"LSRN",     0x02ca, 0xffff, DSPInterpreter::lsrn,    &DSPEmitter::lsrn,   1, 0, {},                                                                                     false, false, false, false, true}, // $ac0 >>=/<<= $ac1.m[0-6]
	{"ASRN",     0x02cb, 0xffff, DSPInterpreter::asrn,    &DSPEmitter::asrn,   1, 0, {},                                                                                     false, false, false, false, true}, // $ac0 >>=/<<= $ac1.m[0-6] (arithmetic)

	{"LRI",      0x0080, 0xffe0, DSPInterpreter::lri,     &DSPEmitter::lri,    2, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, false}, // $D = I
	{"LR",       0x00c0, 0xffe0, DSPInterpreter::lr,      &DSPEmitter::lr,     2, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_MEM, 2, 1, 0, 0xffff}},                               false, false, false, true, false}, // $D = MEM[M]
	{"SR",       0x00e0, 0xffe0, DSPInterpreter::sr,      &DSPEmitter::sr,     2, 2, {{P_MEM, 2, 1, 0, 0xffff},     {P_REG, 1, 0, 0, 0x001f}},                               false, false, false, true, false}, // MEM[M] = $S

	{"MRR",      0x1c00, 0xfc00, DSPInterpreter::mrr,     &DSPEmitter::mrr,    1, 2, {{P_REG, 1, 0, 5, 0x03e0},     {P_REG, 1, 0, 0, 0x001f}},                               false, false, false, false, false}, // $D = $S

	{"SI",       0x1600, 0xff00, DSPInterpreter::si,      &DSPEmitter::si,     2, 2, {{P_MEM, 1, 0, 0, 0x00ff},     {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, false}, // MEM[M] = I

	{"ADDIS",    0x0400, 0xfe00, DSPInterpreter::addis,   &DSPEmitter::addis,  1, 2, {{P_ACCM,  1, 0, 8, 0x0100},   {P_IMM, 1, 0, 0, 0x00ff}},                               false, false, false, false, true}, // $acD.hm += I
	{"CMPIS",    0x0600, 0xfe00, DSPInterpreter::cmpis,   &DSPEmitter::cmpis,  1, 2, {{P_ACCM,  1, 0, 8, 0x0100},   {P_IMM, 1, 0, 0, 0x00ff}},                               false, false, false, false, true}, // FLAGS($acD - I)
	{"LRIS",     0x0800, 0xf800, DSPInterpreter::lris,    &DSPEmitter::lris,   1, 2, {{P_REG18, 1, 0, 8, 0x0700},   {P_IMM, 1, 0, 0, 0x00ff}},                               false, false, false, false, true}, // $(D+24) = I

	{"ADDI",     0x0200, 0xfeff, DSPInterpreter::addi,    &DSPEmitter::addi,   2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // $acD.hm += I
	{"XORI",     0x0220, 0xfeff, DSPInterpreter::xori,    &DSPEmitter::xori,   2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // $acD.m ^= I
	{"ANDI",     0x0240, 0xfeff, DSPInterpreter::andi,    &DSPEmitter::andi,   2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // $acD.m &= I
	{"ORI",      0x0260, 0xfeff, DSPInterpreter::ori,     &DSPEmitter::ori,    2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // $acD.m |= I
	{"CMPI",     0x0280, 0xfeff, DSPInterpreter::cmpi,    &DSPEmitter::cmpi,   2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // FLAGS(($acD.hm - I) | $acD.l)

	{"ANDF",     0x02a0, 0xfeff, DSPInterpreter::andf,    &DSPEmitter::andf,   2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // $sr.LZ = ($acD.m & I) == 0 ? 1 : 0
	{"ANDCF",    0x02c0, 0xfeff, DSPInterpreter::andcf,   &DSPEmitter::andcf,  2, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_IMM, 2, 1, 0, 0xffff}},                               false, false, false, true, true}, // $sr.LZ = ($acD.m & I) == I ? 1 : 0

	{"ILRR",     0x0210, 0xfefc, DSPInterpreter::ilrr,    &DSPEmitter::ilrr,   1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_PRG, 1, 0, 0, 0x0003}},                               false, false, false, false, false}, // $acD.m = IMEM[$arS]
	{"ILRRD",    0x0214, 0xfefc, DSPInterpreter::ilrrd,   &DSPEmitter::ilrrd,  1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_PRG, 1, 0, 0, 0x0003}},                               false, false, false, false, false}, // $acD.m = IMEM[$arS--]
	{"ILRRI",    0x0218, 0xfefc, DSPInterpreter::ilrri,   &DSPEmitter::ilrri,  1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_PRG, 1, 0, 0, 0x0003}},                               false, false, false, false, false}, // $acD.m = IMEM[$arS++]
	{"ILRRN",    0x021c, 0xfefc, DSPInterpreter::ilrrn,   &DSPEmitter::ilrrn,  1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_PRG, 1, 0, 0, 0x0003}},                               false, false, false, false, false}, // $acD.m = IMEM[$arS]; $arS += $ixS

	// LOOPS
	{"LOOP",     0x0040, 0xffe0, DSPInterpreter::loop,    &DSPEmitter::loop,   1, 1, {{P_REG, 1, 0, 0, 0x001f}},                                                             false, true, true, true, false}, // run next instruction $R times
	{"BLOOP",    0x0060, 0xffe0, DSPInterpreter::bloop,   &DSPEmitter::bloop,  2, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_ADDR_I, 2, 1, 0, 0xffff}},                            false, true, true, true, false}, // COMEFROM addr $R times
	{"LOOPI",    0x1000, 0xff00, DSPInterpreter::loopi,   &DSPEmitter::loopi,  1, 1, {{P_IMM, 1, 0, 0, 0x00ff}},                                                             false, true, true, true, false}, // run next instruction I times
	{"BLOOPI",   0x1100, 0xff00, DSPInterpreter::bloopi,  &DSPEmitter::bloopi, 2, 2, {{P_IMM, 1, 0, 0, 0x00ff},     {P_ADDR_I, 2, 1, 0, 0xffff}},                            false, true, true, true, false}, // COMEFROM addr I times

	// load and store value pointed by indexing reg and increment; LRR/SRR variants
	{"LRR",      0x1800, 0xff80, DSPInterpreter::lrr,     &DSPEmitter::lrr,    1, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_PRG, 1, 0, 5, 0x0060}},                               false, false, false, false, false}, // $D = MEM[$arS]
	{"LRRD",     0x1880, 0xff80, DSPInterpreter::lrrd,    &DSPEmitter::lrrd,   1, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_PRG, 1, 0, 5, 0x0060}},                               false, false, false, false, false}, // $D = MEM[$arS--]
	{"LRRI",     0x1900, 0xff80, DSPInterpreter::lrri,    &DSPEmitter::lrri,   1, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_PRG, 1, 0, 5, 0x0060}},                               false, false, false, false, false}, // $D = MEM[$arS++]
	{"LRRN",     0x1980, 0xff80, DSPInterpreter::lrrn,    &DSPEmitter::lrrn,   1, 2, {{P_REG, 1, 0, 0, 0x001f},     {P_PRG, 1, 0, 5, 0x0060}},                               false, false, false, false, false}, // $D = MEM[$arS]; $arS += $ixS

	{"SRR",      0x1a00, 0xff80, DSPInterpreter::srr,     &DSPEmitter::srr,    1, 2, {{P_PRG, 1, 0, 5, 0x0060},     {P_REG, 1, 0, 0, 0x001f}},                               false, false, false, false, false}, // MEM[$arD] = $S
	{"SRRD",     0x1a80, 0xff80, DSPInterpreter::srrd,    &DSPEmitter::srrd,   1, 2, {{P_PRG, 1, 0, 5, 0x0060},     {P_REG, 1, 0, 0, 0x001f}},                               false, false, false, false, false}, // MEM[$arD--] = $S
	{"SRRI",     0x1b00, 0xff80, DSPInterpreter::srri,    &DSPEmitter::srri,   1, 2, {{P_PRG, 1, 0, 5, 0x0060},     {P_REG, 1, 0, 0, 0x001f}},                               false, false, false, false, false}, // MEM[$arD++] = $S
	{"SRRN",     0x1b80, 0xff80, DSPInterpreter::srrn,    &DSPEmitter::srrn,   1, 2, {{P_PRG, 1, 0, 5, 0x0060},     {P_REG, 1, 0, 0, 0x001f}},                               false, false, false, false, false}, // MEM[$arD] = $S; $arD += $ixD

	//2
	{"LRS",      0x2000, 0xf800, DSPInterpreter::lrs,     &DSPEmitter::lrs,    1, 2, {{P_REG18, 1, 0, 8, 0x0700},   {P_MEM, 1, 0, 0, 0x00ff}},                               false, false, false, false, false}, // $(D+24) = MEM[($cr[0-7] << 8) | I]
	{"SRS",      0x2800, 0xf800, DSPInterpreter::srs,     &DSPEmitter::srs,    1, 2, {{P_MEM,   1, 0, 0, 0x00ff},   {P_REG18, 1, 0, 8, 0x0700}},                             false, false, false, false, false}, // MEM[($cr[0-7] << 8) | I] = $(S+24)

// opcodes that can be extended

	//3 - main opcode defined by 9 bits, extension defined by last 7 bits!!
	{"XORR",     0x3000, 0xfc80, DSPInterpreter::xorr,    &DSPEmitter::xorr,   1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_REG1A,  1, 0, 9, 0x0200}},                            true, false, false, false, true}, // $acD.m ^= $axS.h
	{"ANDR",     0x3400, 0xfc80, DSPInterpreter::andr,    &DSPEmitter::andr,   1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_REG1A,  1, 0, 9, 0x0200}},                            true, false, false, false, true}, // $acD.m &= $axS.h
	{"ORR",      0x3800, 0xfc80, DSPInterpreter::orr,     &DSPEmitter::orr,    1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_REG1A,  1, 0, 9, 0x0200}},                            true, false, false, false, true}, // $acD.m |= $axS.h
	{"ANDC",     0x3c00, 0xfe80, DSPInterpreter::andc,    &DSPEmitter::andc,   1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_ACCM_D, 1, 0, 8, 0x0100}},                            true, false, false, false, true}, // $acD.m &= $ac(1-D).m
	{"ORC",      0x3e00, 0xfe80, DSPInterpreter::orc,     &DSPEmitter::orc,    1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_ACCM_D, 1, 0, 8, 0x0100}},                            true, false, false, false, true}, // $acD.m |= $ac(1-D).m
	{"XORC",     0x3080, 0xfe80, DSPInterpreter::xorc,    &DSPEmitter::xorc,   1, 2, {{P_ACCM, 1, 0, 8, 0x0100},    {P_ACCM_D, 1, 0, 8, 0x0100}},                            true, false, false, false, true}, // $acD.m ^= $ac(1-D).m
	{"NOT",      0x3280, 0xfe80, DSPInterpreter::notc,    &DSPEmitter::notc,   1, 1, {{P_ACCM, 1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acD.m = ~$acD.m
	{"LSRNRX",   0x3480, 0xfc80, DSPInterpreter::lsrnrx,  &DSPEmitter::lsrnrx, 1, 2, {{P_ACC,  1, 0, 8, 0x0100},    {P_REG1A,  1, 0, 9, 0x0200}},                            true, false, false, false, true}, // $acD >>=/<<= $axS.h[0-6]
	{"ASRNRX",   0x3880, 0xfc80, DSPInterpreter::asrnrx,  &DSPEmitter::asrnrx, 1, 2, {{P_ACC,  1, 0, 8, 0x0100},    {P_REG1A,  1, 0, 9, 0x0200}},                            true, false, false, false, true}, // $acD >>=/<<= $axS.h[0-6] (arithmetic)
	{"LSRNR",    0x3c80, 0xfe80, DSPInterpreter::lsrnr,   &DSPEmitter::lsrnr,  1, 2, {{P_ACC,  1, 0, 8, 0x0100},    {P_ACCM_D, 1, 0, 8, 0x0100}},                            true, false, false, false, true}, // $acD >>=/<<= $ac(1-D).m[0-6]
	{"ASRNR",    0x3e80, 0xfe80, DSPInterpreter::asrnr,   &DSPEmitter::asrnr,  1, 2, {{P_ACC,  1, 0, 8, 0x0100},    {P_ACCM_D, 1, 0, 8, 0x0100}},                            true, false, false, false, true}, // $acD >>=/<<= $ac(1-D).m[0-6] (arithmetic)

	//4
	{"ADDR",     0x4000, 0xf800, DSPInterpreter::addr,    &DSPEmitter::addr,   1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_REG18, 1, 0, 9, 0x0600}},                             true, false, false, false, true}, // $acD += $(S+24)
	{"ADDAX",    0x4800, 0xfc00, DSPInterpreter::addax,   &DSPEmitter::addax,  1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_AX, 1, 0, 9, 0x0200}},                                true, false, false, false, true}, // $acD += $axS
	{"ADD",      0x4c00, 0xfe00, DSPInterpreter::add,     &DSPEmitter::add,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_ACC_D, 1, 0, 8, 0x0100}},                             true, false, false, false, true}, // $acD += $ac(1-D)
	{"ADDP",     0x4e00, 0xfe00, DSPInterpreter::addp,    &DSPEmitter::addp,   1, 1, {{P_ACC, 1, 0, 8, 0x0100}},                                                             true, false, false, false, true}, // $acD += $prod

	//5
	{"SUBR",     0x5000, 0xf800, DSPInterpreter::subr,    &DSPEmitter::subr,   1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_REG18, 1, 0, 9, 0x0600}},                             true, false, false, false, true}, // $acD -= $(S+24)
	{"SUBAX",    0x5800, 0xfc00, DSPInterpreter::subax,   &DSPEmitter::subax,  1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_AX, 1, 0, 9, 0x0200}},                                true, false, false, false, true}, // $acD -= $axS
	{"SUB",      0x5c00, 0xfe00, DSPInterpreter::sub,     &DSPEmitter::sub,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_ACC_D, 1, 0, 8, 0x0100}},                             true, false, false, false, true}, // $acD -= $ac(1-D)
	{"SUBP",     0x5e00, 0xfe00, DSPInterpreter::subp,    &DSPEmitter::subp,   1, 1, {{P_ACC, 1, 0, 8, 0x0100}},                                                             true, false, false, false, true}, // $acD -= $prod

	//6
	{"MOVR",     0x6000, 0xf800, DSPInterpreter::movr,    &DSPEmitter::movr,   1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_REG18, 1, 0, 9, 0x0600}},                             true, false, false, false, true}, // $acD.hm = $(S+24); $acD.l = 0
	{"MOVAX",    0x6800, 0xfc00, DSPInterpreter::movax,   &DSPEmitter::movax,  1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_AX, 1, 0, 9, 0x0200}},                                true, false, false, false, true}, // $acD = $axS
	{"MOV",      0x6c00, 0xfe00, DSPInterpreter::mov,     &DSPEmitter::mov,    1, 2, {{P_ACC, 1, 0, 8, 0x0100},     {P_ACC_D, 1, 0, 8, 0x0100}},                             true, false, false, false, true}, // $acD = $ax(1-D)
	{"MOVP",     0x6e00, 0xfe00, DSPInterpreter::movp,    &DSPEmitter::movp,   1, 1, {{P_ACC, 1, 0, 8, 0x0100}},                                                             true, false, false, false, true}, // $acD = $prod

	//7
	{"ADDAXL",   0x7000, 0xfc00, DSPInterpreter::addaxl,  &DSPEmitter::addaxl, 1, 2, {{P_ACC,  1, 0, 8, 0x0100},    {P_REG18, 1, 0, 9, 0x0200}},                             true, false, false, false, true}, // $acD += $axS.l
	{"INCM",     0x7400, 0xfe00, DSPInterpreter::incm,    &DSPEmitter::incm,   1, 1, {{P_ACCM, 1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acsD++
	{"INC",      0x7600, 0xfe00, DSPInterpreter::inc,     &DSPEmitter::inc,    1, 1, {{P_ACC,  1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acD++
	{"DECM",     0x7800, 0xfe00, DSPInterpreter::decm,    &DSPEmitter::decm,   1, 1, {{P_ACCM, 1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acsD--
	{"DEC",      0x7a00, 0xfe00, DSPInterpreter::dec,     &DSPEmitter::dec,    1, 1, {{P_ACC,  1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acD--
	{"NEG",      0x7c00, 0xfe00, DSPInterpreter::neg,     &DSPEmitter::neg,    1, 1, {{P_ACC,  1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acD = -$acD
	{"MOVNP",    0x7e00, 0xfe00, DSPInterpreter::movnp,   &DSPEmitter::movnp,  1, 1, {{P_ACC,  1, 0, 8, 0x0100}},                                                            true, false, false, false, true}, // $acD = -$prod

	//8
	{"NX",       0x8000, 0xf700, DSPInterpreter::nx,      &DSPEmitter::nx,     1, 0, {},                                                                                     true, false, false, false, false}, // extendable nop
	{"CLR",      0x8100, 0xf700, DSPInterpreter::clr,     &DSPEmitter::clr,    1, 1, {{P_ACC,   1, 0, 11, 0x0800}},                                                          true, false, false, false, true},  // $acD = 0
	{"CMP",      0x8200, 0xff00, DSPInterpreter::cmp,     &DSPEmitter::cmp,    1, 0, {},                                                                                     true, false, false, false, true},  // FLAGS($ac0 - $ac1)
	{"MULAXH",   0x8300, 0xff00, DSPInterpreter::mulaxh,  &DSPEmitter::mulaxh, 1, 0, {},                                                                                     true, false, false, false, true},  // $prod = $ax0.h * $ax0.h
	{"CLRP",     0x8400, 0xff00, DSPInterpreter::clrp,    &DSPEmitter::clrp,   1, 0, {},                                                                                     true, false, false, false, true},  // $prod = 0
	{"TSTPROD",  0x8500, 0xff00, DSPInterpreter::tstprod, &DSPEmitter::tstprod,1, 0, {},                                                                                     true, false, false, false, true},  // FLAGS($prod)
	{"TSTAXH",   0x8600, 0xfe00, DSPInterpreter::tstaxh,  &DSPEmitter::tstaxh, 1, 1, {{P_REG1A, 1, 0, 8, 0x0100}},                                                           true, false, false, false, true},  // FLAGS($axR.h)
	{"M2",       0x8a00, 0xff00, DSPInterpreter::srbith,  &DSPEmitter::srbith, 1, 0, {},                                                                                     true, false, false, false, false}, // enable "$prod *= 2" after every multiplication
	{"M0",       0x8b00, 0xff00, DSPInterpreter::srbith,  &DSPEmitter::srbith, 1, 0, {},                                                                                     true, false, false, false, false}, // disable "$prod *= 2" after every multiplication
	{"CLR15",    0x8c00, 0xff00, DSPInterpreter::srbith,  &DSPEmitter::srbith, 1, 0, {},                                                                                     true, false, false, false, false}, // set normal multiplication
	{"SET15",    0x8d00, 0xff00, DSPInterpreter::srbith,  &DSPEmitter::srbith, 1, 0, {},                                                                                     true, false, false, false, false}, // set unsigned multiplication in MUL
	{"SET16",    0x8e00, 0xff00, DSPInterpreter::srbith,  &DSPEmitter::srbith, 1, 0, {},                                                                                     true, false, false, false, false}, // set 16 bit sign extension width
	{"SET40",    0x8f00, 0xff00, DSPInterpreter::srbith,  &DSPEmitter::srbith, 1, 0, {},                                                                                     true, false, false, false, false}, // set 40 bit sign extension width

	//9
	{"MUL",      0x9000, 0xf700, DSPInterpreter::mul,     &DSPEmitter::mul,    1, 2, {{P_REG18, 1, 0, 11, 0x0800},  {P_REG1A, 1, 0, 11, 0x0800}},                            true, false, false, false, true}, // $prod = $axS.l * $axS.h
	{"ASR16",    0x9100, 0xf700, DSPInterpreter::asr16,   &DSPEmitter::asr16,  1, 1, {{P_ACC,   1, 0, 11, 0x0800}},                                                          true, false, false, false, true}, // $acD >>= 16 (shifting in sign bits)
	{"MULMVZ",   0x9200, 0xf600, DSPInterpreter::mulmvz,  &DSPEmitter::mulmvz, 1, 3, {{P_REG18, 1, 0, 11, 0x0800},  {P_REG1A, 1, 0, 11, 0x0800},  {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR.hm = $prod.hm; $acR.l = 0; $prod = $axS.l * $axS.h
	{"MULAC",    0x9400, 0xf600, DSPInterpreter::mulac,   &DSPEmitter::mulac,  1, 3, {{P_REG18, 1, 0, 11, 0x0800},  {P_REG1A, 1, 0, 11, 0x0800},  {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR += $prod; $prod = $axS.l * $axS.h
	{"MULMV",    0x9600, 0xf600, DSPInterpreter::mulmv,   &DSPEmitter::mulmv,  1, 3, {{P_REG18, 1, 0, 11, 0x0800},  {P_REG1A, 1, 0, 11, 0x0800},  {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR = $prod; $prod = $axS.l * $axS.h

	//a-b
	{"MULX",     0xa000, 0xe700, DSPInterpreter::mulx,    &DSPEmitter::mulx,   1, 2, {{P_REGM18, 1, 0, 11, 0x1000}, {P_REGM19, 1, 0, 10, 0x0800}},                           true, false, false, false, true}, // $prod = $ax0.S * $ax1.T
	{"ABS",      0xa100, 0xf700, DSPInterpreter::abs,     &DSPEmitter::abs,    1, 1, {{P_ACC,    1, 0, 11, 0x0800}},                                                         true, false, false, false, true}, // $acD = abs($acD)
	{"MULXMVZ",  0xa200, 0xe600, DSPInterpreter::mulxmvz, &DSPEmitter::mulxmvz,1, 3, {{P_REGM18, 1, 0, 11, 0x1000}, {P_REGM19, 1, 0, 10, 0x0800}, {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR.hm = $prod.hm; $acR.l = 0; $prod = $ax0.S * $ax1.T
	{"MULXAC",   0xa400, 0xe600, DSPInterpreter::mulxac,  &DSPEmitter::mulxac, 1, 3, {{P_REGM18, 1, 0, 11, 0x1000}, {P_REGM19, 1, 0, 10, 0x0800}, {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR += $prod; $prod = $ax0.S * $ax1.T
	{"MULXMV",   0xa600, 0xe600, DSPInterpreter::mulxmv,  &DSPEmitter::mulxmv, 1, 3, {{P_REGM18, 1, 0, 11, 0x1000}, {P_REGM19, 1, 0, 10, 0x0800}, {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR = $prod; $prod = $ax0.S * $ax1.T
	{"TST",      0xb100, 0xf700, DSPInterpreter::tst,     &DSPEmitter::tst,    1, 1, {{P_ACC,    1, 0, 11, 0x0800}},                                                         true, false, false, false, true}, // FLAGS($acR)

	//c-d
	{"MULC",     0xc000, 0xe700, DSPInterpreter::mulc,    &DSPEmitter::mulc,   1, 2, {{P_ACCM, 1, 0, 12, 0x1000},   {P_REG1A, 1, 0, 11, 0x0800}},                            true, false, false, false, true}, // $prod = $acS.m * $axS.h
	{"CMPAR",    0xc100, 0xe700, DSPInterpreter::cmpar,   &DSPEmitter::cmpar,  1, 2, {{P_ACC,  1, 0, 11, 0x0800},   {P_REG1A, 1, 0, 12, 0x1000}},                            true, false, false, false, true}, // FLAGS($acS - axR.h)
	{"MULCMVZ",  0xc200, 0xe600, DSPInterpreter::mulcmvz, &DSPEmitter::mulcmvz,1, 3, {{P_ACCM, 1, 0, 12, 0x1000},   {P_REG1A, 1, 0, 11, 0x0800},  {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR.hm, $acR.l, $prod = $prod.hm, 0, $acS.m * $axS.h
	{"MULCAC",   0xc400, 0xe600, DSPInterpreter::mulcac,  &DSPEmitter::mulcac, 1, 3, {{P_ACCM, 1, 0, 12, 0x1000},   {P_REG1A, 1, 0, 11, 0x0800},  {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR, $prod = $acR + $prod, $acS.m * $axS.h
	{"MULCMV",   0xc600, 0xe600, DSPInterpreter::mulcmv,  &DSPEmitter::mulcmv, 1, 3, {{P_ACCM, 1, 0, 12, 0x1000},   {P_REG1A, 1, 0, 11, 0x0800},  {P_ACC, 1, 0, 8, 0x0100}}, true, false, false, false, true}, // $acR, $prod = $prod, $acS.m * $axS.h

	//e
	{"MADDX",    0xe000, 0xfc00, DSPInterpreter::maddx,   &DSPEmitter::maddx,  1, 2, {{P_REGM18, 1, 0, 8, 0x0200},  {P_REGM19, 1, 0, 7, 0x0100}},                            true, false, false, false, true}, // $prod += $ax0.S * $ax1.T
	{"MSUBX",    0xe400, 0xfc00, DSPInterpreter::msubx,   &DSPEmitter::msubx,  1, 2, {{P_REGM18, 1, 0, 8, 0x0200},  {P_REGM19, 1, 0, 7, 0x0100}},                            true, false, false, false, true}, // $prod -= $ax0.S * $ax1.T
	{"MADDC",    0xe800, 0xfc00, DSPInterpreter::maddc,   &DSPEmitter::maddc,  1, 2, {{P_ACCM,   1, 0, 9, 0x0200},  {P_REG19, 1, 0, 7, 0x0100}},                             true, false, false, false, true}, // $prod += $acS.m * $axT.h
	{"MSUBC",    0xec00, 0xfc00, DSPInterpreter::msubc,   &DSPEmitter::msubc,  1, 2, {{P_ACCM,   1, 0, 9, 0x0200},  {P_REG19, 1, 0, 7, 0x0100}},                             true, false, false, false, true}, // $prod -= $acS.m * $axT.h

	//f
	{"LSL16",    0xf000, 0xfe00, DSPInterpreter::lsl16,   &DSPEmitter::lsl16,  1, 1, {{P_ACC,   1, 0,  8, 0x0100}},                                                          true, false, false, false, true}, // $acR <<= 16
	{"MADD",     0xf200, 0xfe00, DSPInterpreter::madd,    &DSPEmitter::madd,   1, 2, {{P_REG18, 1, 0,  8, 0x0100},  {P_REG1A, 1, 0, 8, 0x0100}},                             true, false, false, false, true}, // $prod += $axS.l * $axS.h
	{"LSR16",    0xf400, 0xfe00, DSPInterpreter::lsr16,   &DSPEmitter::lsr16,  1, 1, {{P_ACC,   1, 0,  8, 0x0100}},                                                          true, false, false, false, true}, // $acR >>= 16
	{"MSUB",     0xf600, 0xfe00, DSPInterpreter::msub,    &DSPEmitter::msub,   1, 2, {{P_REG18, 1, 0,  8, 0x0100},  {P_REG1A, 1, 0, 8, 0x0100}},                             true, false, false, false, true}, // $prod -= $axS.l * $axS.h
	{"ADDPAXZ",  0xf800, 0xfc00, DSPInterpreter::addpaxz, &DSPEmitter::addpaxz,1, 2, {{P_ACC,   1, 0,  9, 0x0200},  {P_AX, 1, 0, 8, 0x0100}},                                true, false, false, false, true}, // $acD.hm = $prod.hm + $ax.h; $acD.l = 0
	{"CLRL",     0xfc00, 0xfe00, DSPInterpreter::clrl,    &DSPEmitter::clrl,   1, 1, {{P_ACCL,  1, 0, 11, 0x0800}},                                                          true, false, false, false, true}, // $acR.l = 0
	{"MOVPZ",    0xfe00, 0xfe00, DSPInterpreter::movpz,   &DSPEmitter::movpz,  1, 1, {{P_ACC,   1, 0,  8, 0x0100}},                                                          true, false, false, false, true}, // $acD.hm = $prod.hm; $acD.l = 0
};

const DSPOPCTemplate cw =
	{"CW",     0x0000, 0x0000, nop, nullptr, 1, 1, {{P_VAL, 2, 0, 0, 0xffff}}, false, false, false, false, false};

// extended opcodes

const DSPOPCTemplate opcodes_ext[] =
{
	{"XXX",    0x0000, 0x00fc, DSPInterpreter::Ext::nop,  &DSPEmitter::nop,  1, 1, {{P_VAL, 1, 0, 0, 0x00ff}}, false, false, false, false, false}, // no operation

	{"DR",     0x0004, 0x00fc, DSPInterpreter::Ext::dr,   &DSPEmitter::dr,   1, 1, {{P_REG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $arR--
	{"IR",     0x0008, 0x00fc, DSPInterpreter::Ext::ir,   &DSPEmitter::ir,   1, 1, {{P_REG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $arR++
	{"NR",     0x000c, 0x00fc, DSPInterpreter::Ext::nr,   &DSPEmitter::nr,   1, 1, {{P_REG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $arR += $ixR
	{"MV",     0x0010, 0x00f0, DSPInterpreter::Ext::mv,   &DSPEmitter::mv,   1, 2, {{P_REG18, 1, 0, 2, 0x000c}, {P_REG1C, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $(D+24) = $(S+28)

	{"S",      0x0020, 0x00e4, DSPInterpreter::Ext::s,    &DSPEmitter::s,    1, 2, {{P_PRG, 1, 0, 0, 0x0003}, {P_REG1C, 1, 0, 3, 0x0018}}, false, false, false, false, false}, // MEM[$D++] = $(S+28)
	{"SN",     0x0024, 0x00e4, DSPInterpreter::Ext::sn,   &DSPEmitter::sn,   1, 2, {{P_PRG, 1, 0, 0, 0x0003}, {P_REG1C, 1, 0, 3, 0x0018}}, false, false, false, false, false}, // MEM[$D] = $(D+28); $D += $(D+4)

	{"L",      0x0040, 0x00c4, DSPInterpreter::Ext::l,    &DSPEmitter::l,    1, 2, {{P_REG18, 1, 0, 3, 0x0038}, {P_PRG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $(D+24) = MEM[$S++]
	{"LN",     0x0044, 0x00c4, DSPInterpreter::Ext::ln,   &DSPEmitter::ln,   1, 2, {{P_REG18, 1, 0, 3, 0x0038}, {P_PRG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $(D+24) = MEM[$S]; $S += $(S+4)

	{"LS",     0x0080, 0x00ce, DSPInterpreter::Ext::ls,   &DSPEmitter::ls,   1, 2, {{P_REG18, 1, 0, 4, 0x0030}, {P_ACCM, 1, 0, 0, 0x0001}}, false, false, false, false, false}, // $(D+24) = MEM[$ar0++]; MEM[$ar3++] = $acS.m
	{"SL",     0x0082, 0x00ce, DSPInterpreter::Ext::sl,   &DSPEmitter::sl,   1, 2, {{P_ACCM, 1, 0, 0, 0x0001}, {P_REG18, 1, 0, 4, 0x0030}}, false, false, false, false, false}, // MEM[$ar0++] = $acS.m; $(D+24) = MEM[$ar3++]
	{"LSN",    0x0084, 0x00ce, DSPInterpreter::Ext::lsn,  &DSPEmitter::lsn,  1, 2, {{P_REG18, 1, 0, 4, 0x0030}, {P_ACCM, 1, 0, 0, 0x0001}}, false, false, false, false, false}, // $(D+24) = MEM[$ar0]; MEM[$ar3++] = $acS.m; $ar0 += $ix0
	{"SLN",    0x0086, 0x00ce, DSPInterpreter::Ext::sln,  &DSPEmitter::sln,  1, 2, {{P_ACCM, 1, 0, 0, 0x0001}, {P_REG18, 1, 0, 4, 0x0030}}, false, false, false, false, false}, // MEM[$ar0] = $acS.m; $(D+24) = MEM[$ar3++]; $ar0 += $ix0
	{"LSM",    0x0088, 0x00ce, DSPInterpreter::Ext::lsm,  &DSPEmitter::lsm,  1, 2, {{P_REG18, 1, 0, 4, 0x0030}, {P_ACCM, 1, 0, 0, 0x0001}}, false, false, false, false, false}, // $(D+24) = MEM[$ar0++]; MEM[$ar3] = $acS.m; $ar3 += $ix3
	{"SLM",    0x008a, 0x00ce, DSPInterpreter::Ext::slm,  &DSPEmitter::slm,  1, 2, {{P_ACCM, 1, 0, 0, 0x0001}, {P_REG18, 1, 0, 4, 0x0030}}, false, false, false, false, false}, // MEM[$ar0++] = $acS.m; $(D+24) = MEM[$ar3]; $ar3 += $ix3
	{"LSNM",   0x008c, 0x00ce, DSPInterpreter::Ext::lsnm, &DSPEmitter::lsnm, 1, 2, {{P_REG18, 1, 0, 4, 0x0030}, {P_ACCM, 1, 0, 0, 0x0001}}, false, false, false, false, false}, // $(D+24) = MEM[$ar0]; MEM[$ar3] = $acS.m; $ar0 += $ix0; $ar3 += $ix3
	{"SLNM",   0x008e, 0x00ce, DSPInterpreter::Ext::slnm, &DSPEmitter::slnm, 1, 2, {{P_ACCM, 1, 0, 0, 0x0001}, {P_REG18, 1, 0, 4, 0x0030}}, false, false, false, false, false}, // MEM[$ar0] = $acS.m; $(D+24) = MEM[$ar3]; $ar0 += $ix0; $ar3 += $ix3

	{"LDAX",   0x00c3, 0x00cf, DSPInterpreter::Ext::ldax,   &DSPEmitter::ldax,   1, 2, {{P_AX, 1, 0, 4, 0x0010}, {P_PRG, 1, 0, 5, 0x0020}}, false, false, false, false, false}, // $axR.h = MEM[$arS++]; $axR.l = MEM[$ar3++]
	{"LDAXN",  0x00c7, 0x00cf, DSPInterpreter::Ext::ldaxn,  &DSPEmitter::ldaxn,  1, 2, {{P_AX, 1, 0, 4, 0x0010}, {P_PRG, 1, 0, 5, 0x0020}}, false, false, false, false, false}, // $axR.h = MEM[$arS]; $axR.l = MEM[$ar3++]; $arS += $ixS
	{"LDAXM",  0x00cb, 0x00cf, DSPInterpreter::Ext::ldaxm,  &DSPEmitter::ldaxm,  1, 2, {{P_AX, 1, 0, 4, 0x0010}, {P_PRG, 1, 0, 5, 0x0020}}, false, false, false, false, false}, // $axR.h = MEM[$arS++]; $axR.l = MEM[$ar3]; $ar3 += $ix3
	{"LDAXNM", 0x00cf, 0x00cf, DSPInterpreter::Ext::ldaxnm, &DSPEmitter::ldaxnm, 1, 2, {{P_AX, 1, 0, 4, 0x0010}, {P_PRG, 1, 0, 5, 0x0020}}, false, false, false, false, false}, // $axR.h = MEM[$arS]; $axR.l = MEM[$ar3]; $arS += $ixS; $ar3 += $ix3

	{"LD",     0x00c0, 0x00cc, DSPInterpreter::Ext::ld,   &DSPEmitter::ld,   1, 3, {{P_REGM18, 1, 0, 4, 0x0020}, {P_REGM19, 1, 0, 3, 0x0010}, {P_PRG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $ax0.D = MEM[$arS++]; $ax1.R = MEM[$ar3++]
	{"LDN",    0x00c4, 0x00cc, DSPInterpreter::Ext::ldn,  &DSPEmitter::ldn,  1, 3, {{P_REGM18, 1, 0, 4, 0x0020}, {P_REGM19, 1, 0, 3, 0x0010}, {P_PRG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $ax0.D = MEM[$arS]; $ax1.R = MEM[$ar3++]; $arS += $ixS
	{"LDM",    0x00c8, 0x00cc, DSPInterpreter::Ext::ldm,  &DSPEmitter::ldm,  1, 3, {{P_REGM18, 1, 0, 4, 0x0020}, {P_REGM19, 1, 0, 3, 0x0010}, {P_PRG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $ax0.D = MEM[$arS++]; $ax1.R = MEM[$ar3]; $ar3 += $ix3
	{"LDNM",   0x00cc, 0x00cc, DSPInterpreter::Ext::ldnm, &DSPEmitter::ldnm, 1, 3, {{P_REGM18, 1, 0, 4, 0x0020}, {P_REGM19, 1, 0, 3, 0x0010}, {P_PRG, 1, 0, 0, 0x0003}}, false, false, false, false, false}, // $ax0.D = MEM[$arS]; $ax1.R = MEM[$ar3]; $arS += $ixS; $ar3 += $ix3
};

const int opcodes_size = sizeof(opcodes) / sizeof(DSPOPCTemplate);
const int opcodes_ext_size = sizeof(opcodes_ext) / sizeof(DSPOPCTemplate);

const pdlabel_t pdlabels[] =
{
	{0xffa0, "COEF_A1_0", "COEF_A1_0",},
	{0xffa1, "COEF_A2_0", "COEF_A2_0",},
	{0xffa2, "COEF_A1_1", "COEF_A1_1",},
	{0xffa3, "COEF_A2_1", "COEF_A2_1",},
	{0xffa4, "COEF_A1_2", "COEF_A1_2",},
	{0xffa5, "COEF_A2_2", "COEF_A2_2",},
	{0xffa6, "COEF_A1_3", "COEF_A1_3",},
	{0xffa7, "COEF_A2_3", "COEF_A2_3",},
	{0xffa8, "COEF_A1_4", "COEF_A1_4",},
	{0xffa9, "COEF_A2_4", "COEF_A2_4",},
	{0xffaa, "COEF_A1_5", "COEF_A1_5",},
	{0xffab, "COEF_A2_5", "COEF_A2_5",},
	{0xffac, "COEF_A1_6", "COEF_A1_6",},
	{0xffad, "COEF_A2_6", "COEF_A2_6",},
	{0xffae, "COEF_A1_7", "COEF_A1_7",},
	{0xffaf, "COEF_A2_7", "COEF_A2_7",},

	{0xffb0, "0xffb0", nullptr,},
	{0xffb1, "0xffb1", nullptr,},
	{0xffb2, "0xffb2", nullptr,},
	{0xffb3, "0xffb3", nullptr,},
	{0xffb4, "0xffb4", nullptr,},
	{0xffb5, "0xffb5", nullptr,},
	{0xffb6, "0xffb6", nullptr,},
	{0xffb7, "0xffb7", nullptr,},
	{0xffb8, "0xffb8", nullptr,},
	{0xffb9, "0xffb9", nullptr,},
	{0xffba, "0xffba", nullptr,},
	{0xffbb, "0xffbb", nullptr,},
	{0xffbc, "0xffbc", nullptr,},
	{0xffbd, "0xffbd", nullptr,},
	{0xffbe, "0xffbe", nullptr,},
	{0xffbf, "0xffbf", nullptr,},

	{0xffc0, "0xffc0", nullptr,},
	{0xffc1, "0xffc1", nullptr,},
	{0xffc2, "0xffc2", nullptr,},
	{0xffc3, "0xffc3", nullptr,},
	{0xffc4, "0xffc4", nullptr,},
	{0xffc5, "0xffc5", nullptr,},
	{0xffc6, "0xffc6", nullptr,},
	{0xffc7, "0xffc7", nullptr,},
	{0xffc8, "0xffc8", nullptr,},
	{0xffc9, "DSCR", "DSP DMA Control Reg",},
	{0xffca, "0xffca", nullptr,},
	{0xffcb, "DSBL", "DSP DMA Block Length",},
	{0xffcc, "0xffcc", nullptr,},
	{0xffcd, "DSPA", "DSP DMA DMEM Address",},
	{0xffce, "DSMAH", "DSP DMA Mem Address H",},
	{0xffcf, "DSMAL", "DSP DMA Mem Address L",},

	{0xffd0, "0xffd0",nullptr,},
	{0xffd1, "SampleFormat", "SampleFormat",},
	{0xffd2, "0xffd2",nullptr,},
	{0xffd3, "UnkZelda", "Unk Zelda reads/writes from/to it",},
	{0xffd4, "ACSAH", "Accelerator start address H",},
	{0xffd5, "ACSAL", "Accelerator start address L",},
	{0xffd6, "ACEAH", "Accelerator end address H",},
	{0xffd7, "ACEAL", "Accelerator end address L",},
	{0xffd8, "ACCAH", "Accelerator current address H",},
	{0xffd9, "ACCAL", "Accelerator current address L",},
	{0xffda, "pred_scale", "pred_scale",},
	{0xffdb, "yn1", "yn1",},
	{0xffdc, "yn2", "yn2",},
	{0xffdd, "ARAM", "Direct Read from ARAM (uses ADPCM)",},
	{0xffde, "GAIN", "Gain",},
	{0xffdf, "0xffdf", nullptr,},

	{0xffe0, "0xffe0",nullptr,},
	{0xffe1, "0xffe1",nullptr,},
	{0xffe2, "0xffe2",nullptr,},
	{0xffe3, "0xffe3",nullptr,},
	{0xffe4, "0xffe4",nullptr,},
	{0xffe5, "0xffe5",nullptr,},
	{0xffe6, "0xffe6",nullptr,},
	{0xffe7, "0xffe7",nullptr,},
	{0xffe8, "0xffe8",nullptr,},
	{0xffe9, "0xffe9",nullptr,},
	{0xffea, "0xffea",nullptr,},
	{0xffeb, "0xffeb",nullptr,},
	{0xffec, "0xffec",nullptr,},
	{0xffed, "0xffed",nullptr,},
	{0xffee, "0xffee",nullptr,},
	{0xffef, "AMDM", "ARAM DMA Request Mask",},

	{0xfff0, "0xfff0",nullptr,},
	{0xfff1, "0xfff1",nullptr,},
	{0xfff2, "0xfff2",nullptr,},
	{0xfff3, "0xfff3",nullptr,},
	{0xfff4, "0xfff4",nullptr,},
	{0xfff5, "0xfff5",nullptr,},
	{0xfff6, "0xfff6",nullptr,},
	{0xfff7, "0xfff7",nullptr,},
	{0xfff8, "0xfff8",nullptr,},
	{0xfff9, "0xfff9",nullptr,},
	{0xfffa, "0xfffa",nullptr,},
	{0xfffb, "DIRQ", "DSP IRQ Request",},
	{0xfffc, "DMBH", "DSP Mailbox H",},
	{0xfffd, "DMBL", "DSP Mailbox L",},
	{0xfffe, "CMBH", "CPU Mailbox H",},
	{0xffff, "CMBL", "CPU Mailbox L",},
};

const u32 pdlabels_size = sizeof(pdlabels) / sizeof(pdlabel_t);

const pdlabel_t regnames[] =
{
	{0x00, "AR0",       "Addr Reg 00",},
	{0x01, "AR1",       "Addr Reg 01",},
	{0x02, "AR2",       "Addr Reg 02",},
	{0x03, "AR3",       "Addr Reg 03",},
	{0x04, "IX0",       "Index Reg 0",},
	{0x05, "IX1",       "Index Reg 1",},
	{0x06, "IX2",       "Index Reg 2",},
	{0x07, "IX3",       "Index Reg 3",},
	{0x08, "WR0",       "Wrapping Register 0",},
	{0x09, "WR1",       "Wrapping Register 1",},
	{0x0a, "WR2",       "Wrapping Register 2",},
	{0x0b, "WR3",       "Wrapping Register 3",},
	{0x0c, "ST0",       "Call stack",},
	{0x0d, "ST1",       "Data stack",},
	{0x0e, "ST2",       "Loop addr stack",},
	{0x0f, "ST3",       "Loop counter",},
	{0x10, "AC0.H",     "Accu High 0",},
	{0x11, "AC1.H",     "Accu High 1",},
	{0x12, "CR",        "Config Register",},
	{0x13, "SR",        "Special Register",},
	{0x14, "PROD.L",    "Prod L",},
	{0x15, "PROD.M1",   "Prod M1",},
	{0x16, "PROD.H",    "Prod H",},
	{0x17, "PROD.M2",   "Prod M2",},
	{0x18, "AX0.L",     "Extra Accu L 0",},
	{0x19, "AX1.L",     "Extra Accu L 1",},
	{0x1a, "AX0.H",     "Extra Accu H 0",},
	{0x1b, "AX1.H",     "Extra Accu H 1",},
	{0x1c, "AC0.L",     "Accu Low 0",},
	{0x1d, "AC1.L",     "Accu Low 1",},
	{0x1e, "AC0.M",     "Accu Mid 0",},
	{0x1f, "AC1.M",     "Accu Mid 1",},

	// To resolve combined register names.
	{0x20, "ACC0",      "Accu Full 0",},
	{0x21, "ACC1",      "Accu Full 1",},
	{0x22, "AX0",       "Extra Accu 0",},
	{0x23, "AX1",       "Extra Accu 1",},
};

const DSPOPCTemplate *opTable[OPTABLE_SIZE];
const DSPOPCTemplate *extOpTable[EXT_OPTABLE_SIZE];
u16 writeBackLog[WRITEBACKLOGSIZE];
int writeBackLogIdx[WRITEBACKLOGSIZE];

const char* pdname(u16 val)
{
	static char tmpstr[12]; // nasty

	for (const pdlabel_t& pdlabel : pdlabels)
	{
		if (pdlabel.addr == val)
			return pdlabel.name;
	}

	sprintf(tmpstr, "0x%04x", val);
	return tmpstr;
}

const char *pdregname(int val)
{
	return regnames[val].name;
}

const char *pdregnamelong(int val)
{
	return regnames[val].description;
}

const DSPOPCTemplate *GetOpTemplate(const UDSPInstruction &inst)
{
	return opTable[inst];
}


// This function could use the above GetOpTemplate, but then we'd lose the
// nice property that it catches colliding op masks.
void InitInstructionTable()
{
	// ext op table
	for (int i = 0; i < EXT_OPTABLE_SIZE; i++)
	{
		extOpTable[i] = &cw;

		for (const DSPOPCTemplate& ext : opcodes_ext)
		{
			u16 mask = ext.opcode_mask;
			if ((mask & i) == ext.opcode)
			{
				if (extOpTable[i] == &cw)
				{
					extOpTable[i] = &ext;
				}
				else
				{
					//if the entry already in the table
					//is a strict subset, allow it
					if ((extOpTable[i]->opcode_mask | ext.opcode_mask) != extOpTable[i]->opcode_mask)
					{
						ERROR_LOG(DSPLLE, "opcode ext table place %d already in use by %s when inserting %s", i, extOpTable[i]->name, ext.name);
					}
				}
			}
		}
	}

	// op table
	for (const DSPOPCTemplate*& opcode : opTable)
	{
		opcode = &cw;
	}

	for (int i = 0; i < OPTABLE_SIZE; i++)
	{
		for (const DSPOPCTemplate& opcode : opcodes)
		{
			u16 mask = opcode.opcode_mask;
			if ((mask & i) == opcode.opcode)
			{
				if (opTable[i] == &cw)
					opTable[i] = &opcode;
				else
					ERROR_LOG(DSPLLE, "opcode table place %d already in use for %s", i, opcode.name);
			}
		}
	}

	for (int& elem : writeBackLogIdx)
	{
		elem = -1;
	}
}
