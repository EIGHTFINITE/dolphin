// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

//DL facts:
//  Ikaruga uses (nearly) NO display lists!
//  Zelda WW uses TONS of display lists
//  Zelda TP uses almost 100% display lists except menus (we like this!)
//  Super Mario Galaxy has nearly all geometry and more than half of the state in DLs (great!)

// Note that it IS NOT GENERALLY POSSIBLE to precompile display lists! You can compile them as they are
// while interpreting them, and hope that the vertex format doesn't change, though, if you do it right
// when they are called. The reason is that the vertex format affects the sizes of the vertices.

#include "Common/CommonTypes.h"
#include "Common/MsgHandler.h"
#include "Common/Logging/Log.h"
#include "Core/FifoPlayer/FifoRecorder.h"
#include "Core/HW/Memmap.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/CPMemory.h"
#include "VideoCommon/DataReader.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/XFMemory.h"

bool g_bRecordFifoData = false;

namespace OpcodeDecoder
{

static bool s_bFifoErrorSeen = false;

static u32 InterpretDisplayList(u32 address, u32 size)
{
	u8* startAddress;

	if (Fifo::UseDeterministicGPUThread())
		startAddress = (u8*)Fifo::PopFifoAuxBuffer(size);
	else
		startAddress = Memory::GetPointer(address);

	u32 cycles = 0;

	// Avoid the crash if Memory::GetPointer failed ..
	if (startAddress != nullptr)
	{
		// temporarily swap dl and non-dl (small "hack" for the stats)
		Statistics::SwapDL();

		Run(DataReader(startAddress, startAddress + size), &cycles, true);
		INCSTAT(stats.thisFrame.numDListsCalled);

		// un-swap
		Statistics::SwapDL();
	}

	return cycles;
}

static void InterpretDisplayListPreprocess(u32 address, u32 size)
{
	u8* startAddress = Memory::GetPointer(address);

	Fifo::PushFifoAuxBuffer(startAddress, size);

	if (startAddress != nullptr)
	{
		Run<true>(DataReader(startAddress, startAddress + size), nullptr, true);
	}
}

static void UnknownOpcode(u8 cmd_byte, void *buffer, bool preprocess)
{
	// TODO(Omega): Maybe dump FIFO to file on this error
	PanicAlertT(
	    "GFX FIFO: Unknown Opcode (0x%02x @ %p, %s).\n"
	    "This means one of the following:\n"
	    "* The emulated GPU got desynced, disabling dual core can help\n"
	    "* Command stream corrupted by some spurious memory bug\n"
	    "* This really is an unknown opcode (unlikely)\n"
	    "* Some other sort of bug\n\n"
	    "Further errors will be sent to the Video Backend log and\n"
	    "Dolphin will now likely crash or hang. Enjoy." ,
	    cmd_byte,
	    buffer,
	    preprocess ? "preprocess=true" : "preprocess=false");

	{
		SCPFifoStruct &fifo = CommandProcessor::fifo;

		PanicAlert(
		    "Illegal command %02x\n"
		    "CPBase: 0x%08x\n"
		    "CPEnd: 0x%08x\n"
		    "CPHiWatermark: 0x%08x\n"
		    "CPLoWatermark: 0x%08x\n"
		    "CPReadWriteDistance: 0x%08x\n"
		    "CPWritePointer: 0x%08x\n"
		    "CPReadPointer: 0x%08x\n"
		    "CPBreakpoint: 0x%08x\n"
		    "bFF_GPReadEnable: %s\n"
		    "bFF_BPEnable: %s\n"
		    "bFF_BPInt: %s\n"
		    "bFF_Breakpoint: %s\n"
		    "bFF_GPLinkEnable: %s\n"
		    "bFF_HiWatermarkInt: %s\n"
		    "bFF_LoWatermarkInt: %s\n"
		    ,cmd_byte, fifo.CPBase, fifo.CPEnd, fifo.CPHiWatermark, fifo.CPLoWatermark, fifo.CPReadWriteDistance
		    ,fifo.CPWritePointer, fifo.CPReadPointer, fifo.CPBreakpoint
		    ,fifo.bFF_GPReadEnable ? "true" : "false"
		    ,fifo.bFF_BPEnable ? "true" : "false"
		    ,fifo.bFF_BPInt ? "true" : "false"
		    ,fifo.bFF_Breakpoint ? "true" : "false"
		    ,fifo.bFF_GPLinkEnable ? "true" : "false"
		    ,fifo.bFF_HiWatermarkInt ? "true" : "false"
		    ,fifo.bFF_LoWatermarkInt ? "true" : "false"
		    );
	}
}

void Init()
{
	s_bFifoErrorSeen = false;
}

void Shutdown()
{
}

template <bool is_preprocess>
u8* Run(DataReader src, u32* cycles, bool in_display_list)
{
	u32 totalCycles = 0;
	u8* opcodeStart;
	while (true)
	{
		opcodeStart = src.GetPointer();

		if (!src.size())
			goto end;

		u8 cmd_byte = src.Read<u8>();
		int refarray;
		switch (cmd_byte)
		{
		case GX_NOP:
			totalCycles += 6; // Hm, this means that we scan over nop streams pretty slowly...
			break;

		case GX_UNKNOWN_RESET:
			totalCycles += 6; // Datel software uses this command
			DEBUG_LOG(VIDEO, "GX Reset?: %08x", cmd_byte);
			break;

		case GX_LOAD_CP_REG:
			{
				if (src.size() < 1 + 4)
					goto end;
				totalCycles += 12;
				u8 sub_cmd = src.Read<u8>();
				u32 value =  src.Read<u32>();
				LoadCPReg(sub_cmd, value, is_preprocess);
				if (!is_preprocess)
					INCSTAT(stats.thisFrame.numCPLoads);
			}
			break;

		case GX_LOAD_XF_REG:
			{
				if (src.size() < 4)
					goto end;
				u32 Cmd2 =  src.Read<u32>();
				int transfer_size = ((Cmd2 >> 16) & 15) + 1;
				if (src.size() < transfer_size * sizeof(u32))
					goto end;
				totalCycles += 18 + 6 * transfer_size;
				if (!is_preprocess)
				{
					u32 xf_address = Cmd2 & 0xFFFF;
					LoadXFReg(transfer_size, xf_address, src);

					INCSTAT(stats.thisFrame.numXFLoads);
				}
				src.Skip<u32>(transfer_size);
			}
			break;

		case GX_LOAD_INDX_A: //used for position matrices
			refarray = 0xC;
			goto load_indx;
		case GX_LOAD_INDX_B: //used for normal matrices
			refarray = 0xD;
			goto load_indx;
		case GX_LOAD_INDX_C: //used for postmatrices
			refarray = 0xE;
			goto load_indx;
		case GX_LOAD_INDX_D: //used for lights
			refarray = 0xF;
			goto load_indx;
		load_indx:
			if (src.size() < 4)
				goto end;
			totalCycles += 6;
			if (is_preprocess)
				PreprocessIndexedXF(src.Read<u32>(), refarray);
			else
				LoadIndexedXF(src.Read<u32>(), refarray);
			break;

		case GX_CMD_CALL_DL:
			{
				if (src.size() < 8)
					goto end;
				u32 address = src.Read<u32>();
				u32 count = src.Read<u32>();

				if (in_display_list)
				{
					totalCycles += 6;
					WARN_LOG(VIDEO,"recursive display list detected");
				}
				else
				{
					if (is_preprocess)
						InterpretDisplayListPreprocess(address, count);
					else
						totalCycles += 6 + InterpretDisplayList(address, count);
				}
			}
			break;

		case GX_CMD_UNKNOWN_METRICS: // zelda 4 swords calls it and checks the metrics registers after that
			totalCycles += 6;
			DEBUG_LOG(VIDEO, "GX 0x44: %08x", cmd_byte);
			break;

		case GX_CMD_INVL_VC: // Invalidate Vertex Cache
			totalCycles += 6;
			DEBUG_LOG(VIDEO, "Invalidate (vertex cache?)");
			break;

		case GX_LOAD_BP_REG:
			// In skipped_frame case: We have to let BP writes through because they set
			// tokens and stuff.  TODO: Call a much simplified LoadBPReg instead.
			{
				if (src.size() < 4)
					goto end;
				totalCycles += 12;
				u32 bp_cmd = src.Read<u32>();
				if (is_preprocess)
				{
					LoadBPRegPreprocess(bp_cmd);
				}
				else
				{
					LoadBPReg(bp_cmd);
					INCSTAT(stats.thisFrame.numBPLoads);
				}
			}
			break;

		// draw primitives
		default:
			if ((cmd_byte & 0xC0) == 0x80)
			{
				// load vertices
				if (src.size() < 2)
					goto end;
				u16 num_vertices = src.Read<u16>();
				int bytes = VertexLoaderManager::RunVertices(
					cmd_byte & GX_VAT_MASK,   // Vertex loader index (0 - 7)
					(cmd_byte & GX_PRIMITIVE_MASK) >> GX_PRIMITIVE_SHIFT,
					num_vertices,
					src,
					Fifo::WillSkipCurrentFrame(),
					is_preprocess);

				if (bytes < 0)
					goto end;

				src.Skip(bytes);

				// 4 GPU ticks per vertex, 3 CPU ticks per GPU tick
				totalCycles += num_vertices * 4 * 3 + 6;
			}
			else
			{
				if (!s_bFifoErrorSeen)
					UnknownOpcode(cmd_byte, opcodeStart, is_preprocess);
				ERROR_LOG(VIDEO, "FIFO: Unknown Opcode(0x%02x @ %p, preprocessing = %s)", cmd_byte, opcodeStart, is_preprocess ? "yes" : "no");
				s_bFifoErrorSeen = true;
				totalCycles += 1;
			}
			break;
		}

		// Display lists get added directly into the FIFO stream
		if (!is_preprocess && g_bRecordFifoData && cmd_byte != GX_CMD_CALL_DL)
		{
			u8* opcodeEnd;
			opcodeEnd = src.GetPointer();
			FifoRecorder::GetInstance().WriteGPCommand(opcodeStart, u32(opcodeEnd - opcodeStart));
		}
	}

end:
	if (cycles)
	{
		*cycles = totalCycles;
	}
	return opcodeStart;
}

template u8* Run<true>(DataReader src, u32* cycles, bool in_display_list);
template u8* Run<false>(DataReader src, u32* cycles, bool in_display_list);

} // namespace OpcodeDecoder
