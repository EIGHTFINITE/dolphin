// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "Hash.h"
#include "DSP/DSPAnalyzer.h"
#include "DSP/DSPCore.h"
#include "DSP/DSPHost.h"
#include "DSPSymbols.h"
#include "DSPLLETools.h"
#include "../DSP.h"
#include "../../ConfigManager.h"
#include "../../PowerPC/PowerPC.h"
#include "Host.h"

// The user of the DSPCore library must supply a few functions so that the
// emulation core can access the environment it runs in. If the emulation
// core isn't used, for example in an asm/disasm tool, then most of these
// can be stubbed out.

u8 DSPHost_ReadHostMemory(u32 addr)
{
	return DSP::ReadARAM(addr);
}

void DSPHost_WriteHostMemory(u8 value, u32 addr)
{
	DSP::WriteARAM(value, addr);
}

bool DSPHost_OnThread()
{
	const SCoreStartupParameter& _CoreParameter = SConfig::GetInstance().m_LocalCoreStartupParameter;
	return  _CoreParameter.bDSPThread;
}

bool DSPHost_Wii()
{
	const SCoreStartupParameter& _CoreParameter = SConfig::GetInstance().m_LocalCoreStartupParameter;
	return  _CoreParameter.bWii;
}

void DSPHost_InterruptRequest()
{
	// Fire an interrupt on the PPC ASAP.
	DSP::GenerateDSPInterruptFromDSPEmu(DSP::INT_DSP);
}

void DSPHost_CodeLoaded(const u8 *ptr, int size)
{
	g_dsp.iram_crc = HashEctor(ptr, size);

#if defined(_DEBUG) || defined(DEBUGFAST)
	DumpDSPCode(ptr, size, g_dsp.iram_crc);
#endif

	DSPSymbols::Clear();

	// Auto load text file - if none just disassemble.
	
	NOTICE_LOG(DSPLLE, "g_dsp.iram_crc: %08x", g_dsp.iram_crc);

	DSPSymbols::Clear();
	bool success = false;
	switch (g_dsp.iram_crc)
	{
		case 0x86840740: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_Zelda.txt"); break;
		case 0x42f64ac4: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_Luigi.txt"); break;
		case 0x07f88145: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_AX_07F88145.txt"); break;
		case 0x3ad3b7ac: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_AX_3AD3B7AC.txt"); break;
		case 0x3daf59b9: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_AX_3DAF59B9.txt"); break;
		case 0x4e8a8b21: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_AX_4E8A8B21.txt"); break;
		case 0xe2136399: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_AX_E2136399.txt"); break;
		case 0xdd7e72d5: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_GBA.txt"); break;
		case 0x347112BA: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_AXWii.txt"); break;
		case 0xD643001F: success = DSPSymbols::ReadAnnotatedAssembly("../../docs/DSP/DSP_UC_SuperMarioGalaxy.txt"); break;
		default: success = false; break;
	}

	if (!success)
	{
		DSPSymbols::AutoDisassembly(0x0, 0x1000);
	}

	// Always add the ROM.
	DSPSymbols::AutoDisassembly(0x8000, 0x9000);

	DSPHost_UpdateDebugger();

	if (dspjit)
		dspjit->ClearIRAM();

	DSPAnalyzer::Analyze();
}

void DSPHost_UpdateDebugger()
{
	Host_RefreshDSPDebuggerWindow();
}
