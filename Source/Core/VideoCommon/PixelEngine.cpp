// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.


// http://www.nvidia.com/object/General_FAQ.html#t6 !!!!!

#include <atomic>

#include "Common/Atomic.h"
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/ProcessorInterface.h"
#include "VideoCommon/BoundingBox.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/PixelEngine.h"

namespace PixelEngine
{

union UPEZConfReg
{
	u16 Hex;
	struct
	{
		u16 ZCompEnable : 1; // Z Comparator Enable
		u16 Function    : 3;
		u16 ZUpdEnable  : 1;
		u16             : 11;
	};
};

union UPEAlphaConfReg
{
	u16 Hex;
	struct
	{
		u16 BMMath         : 1; // GX_BM_BLEND || GX_BM_SUBSTRACT
		u16 BMLogic        : 1; // GX_BM_LOGIC
		u16 Dither         : 1;
		u16 ColorUpdEnable : 1;
		u16 AlphaUpdEnable : 1;
		u16 DstFactor      : 3;
		u16 SrcFactor      : 3;
		u16 Substract      : 1; // Additive mode by default
		u16 BlendOperator  : 4;
	};
};

union UPEDstAlphaConfReg
{
	u16 Hex;
	struct
	{
		u16 DstAlpha : 8;
		u16 Enable   : 1;
		u16          : 7;
	};
};

union UPEAlphaModeConfReg
{
	u16 Hex;
	struct
	{
		u16 Threshold   : 8;
		u16 CompareMode : 8;
	};
};

// fifo Control Register
union UPECtrlReg
{
	struct
	{
		u16 PETokenEnable  : 1;
		u16 PEFinishEnable : 1;
		u16 PEToken        : 1; // write only
		u16 PEFinish       : 1; // write only
		u16                : 12;
	};
	u16 Hex;
	UPECtrlReg() {Hex = 0; }
	UPECtrlReg(u16 _hex) {Hex = _hex; }
};

// STATE_TO_SAVE
static UPEZConfReg         m_ZConf;
static UPEAlphaConfReg     m_AlphaConf;
static UPEDstAlphaConfReg  m_DstAlphaConf;
static UPEAlphaModeConfReg m_AlphaModeConf;
static UPEAlphaReadReg     m_AlphaRead;
static UPECtrlReg          m_Control;
//static u16                 m_Token; // token value most recently encountered

static std::atomic<u32> s_signal_token_interrupt;
static std::atomic<u32> s_signal_finish_interrupt;

static int et_SetTokenOnMainThread;
static int et_SetFinishOnMainThread;

enum
{
	INT_CAUSE_PE_TOKEN  = 0x200, // GP Token
	INT_CAUSE_PE_FINISH = 0x400, // GP Finished
};

void DoState(PointerWrap &p)
{
	p.Do(m_ZConf);
	p.Do(m_AlphaConf);
	p.Do(m_DstAlphaConf);
	p.Do(m_AlphaModeConf);
	p.Do(m_AlphaRead);
	p.DoPOD(m_Control);

	p.Do(s_signal_token_interrupt);
	p.Do(s_signal_finish_interrupt);
}

static void UpdateInterrupts();
static void UpdateTokenInterrupt(bool active);
static void UpdateFinishInterrupt(bool active);
static void SetToken_OnMainThread(u64 userdata, s64 cyclesLate);
static void SetFinish_OnMainThread(u64 userdata, s64 cyclesLate);

void Init()
{
	m_Control.Hex = 0;
	m_ZConf.Hex = 0;
	m_AlphaConf.Hex = 0;
	m_DstAlphaConf.Hex = 0;
	m_AlphaModeConf.Hex = 0;
	m_AlphaRead.Hex = 0;

	s_signal_token_interrupt.store(0);
	s_signal_finish_interrupt.store(0);

	et_SetTokenOnMainThread = CoreTiming::RegisterEvent("SetToken", SetToken_OnMainThread);
	et_SetFinishOnMainThread = CoreTiming::RegisterEvent("SetFinish", SetFinish_OnMainThread);
}

void RegisterMMIO(MMIO::Mapping* mmio, u32 base)
{
	// Directly mapped registers.
	struct {
		u32 addr;
		u16* ptr;
	} directly_mapped_vars[] = {
		{ PE_ZCONF, &m_ZConf.Hex },
		{ PE_ALPHACONF, &m_AlphaConf.Hex },
		{ PE_DSTALPHACONF, &m_DstAlphaConf.Hex },
		{ PE_ALPHAMODE, &m_AlphaModeConf.Hex },
		{ PE_ALPHAREAD, &m_AlphaRead.Hex },
	};
	for (auto& mapped_var : directly_mapped_vars)
	{
		mmio->Register(base | mapped_var.addr,
			MMIO::DirectRead<u16>(mapped_var.ptr),
			MMIO::DirectWrite<u16>(mapped_var.ptr)
		);
	}

	// Performance queries registers: read only, need to call the video backend
	// to get the results.
	struct {
		u32 addr;
		PerfQueryType pqtype;
	} pq_regs[] = {
		{ PE_PERF_ZCOMP_INPUT_ZCOMPLOC_L, PQ_ZCOMP_INPUT_ZCOMPLOC },
		{ PE_PERF_ZCOMP_OUTPUT_ZCOMPLOC_L, PQ_ZCOMP_OUTPUT_ZCOMPLOC },
		{ PE_PERF_ZCOMP_INPUT_L, PQ_ZCOMP_INPUT },
		{ PE_PERF_ZCOMP_OUTPUT_L, PQ_ZCOMP_OUTPUT },
		{ PE_PERF_BLEND_INPUT_L, PQ_BLEND_INPUT },
		{ PE_PERF_EFB_COPY_CLOCKS_L, PQ_EFB_COPY_CLOCKS },
	};
	for (auto& pq_reg : pq_regs)
	{
		mmio->Register(base | pq_reg.addr,
			MMIO::ComplexRead<u16>([pq_reg](u32) {
				return g_video_backend->Video_GetQueryResult(pq_reg.pqtype) & 0xFFFF;
			}),
			MMIO::InvalidWrite<u16>()
		);
		mmio->Register(base | (pq_reg.addr + 2),
			MMIO::ComplexRead<u16>([pq_reg](u32) {
				return g_video_backend->Video_GetQueryResult(pq_reg.pqtype) >> 16;
			}),
			MMIO::InvalidWrite<u16>()
		);
	}

	// Control register
	mmio->Register(base | PE_CTRL_REGISTER,
		MMIO::DirectRead<u16>(&m_Control.Hex),
		MMIO::ComplexWrite<u16>([](u32, u16 val) {
			UPECtrlReg tmpCtrl(val);

			if (tmpCtrl.PEToken)
				s_signal_token_interrupt.store(0);

			if (tmpCtrl.PEFinish)
				s_signal_finish_interrupt.store(0);

			m_Control.PETokenEnable  = tmpCtrl.PETokenEnable;
			m_Control.PEFinishEnable = tmpCtrl.PEFinishEnable;
			m_Control.PEToken = 0;  // this flag is write only
			m_Control.PEFinish = 0; // this flag is write only

			DEBUG_LOG(PIXELENGINE, "(w16) CTRL_REGISTER: 0x%04x", val);
			UpdateInterrupts();
		})
	);

	// Token register, readonly.
	mmio->Register(base | PE_TOKEN_REG,
		MMIO::DirectRead<u16>(&CommandProcessor::fifo.PEToken),
		MMIO::InvalidWrite<u16>()
	);

	// BBOX registers, readonly and need to update a flag.
	for (int i = 0; i < 4; ++i)
	{
		mmio->Register(base | (PE_BBOX_LEFT + 2 * i),
			MMIO::ComplexRead<u16>([i](u32) {
				BoundingBox::active = false;
				return g_video_backend->Video_GetBoundingBox(i);
			}),
			MMIO::InvalidWrite<u16>()
		);
	}
}

static void UpdateInterrupts()
{
	// check if there is a token-interrupt
	UpdateTokenInterrupt((s_signal_token_interrupt.load() & m_Control.PETokenEnable) != 0);

	// check if there is a finish-interrupt
	UpdateFinishInterrupt((s_signal_finish_interrupt.load() & m_Control.PEFinishEnable) != 0);
}

static void UpdateTokenInterrupt(bool active)
{
	ProcessorInterface::SetInterrupt(INT_CAUSE_PE_TOKEN, active);
}

static void UpdateFinishInterrupt(bool active)
{
	ProcessorInterface::SetInterrupt(INT_CAUSE_PE_FINISH, active);
}

// TODO(mb2): Refactor SetTokenINT_OnMainThread(u64 userdata, int cyclesLate).
//            Think about the right order between tokenVal and tokenINT... one day maybe.
//            Cleanup++

// Called only if BPMEM_PE_TOKEN_INT_ID is ack by GP
static void SetToken_OnMainThread(u64 userdata, s64 cyclesLate)
{
	// XXX: No 16-bit atomic store available, so cheat and use 32-bit.
	// That's what we've always done. We're counting on fifo.PEToken to be
	// 4-byte padded.
	Common::AtomicStore(*(volatile u32*)&CommandProcessor::fifo.PEToken, userdata & 0xffff);
	INFO_LOG(PIXELENGINE, "VIDEO Backend raises INT_CAUSE_PE_TOKEN (btw, token: %04x)", CommandProcessor::fifo.PEToken);
	if (userdata >> 16)
	{
		s_signal_token_interrupt.store(1);
		UpdateInterrupts();
	}
	CommandProcessor::SetInterruptTokenWaiting(false);
}

static void SetFinish_OnMainThread(u64 userdata, s64 cyclesLate)
{
	s_signal_finish_interrupt.store(1);
	UpdateInterrupts();
	CommandProcessor::SetInterruptFinishWaiting(false);

	Core::FrameUpdateOnCPUThread();
}

// SetToken
// THIS IS EXECUTED FROM VIDEO THREAD
void SetToken(const u16 _token, const int _bSetTokenAcknowledge)
{
	if (_bSetTokenAcknowledge) // set token INT
	{
		s_signal_token_interrupt.store(1);
	}

	CommandProcessor::SetInterruptTokenWaiting(true);

	if (!SConfig::GetInstance().bCPUThread || Fifo::UseDeterministicGPUThread())
		CoreTiming::ScheduleEvent(0, et_SetTokenOnMainThread, _token | (_bSetTokenAcknowledge << 16));
	else
		CoreTiming::ScheduleEvent_Threadsafe(0, et_SetTokenOnMainThread, _token | (_bSetTokenAcknowledge << 16));
}

// SetFinish
// THIS IS EXECUTED FROM VIDEO THREAD (BPStructs.cpp) when a new frame has been drawn
void SetFinish()
{
	CommandProcessor::SetInterruptFinishWaiting(true);

	if (!SConfig::GetInstance().bCPUThread || Fifo::UseDeterministicGPUThread())
		CoreTiming::ScheduleEvent(0, et_SetFinishOnMainThread, 0);
	else
		CoreTiming::ScheduleEvent_Threadsafe(0, et_SetFinishOnMainThread, 0);

	INFO_LOG(PIXELENGINE, "VIDEO Set Finish");
}

UPEAlphaReadReg GetAlphaReadMode()
{
	return m_AlphaRead;
}

} // end of namespace PixelEngine
