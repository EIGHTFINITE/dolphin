// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/GekkoDisassembler.h"

#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/Debugger/Debugger_SymbolMap.h"
#include "Core/Debugger/PPCDebugInterface.h"
#include "Core/HW/DSP.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/JitCommon/JitBase.h"

std::string PPCDebugInterface::Disassemble(unsigned int address)
{
	// PowerPC::HostRead_U32 seemed to crash on shutdown
	if (!IsAlive())
		return "";

	if (Core::GetState() == Core::CORE_PAUSE)
	{
		if (!PowerPC::HostIsRAMAddress(address))
		{
			return "(No RAM here)";
		}

		u32 op = PowerPC::HostRead_Instruction(address);
		std::string disasm = GekkoDisassembler::Disassemble(op, address);

		UGeckoInstruction inst;
		inst.hex = PowerPC::HostRead_U32(address);

		if (inst.OPCD == 1)
		{
			disasm += " (hle)";
		}

		return disasm;
	}
	else
	{
		return "<unknown>";
	}
}

void PPCDebugInterface::GetRawMemoryString(int memory, unsigned int address, char *dest, int max_size)
{
	if (IsAlive())
	{
		if (memory || PowerPC::HostIsRAMAddress(address))
		{
			snprintf(dest, max_size, "%08X%s", ReadExtraMemory(memory, address), memory ? " (ARAM)" : "");
		}
		else
		{
			strcpy(dest, memory ? "--ARAM--" : "--------");
		}
	}
	else
	{
		strcpy(dest, "<unknwn>");  // bad spelling - 8 chars
	}
}

unsigned int PPCDebugInterface::ReadMemory(unsigned int address)
{
	return PowerPC::HostRead_U32(address);
}

unsigned int PPCDebugInterface::ReadExtraMemory(int memory, unsigned int address)
{
	switch (memory)
	{
	case 0:
		return PowerPC::HostRead_U32(address);
	case 1:
		return (DSP::ReadARAM(address)     << 24) |
		       (DSP::ReadARAM(address + 1) << 16) |
		       (DSP::ReadARAM(address + 2) << 8) |
		       (DSP::ReadARAM(address + 3));
	default:
		return 0;
	}
}

unsigned int PPCDebugInterface::ReadInstruction(unsigned int address)
{
	return PowerPC::HostRead_Instruction(address);
}

bool PPCDebugInterface::IsAlive()
{
	return Core::IsRunning();
}

bool PPCDebugInterface::IsBreakpoint(unsigned int address)
{
	return PowerPC::breakpoints.IsAddressBreakPoint(address);
}

void PPCDebugInterface::SetBreakpoint(unsigned int address)
{
	PowerPC::breakpoints.Add(address);
}

void PPCDebugInterface::ClearBreakpoint(unsigned int address)
{
	PowerPC::breakpoints.Remove(address);
}

void PPCDebugInterface::ClearAllBreakpoints()
{
	PowerPC::breakpoints.Clear();
}

void PPCDebugInterface::ToggleBreakpoint(unsigned int address)
{
	if (PowerPC::breakpoints.IsAddressBreakPoint(address))
		PowerPC::breakpoints.Remove(address);
	else
		PowerPC::breakpoints.Add(address);
}

void PPCDebugInterface::AddWatch(unsigned int address)
{
	PowerPC::watches.Add(address);
}

void PPCDebugInterface::ClearAllMemChecks()
{
	PowerPC::memchecks.Clear();
}

bool PPCDebugInterface::IsMemCheck(unsigned int address)
{
	return (Memory::AreMemoryBreakpointsActivated() &&
	        PowerPC::memchecks.GetMemCheck(address));
}

void PPCDebugInterface::ToggleMemCheck(unsigned int address)
{
	if (Memory::AreMemoryBreakpointsActivated() &&
	    !PowerPC::memchecks.GetMemCheck(address))
	{
		// Add Memory Check
		TMemCheck MemCheck;
		MemCheck.StartAddress = address;
		MemCheck.EndAddress = address;
		MemCheck.OnRead = true;
		MemCheck.OnWrite = true;

		MemCheck.Log = true;
		MemCheck.Break = true;

		PowerPC::memchecks.Add(MemCheck);
	}
	else
		PowerPC::memchecks.Remove(address);
}

void PPCDebugInterface::InsertBLR(unsigned int address, unsigned int value)
{
	PowerPC::HostWrite_U32(value, address);
}


// =======================================================
// Separate the blocks with colors.
// -------------
int PPCDebugInterface::GetColor(unsigned int address)
{
	if (!IsAlive())
		return 0xFFFFFF;
	if (!PowerPC::HostIsRAMAddress(address))
		return 0xeeeeee;
	static const int colors[6] =
	{
		0xd0FFFF,  // light cyan
		0xFFd0d0,  // light red
		0xd8d8FF,  // light blue
		0xFFd0FF,  // light purple
		0xd0FFd0,  // light green
		0xFFFFd0,  // light yellow
	};
	Symbol *symbol = g_symbolDB.GetSymbolFromAddr(address);
	if (!symbol)
		return 0xFFFFFF;
	if (symbol->type != Symbol::SYMBOL_FUNCTION)
		return 0xEEEEFF;
	return colors[symbol->index % 6];
}
// =============


std::string PPCDebugInterface::GetDescription(unsigned int address)
{
	return g_symbolDB.GetDescription(address);
}

unsigned int PPCDebugInterface::GetPC()
{
	return PowerPC::ppcState.pc;
}

void PPCDebugInterface::SetPC(unsigned int address)
{
	PowerPC::ppcState.pc = address;
}

void PPCDebugInterface::RunToBreakpoint()
{

}
