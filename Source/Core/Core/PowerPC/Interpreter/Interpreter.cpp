// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/PowerPC/Interpreter/Interpreter.h"

#include <array>
#include <cinttypes>
#include <string>

#include <fmt/format.h>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/GekkoDisassembler.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Core/ConfigManager.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Debugger_SymbolMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/CPU.h"
#include "Core/Host.h"
#include "Core/PowerPC/Interpreter/ExceptionUtils.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PPCTables.h"
#include "Core/PowerPC/PowerPC.h"

#ifdef USE_GDBSTUB
#include "Core/PowerPC/GDBStub.h"
#endif

namespace
{
u32 last_pc;
}

bool Interpreter::m_end_block;

// function tables
std::array<Interpreter::Instruction, 64> Interpreter::m_op_table;
std::array<Interpreter::Instruction, 1024> Interpreter::m_op_table4;
std::array<Interpreter::Instruction, 1024> Interpreter::m_op_table19;
std::array<Interpreter::Instruction, 1024> Interpreter::m_op_table31;
std::array<Interpreter::Instruction, 32> Interpreter::m_op_table59;
std::array<Interpreter::Instruction, 1024> Interpreter::m_op_table63;

namespace
{
// Determines whether or not the given instruction is one where its execution
// validity is determined by whether or not HID2's LSQE bit is set.
// In other words, if the instruction is psq_l, psq_lu, psq_st, or psq_stu
bool IsPairedSingleQuantizedNonIndexedInstruction(UGeckoInstruction inst)
{
  const u32 opcode = inst.OPCD;
  return opcode == 0x38 || opcode == 0x39 || opcode == 0x3C || opcode == 0x3D;
}

bool IsPairedSingleInstruction(UGeckoInstruction inst)
{
  return inst.OPCD == 4 || IsPairedSingleQuantizedNonIndexedInstruction(inst);
}

// Checks if a given instruction would be illegal to execute if it's a paired single instruction.
//
// Paired single instructions are illegal to execute if HID2.PSE is not set.
// It's also illegal to execute psq_l, psq_lu, psq_st, and psq_stu if HID2.PSE is enabled,
// but HID2.LSQE is not set.
bool IsInvalidPairedSingleExecution(UGeckoInstruction inst)
{
  if (!HID2.PSE && IsPairedSingleInstruction(inst))
    return true;

  return HID2.PSE && !HID2.LSQE && IsPairedSingleQuantizedNonIndexedInstruction(inst);
}

void UpdatePC()
{
  last_pc = PC;
  PC = NPC;
}
}  // Anonymous namespace

void Interpreter::RunTable4(UGeckoInstruction inst)
{
  m_op_table4[inst.SUBOP10](inst);
}
void Interpreter::RunTable19(UGeckoInstruction inst)
{
  m_op_table19[inst.SUBOP10](inst);
}
void Interpreter::RunTable31(UGeckoInstruction inst)
{
  m_op_table31[inst.SUBOP10](inst);
}
void Interpreter::RunTable59(UGeckoInstruction inst)
{
  m_op_table59[inst.SUBOP5](inst);
}
void Interpreter::RunTable63(UGeckoInstruction inst)
{
  m_op_table63[inst.SUBOP10](inst);
}

void Interpreter::Init()
{
  InitializeInstructionTables();
  m_reserve = false;
  m_end_block = false;
}

void Interpreter::Shutdown()
{
}

static int startTrace = 0;

static void Trace(UGeckoInstruction& inst)
{
  std::string regs;
  for (size_t i = 0; i < std::size(PowerPC::ppcState.gpr); i++)
  {
    regs += fmt::format("r{:02d}: {:08x} ", i, PowerPC::ppcState.gpr[i]);
  }

  std::string fregs;
  for (size_t i = 0; i < std::size(PowerPC::ppcState.ps); i++)
  {
    const auto& ps = PowerPC::ppcState.ps[i];
    fregs += fmt::format("f{:02d}: {:08x} {:08x} ", i, ps.PS0AsU64(), ps.PS1AsU64());
  }

  const std::string ppc_inst = Common::GekkoDisassembler::Disassemble(inst.hex, PC);
  DEBUG_LOG_FMT(POWERPC,
                "INTER PC: {:08x} SRR0: {:08x} SRR1: {:08x} CRval: {:016x} "
                "FPSCR: {:08x} MSR: {:08x} LR: {:08x} {} {:08x} {}",
                PC, SRR0, SRR1, PowerPC::ppcState.cr.fields[0], FPSCR.Hex, MSR.Hex,
                PowerPC::ppcState.spr[8], regs, inst.hex, ppc_inst);
}

bool Interpreter::HandleFunctionHooking(u32 address)
{
  return HLE::ReplaceFunctionIfPossible(address, [](u32 hook_index, HLE::HookType type) {
    HLEFunction(hook_index);
    return type != HLE::HookType::Start;
  });
}

int Interpreter::SingleStepInner()
{
  if (HandleFunctionHooking(PC))
  {
    UpdatePC();
    return PPCTables::GetOpInfo(m_prev_inst)->numCycles;
  }

#ifdef USE_GDBSTUB
  if (gdb_active() && gdb_bp_x(PC))
  {
    Host_UpdateDisasmDialog();

    gdb_signal(GDB_SIGTRAP);
    gdb_handle_exception();
  }
#endif

  NPC = PC + sizeof(UGeckoInstruction);
  m_prev_inst.hex = PowerPC::Read_Opcode(PC);

  // Uncomment to trace the interpreter
  // if ((PC & 0xffffff)>=0x0ab54c && (PC & 0xffffff)<=0x0ab624)
  //	startTrace = 1;
  // else
  //	startTrace = 0;

  if (startTrace)
  {
    Trace(m_prev_inst);
  }

  if (m_prev_inst.hex != 0)
  {
    if (IsInvalidPairedSingleExecution(m_prev_inst))
    {
      GenerateProgramException();
      CheckExceptions();
    }
    else if (MSR.FP)
    {
      m_op_table[m_prev_inst.OPCD](m_prev_inst);
      if (PowerPC::ppcState.Exceptions & EXCEPTION_DSI)
      {
        CheckExceptions();
      }
    }
    else
    {
      // check if we have to generate a FPU unavailable exception or a program exception.
      if (PPCTables::UsesFPU(m_prev_inst))
      {
        PowerPC::ppcState.Exceptions |= EXCEPTION_FPU_UNAVAILABLE;
        CheckExceptions();
      }
      else
      {
        m_op_table[m_prev_inst.OPCD](m_prev_inst);
        if (PowerPC::ppcState.Exceptions & EXCEPTION_DSI)
        {
          CheckExceptions();
        }
      }
    }
  }
  else
  {
    // Memory exception on instruction fetch
    CheckExceptions();
  }

  UpdatePC();

  const GekkoOPInfo* opinfo = PPCTables::GetOpInfo(m_prev_inst);
  PowerPC::UpdatePerformanceMonitor(opinfo->numCycles, (opinfo->flags & FL_LOADSTORE) != 0,
                                    (opinfo->flags & FL_USE_FPU) != 0);
  return opinfo->numCycles;
}

void Interpreter::SingleStep()
{
  // Declare start of new slice
  CoreTiming::Advance();

  SingleStepInner();

  // The interpreter ignores instruction timing information outside the 'fast runloop'.
  CoreTiming::g.slice_length = 1;
  PowerPC::ppcState.downcount = 0;

  if (PowerPC::ppcState.Exceptions)
  {
    PowerPC::CheckExceptions();
    PC = NPC;
  }
}

//#define SHOW_HISTORY
#ifdef SHOW_HISTORY
std::vector<int> PCVec;
std::vector<int> PCBlockVec;
int ShowBlocks = 30;
int ShowSteps = 300;
#endif

// FastRun - inspired by GCemu (to imitate the JIT so that they can be compared).
void Interpreter::Run()
{
  while (CPU::GetState() == CPU::State::Running)
  {
    // CoreTiming Advance() ends the previous slice and declares the start of the next
    // one so it must always be called at the start. At boot, we are in slice -1 and must
    // advance into slice 0 to get a correct slice length before executing any cycles.
    CoreTiming::Advance();

    // we have to check exceptions at branches apparently (or maybe just rfi?)
    if (SConfig::GetInstance().bEnableDebugging)
    {
#ifdef SHOW_HISTORY
      PCBlockVec.push_back(PC);
      if (PCBlockVec.size() > ShowBlocks)
        PCBlockVec.erase(PCBlockVec.begin());
#endif

      // Debugging friendly version of inner loop. Tries to do the timing as similarly to the
      // JIT as possible. Does not take into account that some instructions take multiple cycles.
      while (PowerPC::ppcState.downcount > 0)
      {
        m_end_block = false;
        int i;
        for (i = 0; !m_end_block; i++)
        {
#ifdef SHOW_HISTORY
          PCVec.push_back(PC);
          if (PCVec.size() > ShowSteps)
            PCVec.erase(PCVec.begin());
#endif

          // 2: check for breakpoint
          if (PowerPC::breakpoints.IsAddressBreakPoint(PC))
          {
#ifdef SHOW_HISTORY
            NOTICE_LOG_FMT(POWERPC, "----------------------------");
            NOTICE_LOG_FMT(POWERPC, "Blocks:");
            for (const int entry : PCBlockVec)
              NOTICE_LOG_FMT(POWERPC, "PC: {:#010x}", entry);
            NOTICE_LOG_FMT(POWERPC, "----------------------------");
            NOTICE_LOG_FMT(POWERPC, "Steps:");
            for (size_t j = 0; j < PCVec.size(); j++)
            {
              // Write space
              if (j > 0)
              {
                if (PCVec[j] != PCVec[(j - 1) + 4]
                  NOTICE_LOG_FMT(POWERPC, "");
              }

              NOTICE_LOG_FMT(POWERPC, "PC: {:#010x}", PCVec[j]);
            }
#endif
            INFO_LOG_FMT(POWERPC, "Hit Breakpoint - {:08x}", PC);
            CPU::Break();
            if (PowerPC::breakpoints.IsTempBreakPoint(PC))
              PowerPC::breakpoints.Remove(PC);

            Host_UpdateDisasmDialog();
            return;
          }
          SingleStepInner();
        }
        PowerPC::ppcState.downcount -= i;
      }
    }
    else
    {
      // "fast" version of inner loop. well, it's not so fast.
      while (PowerPC::ppcState.downcount > 0)
      {
        m_end_block = false;

        int cycles = 0;
        while (!m_end_block)
        {
          cycles += SingleStepInner();
        }
        PowerPC::ppcState.downcount -= cycles;
      }
    }
  }
}

void Interpreter::unknown_instruction(UGeckoInstruction inst)
{
  const u32 opcode = PowerPC::HostRead_U32(last_pc);
  const std::string disasm = Common::GekkoDisassembler::Disassemble(opcode, last_pc);
  NOTICE_LOG_FMT(POWERPC, "Last PC = {:08x} : {}", last_pc, disasm);
  Dolphin_Debugger::PrintCallstack();
  NOTICE_LOG_FMT(
      POWERPC,
      "\nIntCPU: Unknown instruction {:08x} at PC = {:08x}  last_PC = {:08x}  LR = {:08x}\n",
      inst.hex, PC, last_pc, LR);
  for (int i = 0; i < 32; i += 4)
  {
    NOTICE_LOG_FMT(POWERPC, "r{}: {:#010x} r{}: {:#010x} r{}: {:#010x} r{}: {:#010x}", i, rGPR[i],
                   i + 1, rGPR[i + 1], i + 2, rGPR[i + 2], i + 3, rGPR[i + 3]);
  }
  ASSERT_MSG(POWERPC, 0,
             "\nIntCPU: Unknown instruction %08x at PC = %08x  last_PC = %08x  LR = %08x\n",
             inst.hex, PC, last_pc, LR);
}

void Interpreter::ClearCache()
{
  // Do nothing.
}

void Interpreter::CheckExceptions()
{
  PowerPC::CheckExceptions();
  m_end_block = true;
}

const char* Interpreter::GetName() const
{
#ifdef _ARCH_64
  return "Interpreter64";
#else
  return "Interpreter32";
#endif
}

Interpreter* Interpreter::getInstance()
{
  static Interpreter instance;
  return &instance;
}
