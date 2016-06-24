// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstdio>

#include "Common/Arm64Emitter.h"
#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Common/PerformanceCounter.h"
#include "Common/StringUtil.h"
#include "Common/Logging/Log.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/PatchEngine.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/Profiler.h"
#include "Core/PowerPC/JitArm64/Jit.h"
#include "Core/PowerPC/JitArm64/JitArm64_RegCache.h"
#include "Core/PowerPC/JitArm64/JitArm64_Tables.h"

using namespace Arm64Gen;

static const int AARCH64_FARCODE_SIZE = 1024 * 1024 * 16;
static bool HasCycleCounters()
{
	// Bit needs to be set to support cycle counters
	const u32 PMUSERENR_CR = 0x4;
	u32 reg;
	asm ("mrs %[val], PMUSERENR_EL0"
			: [val] "=r" (reg));
	return !!(reg & PMUSERENR_CR);
}

void JitArm64::Init()
{
	size_t child_code_size = SConfig::GetInstance().bMMU ? FARCODE_SIZE_MMU : AARCH64_FARCODE_SIZE;
	AllocCodeSpace(CODE_SIZE + child_code_size);
	AddChildCodeSpace(&farcode, child_code_size);
	jo.enableBlocklink = true;
	jo.optimizeGatherPipe = true;
	UpdateMemoryOptions();
	gpr.Init(this);
	fpr.Init(this);

	blocks.Init();
	GenerateAsm();

	code_block.m_stats = &js.st;
	code_block.m_gpa = &js.gpa;
	code_block.m_fpa = &js.fpa;
	analyzer.SetOption(PPCAnalyst::PPCAnalyzer::OPTION_CONDITIONAL_CONTINUE);

	m_supports_cycle_counter = HasCycleCounters();
}

void JitArm64::ClearCache()
{
	m_fault_to_handler.clear();
	m_handler_to_loc.clear();

	blocks.Clear();
	ClearCodeSpace();
	farcode.ClearCodeSpace();
	UpdateMemoryOptions();

	GenerateAsm();
}

void JitArm64::Shutdown()
{
	FreeCodeSpace();
	blocks.Shutdown();
}

void JitArm64::FallBackToInterpreter(UGeckoInstruction inst)
{
	gpr.Flush(FlushMode::FLUSH_ALL, js.op);
	fpr.Flush(FlushMode::FLUSH_ALL, js.op);

	if (js.op->opinfo->flags & FL_ENDBLOCK)
	{
		// also flush the program counter
		ARM64Reg WA = gpr.GetReg();
		MOVI2R(WA, js.compilerPC);
		STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(pc));
		ADD(WA, WA, 4);
		STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(npc));
		gpr.Unlock(WA);
	}

	Interpreter::Instruction instr = GetInterpreterOp(inst);
	MOVI2R(W0, inst.hex);
	MOVI2R(X30, (u64)instr);
	BLR(X30);

	if (js.op->opinfo->flags & FL_ENDBLOCK)
	{
		if (js.isLastInstruction)
		{
			ARM64Reg WA = gpr.GetReg();
			LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(npc));
			WriteExceptionExit(WA);
		}
		else
		{
			// only exit if ppcstate.npc was changed
			ARM64Reg WA = gpr.GetReg();
			LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(npc));
			ARM64Reg WB = gpr.GetReg();
			MOVI2R(WB, js.compilerPC + 4);
			CMP(WB, WA);
			gpr.Unlock(WB);
			FixupBranch c = B(CC_EQ);
			WriteExceptionExit(WA);
			SetJumpTarget(c);
		}
	}
}

void JitArm64::HLEFunction(UGeckoInstruction inst)
{
	gpr.Flush(FlushMode::FLUSH_ALL);
	fpr.Flush(FlushMode::FLUSH_ALL);

	MOVI2R(W0, js.compilerPC);
	MOVI2R(W1, inst.hex);
	MOVI2R(X30, (u64)&HLE::Execute);
	BLR(X30);

	ARM64Reg WA = gpr.GetReg();
	LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(npc));
	WriteExit(WA);
}

void JitArm64::DoNothing(UGeckoInstruction inst)
{
	// Yup, just don't do anything.
}

void JitArm64::Break(UGeckoInstruction inst)
{
	WARN_LOG(DYNA_REC, "Breaking! %08x - Fix me ;)", inst.hex);
	exit(0);
}

void JitArm64::Cleanup()
{
	if (jo.optimizeGatherPipe && js.fifoBytesThisBlock > 0)
	{
		gpr.Lock(W0);
		MOVI2R(X0, (u64)&GPFifo::FastCheckGatherPipe);
		BLR(X0);
		gpr.Unlock(W0);
	}
}

void JitArm64::DoDownCount()
{
	ARM64Reg WA = gpr.GetReg();
	LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(downcount));
	if (js.downcountAmount < 4096) // We can enlarge this if we used rotations
	{
		SUBS(WA, WA, js.downcountAmount);
		STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(downcount));
	}
	else
	{
		ARM64Reg WB = gpr.GetReg();
		MOVI2R(WB, js.downcountAmount);
		SUBS(WA, WA, WB);
		STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(downcount));
		gpr.Unlock(WB);
	}
	gpr.Unlock(WA);
}

// Exits
void JitArm64::WriteExit(u32 destination)
{
	Cleanup();
	DoDownCount();

	if (Profiler::g_ProfileBlocks)
		EndTimeProfile(js.curBlock);

	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	JitBlock::LinkData linkData;
	linkData.exitAddress = destination;
	linkData.exitPtrs = GetWritableCodePtr();
	linkData.linkStatus = false;
	b->linkData.push_back(linkData);

	// the code generated in JitArm64BlockCache::WriteDestroyBlock must fit in this block
	MOVI2R(DISPATCHER_PC, destination);
	B(dispatcher);
}

void JitArm64::WriteExit(ARM64Reg Reg)
{
	Cleanup();
	DoDownCount();

	if (Reg != DISPATCHER_PC)
		MOV(DISPATCHER_PC, Reg);
	gpr.Unlock(Reg);

	if (Profiler::g_ProfileBlocks)
		EndTimeProfile(js.curBlock);

	B(dispatcher);
}

void JitArm64::WriteExceptionExit(u32 destination, bool only_external)
{
	Cleanup();
	DoDownCount();

	LDR(INDEX_UNSIGNED, W30, PPC_REG, PPCSTATE_OFF(Exceptions));
	MOVI2R(DISPATCHER_PC, destination);
	FixupBranch no_exceptions = CBZ(W30);

	STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));
	STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(npc));
	if (only_external)
		MOVI2R(X30, (u64)&PowerPC::CheckExternalExceptions);
	else
		MOVI2R(X30, (u64)&PowerPC::CheckExceptions);
	BLR(X30);
	LDR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(npc));

	SetJumpTarget(no_exceptions);

	if (Profiler::g_ProfileBlocks)
		EndTimeProfile(js.curBlock);

	B(dispatcher);
}

void JitArm64::WriteExceptionExit(ARM64Reg dest, bool only_external)
{
	Cleanup();
	DoDownCount();

	ARM64Reg WA = gpr.GetReg();
	LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
	FixupBranch no_exceptions = CBZ(WA);
	gpr.Unlock(WA);

	STR(INDEX_UNSIGNED, dest, PPC_REG, PPCSTATE_OFF(pc));
	STR(INDEX_UNSIGNED, dest, PPC_REG, PPCSTATE_OFF(npc));
	if (only_external)
		MOVI2R(EncodeRegTo64(dest), (u64)&PowerPC::CheckExternalExceptions);
	else
		MOVI2R(EncodeRegTo64(dest), (u64)&PowerPC::CheckExceptions);
	BLR(EncodeRegTo64(dest));
	LDR(INDEX_UNSIGNED, dest, PPC_REG, PPCSTATE_OFF(npc));

	SetJumpTarget(no_exceptions);

	if (dest != DISPATCHER_PC)
		MOV(DISPATCHER_PC, dest);
	gpr.Unlock(dest);

	if (Profiler::g_ProfileBlocks)
		EndTimeProfile(js.curBlock);

	B(dispatcher);
}

void JitArm64::DumpCode(const u8* start, const u8* end)
{
	std::string output = "";
	for (u8* code = (u8*)start; code < end; code += 4)
		output += StringFromFormat("%08x", Common::swap32(*(u32*)code));
	WARN_LOG(DYNA_REC, "Code dump from %p to %p:\n%s", start, end, output.c_str());
}

void JitArm64::EmitResetCycleCounters()
{
	const u32 PMCR_EL0_E  = 1;
	const u32 PMCR_EL0_P  = 2;
	const u32 PMCR_EL0_C  = 4;
	const u32 PMCR_EL0_LC = 0x40;
	_MSR(FIELD_PMCR_EL0, X0);
	MOVI2R(X1, PMCR_EL0_E |
	           PMCR_EL0_P |
	           PMCR_EL0_C |
	           PMCR_EL0_LC);
	ORR(X0, X0, X1);
	MRS(X0, FIELD_PMCR_EL0);
}

void JitArm64::EmitGetCycles(Arm64Gen::ARM64Reg reg)
{
	_MSR(FIELD_PMCCNTR_EL0, reg);
}

void JitArm64::BeginTimeProfile(JitBlock* b)
{
	b->ticCounter = 0;
	b->ticStart = 0;
	b->ticStop = 0;

	if (m_supports_cycle_counter)
	{
		EmitResetCycleCounters();
		EmitGetCycles(X1);
		MOVI2R(X0, (u64)&b->ticStart);
		STR(INDEX_UNSIGNED, X1, X0, 0);
	}
	else
	{
		MOVI2R(X1, (u64)QueryPerformanceCounter);
		MOVI2R(X0, (u64)&b->ticStart);
		BLR(X1);
	}
}

void JitArm64::EndTimeProfile(JitBlock* b)
{
	if (m_supports_cycle_counter)
	{
		EmitGetCycles(X2);
		MOVI2R(X0, (u64)&b->ticStart);
	}
	else
	{
		MOVI2R(X1, (u64)QueryPerformanceCounter);
		MOVI2R(X0, (u64)&b->ticStop);
		BLR(X1);

		MOVI2R(X0, (u64)&b->ticStart);
		LDR(INDEX_UNSIGNED, X2, X0, 8); // Stop
	}

	LDR(INDEX_UNSIGNED, X1, X0, 0); // Start
	LDR(INDEX_UNSIGNED, X3, X0, 16); // Counter
	SUB(X2, X2, X1);
	ADD(X3, X3, X2);
	STR(INDEX_UNSIGNED, X3, X0, 16);
}

void JitArm64::Run()
{
	CompiledCode pExecAddr = (CompiledCode)enterCode;
	pExecAddr();
}

void JitArm64::SingleStep()
{
	CompiledCode pExecAddr = (CompiledCode)enterCode;
	pExecAddr();
}

void JitArm64::Jit(u32)
{
	if (IsAlmostFull() || farcode.IsAlmostFull() || blocks.IsFull() || SConfig::GetInstance().bJITNoBlockCache)
	{
		ClearCache();
	}

	int blockSize = code_buffer.GetSize();
	u32 em_address = PowerPC::ppcState.pc;

	if (SConfig::GetInstance().bEnableDebugging)
	{
		// Comment out the following to disable breakpoints (speed-up)
		blockSize = 1;
	}

	// Analyze the block, collect all instructions it is made of (including inlining,
	// if that is enabled), reorder instructions for optimal performance, and join joinable instructions.
	u32 nextPC = analyzer.Analyze(em_address, &code_block, &code_buffer, blockSize);

	if (code_block.m_memory_exception)
	{
		// Address of instruction could not be translated
		NPC = nextPC;
		PowerPC::ppcState.Exceptions |= EXCEPTION_ISI;
		PowerPC::CheckExceptions();
		WARN_LOG(POWERPC, "ISI exception at 0x%08x", nextPC);
		return;
	}

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	const u8* BlockPtr = DoJit(em_address, &code_buffer, b, nextPC);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink, BlockPtr);
}

const u8* JitArm64::DoJit(u32 em_address, PPCAnalyst::CodeBuffer *code_buf, JitBlock *b, u32 nextPC)
{
	if (em_address == 0)
	{
		Core::SetState(Core::CORE_PAUSE);
		WARN_LOG(DYNA_REC, "ERROR: Compiling at 0. LR=%08x CTR=%08x", LR, CTR);
	}

	js.isLastInstruction = false;
	js.firstFPInstructionFound = false;
	js.assumeNoPairedQuantize = false;
	js.blockStart = em_address;
	js.fifoBytesThisBlock = 0;
	js.downcountAmount = 0;
	js.skipInstructions = 0;
	js.curBlock = b;

	PPCAnalyst::CodeOp *ops = code_buf->codebuffer;

	const u8 *start = GetCodePtr();
	b->checkedEntry = start;
	b->runCount = 0;

	// Downcount flag check, Only valid for linked blocks
	{
		FixupBranch bail = B(CC_PL);
		MOVI2R(DISPATCHER_PC, js.blockStart);
		B(doTiming);
		SetJumpTarget(bail);
	}

	// Normal entry doesn't need to check for downcount.
	b->normalEntry = GetCodePtr();

	// Conditionally add profiling code.
	if (Profiler::g_ProfileBlocks)
	{
		ARM64Reg WA = gpr.GetReg();
		ARM64Reg WB = gpr.GetReg();
		ARM64Reg XA = EncodeRegTo64(WA);
		ARM64Reg XB = EncodeRegTo64(WB);
		MOVI2R(XA, (u64)&b->runCount);
		LDR(INDEX_UNSIGNED, XB, XA, 0);
		ADD(XB, XB, 1);
		STR(INDEX_UNSIGNED, XB, XA, 0);
		gpr.Unlock(WA, WB);
		// get start tic
		BeginTimeProfile(b);
	}

	if (code_block.m_gqr_used.Count() == 1 && js.pairedQuantizeAddresses.find(js.blockStart) == js.pairedQuantizeAddresses.end())
	{
		int gqr = *code_block.m_gqr_used.begin();
		if (!code_block.m_gqr_modified[gqr] && !GQR(gqr))
		{
			LDR(INDEX_UNSIGNED, W0, PPC_REG, PPCSTATE_OFF(spr[SPR_GQR0]) + gqr * 4);
			FixupBranch no_fail = CBZ(W0);
			FixupBranch fail = B();
			SwitchToFarCode();
				SetJumpTarget(fail);
				MOVI2R(DISPATCHER_PC, js.blockStart);
				STR(INDEX_UNSIGNED, DISPATCHER_PC, PPC_REG, PPCSTATE_OFF(pc));
				MOVI2R(W0, (u32)JitInterface::ExceptionType::EXCEPTIONS_PAIRED_QUANTIZE);
				MOVI2R(X1, (u64)&JitInterface::CompileExceptionCheck);
				BLR(X1);
				B(dispatcher);
			SwitchToNearCode();
			SetJumpTarget(no_fail);
			js.assumeNoPairedQuantize = true;
		}
	}

	gpr.Start(js.gpa);
	fpr.Start(js.fpa);

	if (!SConfig::GetInstance().bEnableDebugging)
		js.downcountAmount += PatchEngine::GetSpeedhackCycles(em_address);

	// Translate instructions
	for (u32 i = 0; i < code_block.m_num_instructions; i++)
	{
		js.compilerPC = ops[i].address;
		js.op = &ops[i];
		js.instructionNumber = i;
		js.instructionsLeft = (code_block.m_num_instructions - 1) - i;
		const GekkoOPInfo *opinfo = ops[i].opinfo;
		js.downcountAmount += opinfo->numCycles;

		if (i == (code_block.m_num_instructions - 1))
		{
			// WARNING - cmp->branch merging will screw this up.
			js.isLastInstruction = true;
		}

		// Gather pipe writes using a non-immediate address are discovered by profiling.
		bool gatherPipeIntCheck = jit->js.fifoWriteAddresses.find(ops[i].address) != jit->js.fifoWriteAddresses.end();

		if (jo.optimizeGatherPipe && js.fifoBytesThisBlock >= 32)
		{
			js.fifoBytesThisBlock -= 32;

			gpr.Lock(W30);
			BitSet32 regs_in_use = gpr.GetCallerSavedUsed();
			regs_in_use[W30] = 0;

			FixupBranch Exception = B();
			SwitchToFarCode();
				const u8* done_here = GetCodePtr();
				FixupBranch exit = B();
				SetJumpTarget(Exception);
				ABI_PushRegisters(regs_in_use);
				MOVI2R(X30, (u64)&GPFifo::FastCheckGatherPipe);
				BLR(X30);
				ABI_PopRegisters(regs_in_use);

				// Inline exception check
				LDR(INDEX_UNSIGNED, W30, PPC_REG, PPCSTATE_OFF(Exceptions));
				TBZ(W30, 3, done_here); // EXCEPTION_EXTERNAL_INT
				LDR(INDEX_UNSIGNED, W30, PPC_REG, PPCSTATE_OFF(msr));
				TBZ(W30, 11, done_here);
				MOVI2R(X30, (u64)&ProcessorInterface::m_InterruptCause);
				LDR(INDEX_UNSIGNED, W30, X30, 0);
				TST(W30, 23, 2);
				B(CC_EQ, done_here);

				gpr.Flush(FLUSH_MAINTAIN_STATE);
				fpr.Flush(FLUSH_MAINTAIN_STATE);
				WriteExceptionExit(js.compilerPC, true);
			SwitchToNearCode();
			SetJumpTarget(exit);
			gpr.Unlock(W30);

			// So we don't check exceptions twice
			gatherPipeIntCheck = false;
		}
		// Gather pipe writes can generate an exception; add an exception check.
		// TODO: This doesn't really match hardware; the CP interrupt is
		// asynchronous.
		if (jo.optimizeGatherPipe && gatherPipeIntCheck)
		{
			ARM64Reg WA = gpr.GetReg();
			ARM64Reg XA = EncodeRegTo64(WA);
			LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
			FixupBranch NoExtException = TBZ(WA, 3); // EXCEPTION_EXTERNAL_INT
			FixupBranch Exception = B();
			SwitchToFarCode();
				const u8* done_here = GetCodePtr();
				FixupBranch exit = B();
				SetJumpTarget(Exception);
				LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(msr));
				TBZ(WA, 11, done_here);
				MOVI2R(XA, (u64)&ProcessorInterface::m_InterruptCause);
				LDR(INDEX_UNSIGNED, WA, XA, 0);
				TST(WA, 23, 2);
				B(CC_EQ, done_here);
				gpr.Unlock(WA);

				gpr.Flush(FLUSH_MAINTAIN_STATE);
				fpr.Flush(FLUSH_MAINTAIN_STATE);
				WriteExceptionExit(js.compilerPC, true);
			SwitchToNearCode();
			SetJumpTarget(NoExtException);
			SetJumpTarget(exit);
		}

		if (!ops[i].skip)
		{
			if ((opinfo->flags & FL_USE_FPU) && !js.firstFPInstructionFound)
			{
				//This instruction uses FPU - needs to add FP exception bailout
				ARM64Reg WA = gpr.GetReg();
				LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(msr));
				FixupBranch b1 = TBNZ(WA, 13); // Test FP enabled bit

				FixupBranch far = B();
				SwitchToFarCode();
				SetJumpTarget(far);

				gpr.Flush(FLUSH_MAINTAIN_STATE);
				fpr.Flush(FLUSH_MAINTAIN_STATE);

				LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
				ORR(WA, WA, 26, 0); // EXCEPTION_FPU_UNAVAILABLE
				STR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));

				gpr.Unlock(WA);

				WriteExceptionExit(js.compilerPC);

				SwitchToNearCode();

				SetJumpTarget(b1);

				js.firstFPInstructionFound = true;
			}

			JitArm64Tables::CompileInstruction(ops[i]);

			// If we have a register that will never be used again, flush it.
			gpr.StoreRegisters(~ops[i].gprInUse);
			fpr.StoreRegisters(~ops[i].fprInUse);

			if (jo.memcheck && (opinfo->flags & FL_LOADSTORE))
			{
				ARM64Reg WA = gpr.GetReg();
				LDR(INDEX_UNSIGNED, WA, PPC_REG, PPCSTATE_OFF(Exceptions));
				FixupBranch noException = TBZ(WA, IntLog2(EXCEPTION_DSI));

				FixupBranch handleException = B();
				SwitchToFarCode();
				SetJumpTarget(handleException);

				gpr.Flush(FLUSH_MAINTAIN_STATE);
				fpr.Flush(FLUSH_MAINTAIN_STATE);

				WriteExceptionExit(js.compilerPC);

				SwitchToNearCode();
				SetJumpTarget(noException);
				gpr.Unlock(WA);
			}
		}

		i += js.skipInstructions;
		js.skipInstructions = 0;
	}

	if (code_block.m_broken)
	{
		gpr.Flush(FLUSH_ALL);
		fpr.Flush(FLUSH_ALL);
		WriteExit(nextPC);
	}

	b->codeSize = (u32)(GetCodePtr() - start);
	b->originalSize = code_block.m_num_instructions;

	FlushIcache();
	farcode.FlushIcache();
	return start;
}
