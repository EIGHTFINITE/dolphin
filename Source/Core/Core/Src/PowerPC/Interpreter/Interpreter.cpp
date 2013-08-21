// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../../HW/Memmap.h"
#include "../../HW/CPU.h"
#include "../../Host.h"
#include "../PPCTables.h"
#include "Interpreter.h"
#include "../../Debugger/Debugger_SymbolMap.h"
#include "../../CoreTiming.h"
#include "../../ConfigManager.h"
#include "PowerPCDisasm.h"
#include "../../IPC_HLE/WII_IPC_HLE.h"
#include "Atomic.h"
#include "HLE/HLE.h"


namespace {
	u32 last_pc;
}

bool Interpreter::m_EndBlock;

// function tables
Interpreter::_interpreterInstruction Interpreter::m_opTable[64];
Interpreter::_interpreterInstruction Interpreter::m_opTable4[1024];
Interpreter::_interpreterInstruction Interpreter::m_opTable19[1024];
Interpreter::_interpreterInstruction Interpreter::m_opTable31[1024];
Interpreter::_interpreterInstruction Interpreter::m_opTable59[32];
Interpreter::_interpreterInstruction Interpreter::m_opTable63[1024];

void Interpreter::RunTable4(UGeckoInstruction _inst)  {m_opTable4 [_inst.SUBOP10](_inst);}
void Interpreter::RunTable19(UGeckoInstruction _inst) {m_opTable19[_inst.SUBOP10](_inst);}
void Interpreter::RunTable31(UGeckoInstruction _inst) {m_opTable31[_inst.SUBOP10](_inst);}
void Interpreter::RunTable59(UGeckoInstruction _inst) {m_opTable59[_inst.SUBOP5 ](_inst);}
void Interpreter::RunTable63(UGeckoInstruction _inst) {m_opTable63[_inst.SUBOP10](_inst);}

void Interpreter::Init()
{
	g_bReserve = false;
	m_EndBlock = false;
}

void Interpreter::Shutdown()
{
}

static void patches()
{
/*	if (Memory::Read_U16(0x90000880) == 0x130b)
	{
		PanicAlert("Memory::Read_U16(0x900008800) == 0x130b");
	}
*/
/*	if (PC == 0x80074cd4)
	{
		u16 command = Common::swap16(Memory::Read_U16(PowerPC::ppcState.gpr[3] + 8));
		if (command == 0x0b13)
		{
			PanicAlert("command: %x", command);
		}
	}*/
}

int startTrace = 0;

void Trace( UGeckoInstruction &instCode )
{
	char reg[25]="";
	std::string regs = "";
	for (int i=0; i<32; i++)
	{
		sprintf(reg, "r%02d: %08x ", i, PowerPC::ppcState.gpr[i]);
		regs.append(reg);
	}

	char freg[25]="";
	std::string fregs = "";
	for (int i=0; i<32; i++)
	{
		sprintf(freg, "f%02d: %08llx %08llx ", i, PowerPC::ppcState.ps[i][0], PowerPC::ppcState.ps[i][1]);
		fregs.append(freg);
	}

	char ppcInst[256];
	DisassembleGekko(instCode.hex, PC, ppcInst, 256);

	DEBUG_LOG(POWERPC, "INTER PC: %08x SRR0: %08x SRR1: %08x CRfast: %02x%02x%02x%02x%02x%02x%02x%02x FPSCR: %08x MSR: %08x LR: %08x %s %s %08x %s", PC, SRR0, SRR1, PowerPC::ppcState.cr_fast[0], PowerPC::ppcState.cr_fast[1], PowerPC::ppcState.cr_fast[2], PowerPC::ppcState.cr_fast[3], PowerPC::ppcState.cr_fast[4], PowerPC::ppcState.cr_fast[5], PowerPC::ppcState.cr_fast[6], PowerPC::ppcState.cr_fast[7], PowerPC::ppcState.fpscr, PowerPC::ppcState.msr, PowerPC::ppcState.spr[8], regs.c_str(), fregs.c_str(), instCode.hex, ppcInst);
}

int Interpreter::SingleStepInner(void)
{
	static UGeckoInstruction instCode;

	u32 function = m_EndBlock ? HLE::GetFunctionIndex(PC) : 0; // Check for HLE functions after branches
	if (function != 0)
	{
		int type = HLE::GetFunctionTypeByIndex(function);
		if (type == HLE::HLE_HOOK_START || type == HLE::HLE_HOOK_REPLACE)
		{
			int flags = HLE::GetFunctionFlagsByIndex(function);
			if (HLE::IsEnabled(flags))
			{
				HLEFunction(function);
			}
		}
	}
	else
	{
		NPC = PC + sizeof(UGeckoInstruction);
		instCode.hex = Memory::Read_Opcode(PC);

		// Uncomment to trace the interpreter
		//if ((PC & 0xffffff)>=0x0ab54c && (PC & 0xffffff)<=0x0ab624)
		//	startTrace = 1;
		//else
		//	startTrace = 0;

		if (startTrace)
		{
			Trace(instCode);
		}

		if (instCode.hex != 0)
		{
			UReg_MSR& msr = (UReg_MSR&)MSR;
			if (msr.FP)  //If FPU is enabled, just execute
			{
				m_opTable[instCode.OPCD](instCode);
				if (PowerPC::ppcState.Exceptions & EXCEPTION_DSI)
				{
					PowerPC::CheckExceptions();
					m_EndBlock = true;
				}
			}
			else
			{
				// check if we have to generate a FPU unavailable exception
				if (!PPCTables::UsesFPU(instCode))
				{
					m_opTable[instCode.OPCD](instCode);
					if (PowerPC::ppcState.Exceptions & EXCEPTION_DSI)
					{
						PowerPC::CheckExceptions();
						m_EndBlock = true;
					}
				}
				else
				{
					Common::AtomicOr(PowerPC::ppcState.Exceptions, EXCEPTION_FPU_UNAVAILABLE);
					PowerPC::CheckExceptions();
					m_EndBlock = true;
				}
			}
		}
		else
		{
			// Memory exception on instruction fetch
			PowerPC::CheckExceptions();
			m_EndBlock = true;
		}
	}
	last_pc = PC;
	PC = NPC;
	
#if defined(_DEBUG) || defined(DEBUGFAST)
	if (PowerPC::ppcState.gpr[1] == 0)
	{
		WARN_LOG(POWERPC, "%i Corrupt stack", PowerPC::ppcState.DebugCount);
	}
	PowerPC::ppcState.DebugCount++;
#endif
	patches();

	GekkoOPInfo *opinfo = GetOpInfo(instCode);
	return opinfo->numCyclesMinusOne + 1;
}

void Interpreter::SingleStep()
{	
	SingleStepInner();
	
	CoreTiming::slicelength = 1;
	CoreTiming::downcount = 0;
	CoreTiming::Advance();

	if (PowerPC::ppcState.Exceptions)
	{
		PowerPC::CheckExceptions();
		PC = NPC;
	}
}

//#define SHOW_HISTORY
#ifdef SHOW_HISTORY
std::vector <int> PCVec;
std::vector <int> PCBlockVec;
int ShowBlocks = 30;
int ShowSteps = 300;
#endif

// FastRun - inspired by GCemu (to imitate the JIT so that they can be compared).
void Interpreter::Run()
{
	while (!PowerPC::GetState())
	{
		//we have to check exceptions at branches apparently (or maybe just rfi?)
		if (SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableDebugging)
		{
			#ifdef SHOW_HISTORY
				PCBlockVec.push_back(PC);
				if (PCBlockVec.size() > ShowBlocks)
					PCBlockVec.erase(PCBlockVec.begin());
			#endif

			// Debugging friendly version of inner loop. Tries to do the timing as similarly to the
			// JIT as possible. Does not take into account that some instructions take multiple cycles.
			while (CoreTiming::downcount > 0)
			{
				m_EndBlock = false;
				int i;
				for (i = 0; !m_EndBlock; i++)
				{
					#ifdef SHOW_HISTORY
						PCVec.push_back(PC);
						if (PCVec.size() > ShowSteps)
							PCVec.erase(PCVec.begin());
					#endif

					//2: check for breakpoint
					if (PowerPC::breakpoints.IsAddressBreakPoint(PC))
					{
						#ifdef SHOW_HISTORY
							NOTICE_LOG(POWERPC, "----------------------------");
							NOTICE_LOG(POWERPC, "Blocks:");
							for (int j = 0; j < PCBlockVec.size(); j++)
								NOTICE_LOG(POWERPC, "PC: 0x%08x", PCBlockVec.at(j));
							NOTICE_LOG(POWERPC, "----------------------------");
							NOTICE_LOG(POWERPC, "Steps:");
							for (int j = 0; j < PCVec.size(); j++)
							{
								// Write space
								if (j > 0)
									if (PCVec.at(j) != PCVec.at(j-1) + 4)
										NOTICE_LOG(POWERPC, "");

								NOTICE_LOG(POWERPC, "PC: 0x%08x", PCVec.at(j));
							}
						#endif
						INFO_LOG(POWERPC, "Hit Breakpoint - %08x", PC);
						CCPU::Break();
						if (PowerPC::breakpoints.IsTempBreakPoint(PC))
							PowerPC::breakpoints.Remove(PC);

						Host_UpdateDisasmDialog();
						return;
					}
					SingleStepInner();
				}
				CoreTiming::downcount -= i;
			}
		}
		else
		{
			// "fast" version of inner loop. well, it's not so fast.
			while (CoreTiming::downcount > 0)
			{
				m_EndBlock = false;
				int i;
				int cycles = 0;
				for (i = 0; !m_EndBlock; i++)
				{
					cycles += SingleStepInner();
				}
				CoreTiming::downcount -= cycles;
			}
		}

		CoreTiming::Advance();

		if (PowerPC::ppcState.Exceptions)
		{
			PowerPC::CheckExceptions();
			PC = NPC;
		}
	}
}

void Interpreter::unknown_instruction(UGeckoInstruction _inst)
{
	if (_inst.hex != 0)
	{
		char disasm[256];
		DisassembleGekko(Memory::ReadUnchecked_U32(last_pc), last_pc, disasm, 256);
		NOTICE_LOG(POWERPC, "Last PC = %08x : %s", last_pc, disasm);
		Dolphin_Debugger::PrintCallstack();
		_dbg_assert_msg_(POWERPC, 0, "\nIntCPU: Unknown instruction %08x at PC = %08x  last_PC = %08x  LR = %08x\n", _inst.hex, PC, last_pc, LR);
	}

}

void Interpreter::ClearCache()
{
	// Do nothing.
}

const char *Interpreter::GetName()
{
#ifdef _M_X64
		return "Interpreter64";
#else
		return "Interpreter32";
#endif
}

Interpreter *Interpreter::getInstance()
{
	static Interpreter instance;
	return &instance;
}
