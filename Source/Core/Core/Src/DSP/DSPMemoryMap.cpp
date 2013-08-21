/*====================================================================

   filename:     gdsp_memory.cpp
   project:      GCemu
   created:      2004-6-18
   mail:		  duddie@walla.com

   Copyright (c) 2005 Duddie & Tratax

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

   ====================================================================*/

#include "DSPInterpreter.h"
#include "DSPMemoryMap.h"
#include "DSPHWInterface.h"
#include "DSPCore.h"

u16 dsp_imem_read(u16 addr)
{
	switch (addr >> 12)
	{
	case 0:   // 0xxx IRAM
		return g_dsp.iram[addr & DSP_IRAM_MASK];

	case 8:   // 8xxx IROM - contains code to receive code for IRAM, and a bunch of mixing loops.
		return g_dsp.irom[addr & DSP_IROM_MASK];

	default:  // Unmapped/non-existing memory
		ERROR_LOG(DSPLLE, "%04x DSP ERROR: Executing from invalid (%04x) memory", g_dsp.pc, addr);
		return 0;
	}
}

u16 dsp_dmem_read(u16 addr)
{
	switch (addr >> 12)
	{
	case 0x0:  // 0xxx DRAM
		return g_dsp.dram[addr & DSP_DRAM_MASK];
		
	case 0x1:  // 1xxx COEF
		DEBUG_LOG(DSPLLE, "%04x : Coefficient Read @ %04x", g_dsp.pc, addr);
		return g_dsp.coef[addr & DSP_COEF_MASK];

	case 0xf:  // Fxxx HW regs
		return gdsp_ifx_read(addr);
		
	default:   // Unmapped/non-existing memory
		ERROR_LOG(DSPLLE, "%04x DSP ERROR: Read from UNKNOWN (%04x) memory", g_dsp.pc, addr);
		return 0;
	}
}

void dsp_dmem_write(u16 addr, u16 val)
{
	switch (addr >> 12)
	{
	case 0x0: // 0xxx DRAM
		g_dsp.dram[addr & DSP_DRAM_MASK] = val;
		break;

	case 0xf: // Fxxx HW regs
		gdsp_ifx_write(addr, val);
		break;

	default:  // Unmapped/non-existing memory
		ERROR_LOG(DSPLLE, "%04x DSP ERROR: Write to UNKNOWN (%04x) memory", g_dsp.pc, addr);
		break;
	}
}
