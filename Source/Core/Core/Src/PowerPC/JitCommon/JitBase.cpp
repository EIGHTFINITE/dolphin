// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <sstream>

#include "JitBase.h"
#include "PowerPCDisasm.h"
#include "disasm.h"

JitBase *jit;

void Jit(u32 em_address)
{
	jit->Jit(em_address);
}

u32 Helper_Mask(u8 mb, u8 me)
{
	return (((mb > me) ?
		~(((u32)-1 >> mb) ^ ((me >= 31) ? 0 : (u32) -1 >> (me + 1)))
		:
		(((u32)-1 >> mb) ^ ((me >= 31) ? 0 : (u32) -1 >> (me + 1))))
		);
}

void LogGeneratedX86(int size, PPCAnalyst::CodeBuffer *code_buffer, const u8 *normalEntry, JitBlock *b)
{
	char pDis[1000] = "";

	for (int i = 0; i < size; i++)
	{
		char temp[256] = "";
		const PPCAnalyst::CodeOp &op = code_buffer->codebuffer[i];
		DisassembleGekko(op.inst.hex, op.address, temp, 256);
		sprintf(pDis, "%08x %s", op.address, temp);
		DEBUG_LOG(DYNA_REC,"IR_X86 PPC: %s\n", pDis);
	}	

	disassembler x64disasm;
	x64disasm.set_syntax_intel();

	u64 disasmPtr = (u64)normalEntry;
	const u8 *end = normalEntry + b->codeSize;

	while ((u8*)disasmPtr < end)
	{
		char sptr[1000] = "";
#ifdef _M_X64
		disasmPtr += x64disasm.disasm64(disasmPtr, disasmPtr, (u8*)disasmPtr, sptr);
#else
		disasmPtr += x64disasm.disasm32(disasmPtr, disasmPtr, (u8*)disasmPtr, sptr);
#endif
		DEBUG_LOG(DYNA_REC,"IR_X86 x86: %s", sptr);
	}

	if (b->codeSize <= 250)
	{
		std::stringstream ss;
		ss << std::hex;
		for (u8 i = 0; i <= b->codeSize; i++)
		{
			ss.width(2);
			ss.fill('0');
			ss << (u32)*(normalEntry + i);
		}
		DEBUG_LOG(DYNA_REC,"IR_X86 bin: %s\n\n\n", ss.str().c_str());
	}
}
