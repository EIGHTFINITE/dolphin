// Copyright 2008 Dolphin Emulator Project
// Copyright 2004 Duddie & Tratax
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <memory>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/Event.h"
#include "Common/FileUtil.h"
#include "Common/Hash.h"
#include "Common/MemoryUtil.h"

#include "Core/DSP/DSPAnalyzer.h"
#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPEmitter.h"
#include "Core/DSP/DSPHost.h"
#include "Core/DSP/DSPHWInterface.h"
#include "Core/DSP/DSPInterpreter.h"
#include "Core/DSP/DSPIntUtil.h"

SDSP g_dsp;
DSPBreakpoints g_dsp_breakpoints;
static DSPCoreState core_state = DSPCORE_STOP;
u16 g_cycles_left = 0;
bool g_init_hax = false;
std::unique_ptr<DSPEmitter> g_dsp_jit;
std::unique_ptr<DSPCaptureLogger> g_dsp_cap;
static Common::Event step_event;

// Returns false if the hash fails and the user hits "Yes"
static bool VerifyRoms()
{
	struct DspRomHashes
	{
		u32 hash_irom; // dsp_rom.bin
		u32 hash_drom; // dsp_coef.bin
	};

	static const std::array<DspRomHashes, 4> known_roms = {{
		// Official Nintendo ROM
		{ 0x66f334fe, 0xf3b93527 },

		// LM1234 replacement ROM (Zelda UCode only)
		{ 0x9c8f593c, 0x10000001 },

		// delroth's improvement on LM1234 replacement ROM (Zelda and AX only,
		// IPL/Card/GBA still broken)
		{ 0xd9907f71, 0xb019c2fb },

		// above with improved resampling coefficients
		{ 0xd9907f71, 0xdb6880c1 }
	}};

	u32 hash_irom = HashAdler32((u8*)g_dsp.irom, DSP_IROM_BYTE_SIZE);
	u32 hash_drom = HashAdler32((u8*)g_dsp.coef, DSP_COEF_BYTE_SIZE);
	int rom_idx = -1;

	for (size_t i = 0; i < known_roms.size(); ++i)
	{
		const DspRomHashes& rom = known_roms[i];
		if (hash_irom == rom.hash_irom && hash_drom == rom.hash_drom)
			rom_idx = static_cast<int>(i);
	}

	if (rom_idx < 0)
	{
		if (AskYesNoT("Your DSP ROMs have incorrect hashes.\n"
			"Would you like to stop now to fix the problem?\n"
			"If you select \"No\", audio might be garbled."))
			return false;
	}

	if (rom_idx == 1)
	{
		DSPHost::OSD_AddMessage("You are using an old free DSP ROM made by the Dolphin Team.", 6000);
		DSPHost::OSD_AddMessage("Only games using the Zelda UCode will work correctly.", 6000);
	}
	else if (rom_idx == 2 || rom_idx == 3)
	{
		DSPHost::OSD_AddMessage("You are using a free DSP ROM made by the Dolphin Team.", 8000);
		DSPHost::OSD_AddMessage("All Wii games will work correctly, and most GC games should ", 8000);
		DSPHost::OSD_AddMessage("also work fine, but the GBA/IPL/CARD UCodes will not work.\n", 8000);
	}

	return true;
}

static void DSPCore_FreeMemoryPages()
{
	FreeMemoryPages(g_dsp.irom, DSP_IROM_BYTE_SIZE);
	FreeMemoryPages(g_dsp.iram, DSP_IRAM_BYTE_SIZE);
	FreeMemoryPages(g_dsp.dram, DSP_DRAM_BYTE_SIZE);
	FreeMemoryPages(g_dsp.coef, DSP_COEF_BYTE_SIZE);
	g_dsp.irom = g_dsp.iram = g_dsp.dram = g_dsp.coef = nullptr;
}

bool DSPCore_Init(const DSPInitOptions& opts)
{
	g_dsp.step_counter = 0;
	g_cycles_left = 0;
	g_init_hax = false;

	g_dsp.irom = (u16*)AllocateMemoryPages(DSP_IROM_BYTE_SIZE);
	g_dsp.iram = (u16*)AllocateMemoryPages(DSP_IRAM_BYTE_SIZE);
	g_dsp.dram = (u16*)AllocateMemoryPages(DSP_DRAM_BYTE_SIZE);
	g_dsp.coef = (u16*)AllocateMemoryPages(DSP_COEF_BYTE_SIZE);

	memcpy(g_dsp.irom, opts.irom_contents.data(), DSP_IROM_BYTE_SIZE);
	memcpy(g_dsp.coef, opts.coef_contents.data(), DSP_COEF_BYTE_SIZE);

	// Try to load real ROM contents.
	if (!VerifyRoms())
	{
		DSPCore_FreeMemoryPages();
		return false;
	}

	memset(&g_dsp.r,0,sizeof(g_dsp.r));

	std::fill(std::begin(g_dsp.reg_stack_ptr), std::end(g_dsp.reg_stack_ptr), 0);

	for (size_t i = 0; i < ArraySize(g_dsp.reg_stack); i++)
		std::fill(std::begin(g_dsp.reg_stack[i]), std::end(g_dsp.reg_stack[i]), 0);

	// Fill IRAM with HALT opcodes.
	std::fill(g_dsp.iram, g_dsp.iram + DSP_IRAM_SIZE, 0x0021);

	// Just zero out DRAM.
	std::fill(g_dsp.dram, g_dsp.dram + DSP_DRAM_SIZE, 0);

	// Copied from a real console after the custom UCode has been loaded.
	// These are the indexing wrapping registers.
	std::fill(std::begin(g_dsp.r.wr), std::end(g_dsp.r.wr), 0xffff);

	g_dsp.r.sr |= SR_INT_ENABLE;
	g_dsp.r.sr |= SR_EXT_INT_ENABLE;

	g_dsp.cr = 0x804;
	gdsp_ifx_init();
	// Mostly keep IRAM write protected. We unprotect only when DMA-ing
	// in new ucodes.
	WriteProtectMemory(g_dsp.iram, DSP_IRAM_BYTE_SIZE, false);

	// Initialize JIT, if necessary
	if (opts.core_type == DSPInitOptions::CORE_JIT)
		g_dsp_jit = std::make_unique<DSPEmitter>();

	g_dsp_cap.reset(opts.capture_logger);

	core_state = DSPCORE_RUNNING;
	return true;
}

void DSPCore_Shutdown()
{
	if (core_state == DSPCORE_STOP)
		return;

	core_state = DSPCORE_STOP;

	g_dsp_jit.reset();

	DSPCore_FreeMemoryPages();

	g_dsp_cap.reset();
}

void DSPCore_Reset()
{
	g_dsp.pc = DSP_RESET_VECTOR;

	std::fill(std::begin(g_dsp.r.wr), std::end(g_dsp.r.wr), 0xffff);

	DSPAnalyzer::Analyze();
}

void DSPCore_SetException(u8 level)
{
	g_dsp.exceptions |= 1 << level;
}

// Notify that an external interrupt is pending (used by thread mode)
void DSPCore_SetExternalInterrupt(bool val)
{
	g_dsp.external_interrupt_waiting = val;
}

// Coming from the CPU
void DSPCore_CheckExternalInterrupt()
{
	if (! dsp_SR_is_flag_set(SR_EXT_INT_ENABLE))
		return;

	// Signal the SPU about new mail
	DSPCore_SetException(EXP_INT);

	g_dsp.cr &= ~CR_EXTERNAL_INT;
}


void DSPCore_CheckExceptions()
{
	// Early out to skip the loop in the common case.
	if (g_dsp.exceptions == 0)
		return;

	for (int i = 7; i > 0; i--)
	{
		// Seems exp int are not masked by sr_int_enable
		if (g_dsp.exceptions & (1 << i))
		{
			if (dsp_SR_is_flag_set(SR_INT_ENABLE) || (i == EXP_INT))
			{
				// store pc and sr until RTI
				dsp_reg_store_stack(DSP_STACK_C, g_dsp.pc);
				dsp_reg_store_stack(DSP_STACK_D, g_dsp.r.sr);

				g_dsp.pc = i * 2;
				g_dsp.exceptions &= ~(1 << i);
				if (i == 7)
					g_dsp.r.sr &= ~SR_EXT_INT_ENABLE;
				else
					g_dsp.r.sr &= ~SR_INT_ENABLE;
				break;
			}
			else
			{
#if defined(_DEBUG) || defined(DEBUGFAST)
				ERROR_LOG(DSPLLE, "Firing exception %d failed", i);
#endif
			}
		}
	}
}

// Delegate to JIT or interpreter as appropriate.
// Handle state changes and stepping.
int DSPCore_RunCycles(int cycles)
{
	if (g_dsp_jit)
	{
		if (g_dsp.external_interrupt_waiting)
		{
			DSPCore_CheckExternalInterrupt();
			DSPCore_CheckExceptions();
			DSPCore_SetExternalInterrupt(false);
		}

		g_cycles_left = cycles;
		DSPCompiledCode pExecAddr = (DSPCompiledCode)g_dsp_jit->enterDispatcher;
		pExecAddr();

		if (g_dsp.reset_dspjit_codespace)
			g_dsp_jit->ClearIRAMandDSPJITCodespaceReset();

		return g_cycles_left;
	}

	while (cycles > 0)
	{
		switch (core_state)
		{
		case DSPCORE_RUNNING:
			// Seems to slow things down
#if defined(_DEBUG) || defined(DEBUGFAST)
			cycles = DSPInterpreter::RunCyclesDebug(cycles);
#else
			cycles = DSPInterpreter::RunCycles(cycles);
#endif
			break;

		case DSPCORE_STEPPING:
			step_event.Wait();
			if (core_state != DSPCORE_STEPPING)
				continue;

			DSPInterpreter::Step();
			cycles--;

			DSPHost::UpdateDebugger();
			break;
		case DSPCORE_STOP:
			break;
		}
	}
	return cycles;
}

void DSPCore_SetState(DSPCoreState new_state)
{
	core_state = new_state;
	// kick the event, in case we are waiting
	if (new_state == DSPCORE_RUNNING)
		step_event.Set();
	// Sleep(10);
	DSPHost::UpdateDebugger();
}

DSPCoreState DSPCore_GetState()
{
	return core_state;
}

void DSPCore_Step()
{
	if (core_state == DSPCORE_STEPPING)
		step_event.Set();
}

void CompileCurrent()
{
	g_dsp_jit->Compile(g_dsp.pc);

	bool retry = true;

	while (retry)
	{
		retry = false;
		for (u16 i = 0x0000; i < 0xffff; ++i)
		{
			if (!g_dsp_jit->unresolvedJumps[i].empty())
			{
				u16 addrToCompile = g_dsp_jit->unresolvedJumps[i].front();
				g_dsp_jit->Compile(addrToCompile);
				if (!g_dsp_jit->unresolvedJumps[i].empty())
					retry = true;
			}
		}
	}
}

u16 DSPCore_ReadRegister(int reg)
{
	switch (reg)
	{
	case DSP_REG_AR0:
	case DSP_REG_AR1:
	case DSP_REG_AR2:
	case DSP_REG_AR3:
		return g_dsp.r.ar[reg - DSP_REG_AR0];
	case DSP_REG_IX0:
	case DSP_REG_IX1:
	case DSP_REG_IX2:
	case DSP_REG_IX3:
		return g_dsp.r.ix[reg - DSP_REG_IX0];
	case DSP_REG_WR0:
	case DSP_REG_WR1:
	case DSP_REG_WR2:
	case DSP_REG_WR3:
		return g_dsp.r.wr[reg - DSP_REG_WR0];
	case DSP_REG_ST0:
	case DSP_REG_ST1:
	case DSP_REG_ST2:
	case DSP_REG_ST3:
		return g_dsp.r.st[reg - DSP_REG_ST0];
	case DSP_REG_ACH0:
	case DSP_REG_ACH1:
		return g_dsp.r.ac[reg - DSP_REG_ACH0].h;
	case DSP_REG_CR:     return g_dsp.r.cr;
	case DSP_REG_SR:     return g_dsp.r.sr;
	case DSP_REG_PRODL:  return g_dsp.r.prod.l;
	case DSP_REG_PRODM:  return g_dsp.r.prod.m;
	case DSP_REG_PRODH:  return g_dsp.r.prod.h;
	case DSP_REG_PRODM2: return g_dsp.r.prod.m2;
	case DSP_REG_AXL0:
	case DSP_REG_AXL1:
		return g_dsp.r.ax[reg - DSP_REG_AXL0].l;
	case DSP_REG_AXH0:
	case DSP_REG_AXH1:
		return g_dsp.r.ax[reg - DSP_REG_AXH0].h;
	case DSP_REG_ACL0:
	case DSP_REG_ACL1:
		return g_dsp.r.ac[reg - DSP_REG_ACL0].l;
	case DSP_REG_ACM0:
	case DSP_REG_ACM1:
		return g_dsp.r.ac[reg - DSP_REG_ACM0].m;
	default:
		_assert_msg_(DSP_CORE, 0, "cannot happen");
		return 0;
	}
}

void DSPCore_WriteRegister(int reg, u16 val)
{
	switch (reg)
	{
	case DSP_REG_AR0:
	case DSP_REG_AR1:
	case DSP_REG_AR2:
	case DSP_REG_AR3:
		g_dsp.r.ar[reg - DSP_REG_AR0] = val;
		break;
	case DSP_REG_IX0:
	case DSP_REG_IX1:
	case DSP_REG_IX2:
	case DSP_REG_IX3:
		g_dsp.r.ix[reg - DSP_REG_IX0] = val;
		break;
	case DSP_REG_WR0:
	case DSP_REG_WR1:
	case DSP_REG_WR2:
	case DSP_REG_WR3:
		g_dsp.r.wr[reg - DSP_REG_WR0] = val;
		break;
	case DSP_REG_ST0:
	case DSP_REG_ST1:
	case DSP_REG_ST2:
	case DSP_REG_ST3:
		g_dsp.r.st[reg - DSP_REG_ST0] = val;
		break;
	case DSP_REG_ACH0:
	case DSP_REG_ACH1:
		g_dsp.r.ac[reg - DSP_REG_ACH0].h = val;
		break;
	case DSP_REG_CR:     g_dsp.r.cr = val; break;
	case DSP_REG_SR:     g_dsp.r.sr = val; break;
	case DSP_REG_PRODL:  g_dsp.r.prod.l = val; break;
	case DSP_REG_PRODM:  g_dsp.r.prod.m = val; break;
	case DSP_REG_PRODH:  g_dsp.r.prod.h = val; break;
	case DSP_REG_PRODM2: g_dsp.r.prod.m2 = val; break;
	case DSP_REG_AXL0:
	case DSP_REG_AXL1:
		g_dsp.r.ax[reg - DSP_REG_AXL0].l = val;
		break;
	case DSP_REG_AXH0:
	case DSP_REG_AXH1:
		g_dsp.r.ax[reg - DSP_REG_AXH0].h = val;
		break;
	case DSP_REG_ACL0:
	case DSP_REG_ACL1:
		g_dsp.r.ac[reg - DSP_REG_ACL0].l = val;
		break;
	case DSP_REG_ACM0:
	case DSP_REG_ACM1:
		g_dsp.r.ac[reg - DSP_REG_ACM0].m = val;
		break;
	}
}
