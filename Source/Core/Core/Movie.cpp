// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <mbedtls/config.h>
#include <mbedtls/md.h>

#include "Common/ChunkFile.h"
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Hash.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"
#include "Common/Timer.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Movie.h"
#include "Core/NetPlayClient.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"
#include "Core/DSP/DSPCore.h"
#include "Core/HW/CPU.h"
#include "Core/HW/DVDInterface.h"
#include "Core/HW/EXI_Device.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/HW/WiimoteEmu/WiimoteHid.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "Core/IPC_HLE/WII_IPC_HLE_WiiMote.h"
#include "Core/PowerPC/PowerPC.h"
#include "InputCommon/GCPadStatus.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

// The chunk to allocate movie data in multiples of.
#define DTM_BASE_LENGTH (1024)

static std::mutex cs_frameSkip;

namespace Movie {

static bool s_bFrameStep = false;
static bool s_bReadOnly = true;
static u32 s_rerecords = 0;
static PlayMode s_playMode = MODE_NONE;

static u32 s_framesToSkip = 0, s_frameSkipCounter = 0;

static u8 s_numPads = 0;
static ControllerState s_padState;
static DTMHeader tmpHeader;
static u8* tmpInput = nullptr;
static size_t tmpInputAllocated = 0;
static u64 s_currentByte = 0, s_totalBytes = 0;
u64 g_currentFrame = 0, g_totalFrames = 0; // VI
u64 g_currentLagCount = 0;
static u64 s_totalLagCount = 0; // just stats
u64 g_currentInputCount = 0, g_totalInputCount = 0; // just stats
static u64 s_totalTickCount = 0, s_tickCountAtLastInput = 0; // just stats
static u64 s_recordingStartTime; // seconds since 1970 that recording started
static bool s_bSaveConfig = false, s_bSkipIdle = false, s_bDualCore = false;
static bool s_bProgressive = false, s_bPAL60 = false;
static bool s_bDSPHLE = false, s_bFastDiscSpeed = false;
static bool s_bSyncGPU = false, s_bNetPlay = false;
static std::string s_videoBackend = "unknown";
static int s_iCPUCore = 1;
bool g_bClearSave = false;
bool g_bDiscChange = false;
bool g_bReset = false;
static std::string s_author = "";
std::string g_discChange = "";
u64 g_titleID = 0;
static u8 s_MD5[16];
static u8 s_bongos, s_memcards;
static u8 s_revision[20];
static u32 s_DSPiromHash = 0;
static u32 s_DSPcoefHash = 0;
static u8 s_language = 10; //Set to unknown until language is known

static bool s_bRecordingFromSaveState = false;
static bool s_bPolled = false;

// s_InputDisplay is used by both CPU and GPU (is mutable).
static std::mutex  s_input_display_lock;
static std::string s_InputDisplay[8];

static GCManipFunction gcmfunc = nullptr;
static WiiManipFunction wiimfunc = nullptr;

// NOTE: Host / CPU Thread
static void EnsureTmpInputSize(size_t bound)
{
	if (tmpInputAllocated >= bound)
		return;
	// The buffer expands in powers of two of DTM_BASE_LENGTH
	// (standard exponential buffer growth).
	size_t newAlloc = DTM_BASE_LENGTH;
	while (newAlloc < bound)
		newAlloc *= 2;

	u8* newTmpInput = new u8[newAlloc];
	tmpInputAllocated = newAlloc;
	if (tmpInput != nullptr)
	{
		if (s_totalBytes > 0)
			memcpy(newTmpInput, tmpInput, (size_t)s_totalBytes);
		delete[] tmpInput;
	}
	tmpInput = newTmpInput;
}

static bool IsMovieHeader(u8 magic[4])
{
	return magic[0] == 'D' &&
	       magic[1] == 'T' &&
	       magic[2] == 'M' &&
	       magic[3] == 0x1A;
}

static std::array<u8, 20> ConvertGitRevisionToBytes(const std::string& revision)
{
	std::array<u8, 20> revision_bytes{};

	if (revision.size() % 2 == 0 && std::all_of(revision.begin(), revision.end(), ::isxdigit))
	{
		// The revision string normally contains a git commit hash,
		// which is 40 hexadecimal digits long. In DTM files, each pair of
		// hexadecimal digits is stored as one byte, for a total of 20 bytes.
		size_t bytes_to_write = std::min(revision.size() / 2, revision_bytes.size());
		unsigned int temp;
		for (size_t i = 0; i < bytes_to_write; ++i)
		{
			sscanf(&revision[2 * i], "%02x", &temp);
			revision_bytes[i] = temp;
		}
	}
	else
	{
		// If the revision string for some reason doesn't only contain hexadecimal digit
		// pairs, we instead copy the string with no conversion. This probably doesn't match
		// the intended design of the DTM format, but it's the most sensible fallback.
		size_t bytes_to_write = std::min(revision.size(), revision_bytes.size());
		std::copy_n(std::begin(revision), bytes_to_write, std::begin(revision_bytes));
	}

	return revision_bytes;
}

// NOTE: GPU Thread
std::string GetInputDisplay()
{
	if (!IsMovieActive())
	{
		s_numPads = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (SerialInterface::GetDeviceType(i) != SIDEVICE_NONE)
				s_numPads |= (1 << i);
			if (g_wiimote_sources[i] != WIIMOTE_SRC_NONE)
				s_numPads |= (1 << (i + 4));
		}
	}

	std::string input_display;
	{
		std::lock_guard<std::mutex> guard(s_input_display_lock);
		for (int i = 0; i < 8; ++i)
		{
			if ((s_numPads & (1 << i)) != 0)
				input_display += s_InputDisplay[i];
		}
	}
	return input_display;
}

// NOTE: GPU Thread
void FrameUpdate()
{
	// TODO[comex]: This runs on the GPU thread, yet it messes with the CPU
	// state directly.  That's super sketchy.
	g_currentFrame++;
	if (!s_bPolled)
		g_currentLagCount++;

	if (IsRecordingInput())
	{
		g_totalFrames = g_currentFrame;
		s_totalLagCount = g_currentLagCount;
	}
	if (s_bFrameStep)
	{
		s_bFrameStep = false;
		CPU::Break();
	}

	if (s_framesToSkip)
		FrameSkipping();

	s_bPolled = false;
}

// called when game is booting up, even if no movie is active,
// but potentially after BeginRecordingInput or PlayInput has been called.
// NOTE: EmuThread
void Init()
{
	s_bPolled = false;
	s_bFrameStep = false;
	s_bSaveConfig = false;
	s_iCPUCore = SConfig::GetInstance().iCPUCore;
	if (IsPlayingInput())
	{
		ReadHeader();
		std::thread md5thread(CheckMD5);
		md5thread.detach();
		if (strncmp(tmpHeader.gameID, SConfig::GetInstance().GetUniqueID().c_str(), 6))
		{
			PanicAlertT("The recorded game (%s) is not the same as the selected game (%s)", tmpHeader.gameID, SConfig::GetInstance().GetUniqueID().c_str());
			EndPlayInput(false);
		}
	}

	if (IsRecordingInput())
	{
		GetSettings();
		std::thread md5thread(GetMD5);
		md5thread.detach();
		s_tickCountAtLastInput = 0;
	}

	s_frameSkipCounter = s_framesToSkip;
	memset(&s_padState, 0, sizeof(s_padState));
	if (!tmpHeader.bFromSaveState || !IsPlayingInput())
		Core::SetStateFileName("");

	for (auto& disp : s_InputDisplay)
		disp.clear();

	if (!IsMovieActive())
	{
		s_bRecordingFromSaveState = false;
		s_rerecords = 0;
		s_currentByte = 0;
		g_currentFrame = 0;
		g_currentLagCount = 0;
		g_currentInputCount = 0;
	}
}

// NOTE: CPU Thread
void InputUpdate()
{
	g_currentInputCount++;
	if (IsRecordingInput())
	{
		g_totalInputCount = g_currentInputCount;
		s_totalTickCount += CoreTiming::GetTicks() - s_tickCountAtLastInput;
		s_tickCountAtLastInput = CoreTiming::GetTicks();
	}
}

// NOTE: Host Thread
void SetFrameSkipping(unsigned int framesToSkip)
{
	std::lock_guard<std::mutex> lk(cs_frameSkip);

	s_framesToSkip = framesToSkip;
	s_frameSkipCounter = 0;

	// Don't forget to re-enable rendering in case it wasn't...
	// as this won't be changed anymore when frameskip is turned off
	if (framesToSkip == 0)
		Fifo::SetRendering(true);
}

// NOTE: CPU Thread
void SetPolledDevice()
{
	s_bPolled = true;
}

// NOTE: Host Thread
void DoFrameStep()
{
	if (Core::GetState() == Core::CORE_PAUSE)
	{
		// if already paused, frame advance for 1 frame
		s_bFrameStep = true;
		Core::RequestRefreshInfo();
		Core::SetState(Core::CORE_RUN);
	}
	else if (!s_bFrameStep)
	{
		// if not paused yet, pause immediately instead
		Core::SetState(Core::CORE_PAUSE);
	}
}

// NOTE: Host Thread
void SetReadOnly(bool bEnabled)
{
	if (s_bReadOnly != bEnabled)
		Core::DisplayMessage(bEnabled ? "Read-only mode." :  "Read+Write mode.", 1000);

	s_bReadOnly = bEnabled;
}

// NOTE: GPU Thread
void FrameSkipping()
{
	// Frameskipping will desync movie playback
	if (!IsMovieActive() || NetPlay::IsNetPlayRunning())
	{
		std::lock_guard<std::mutex> lk(cs_frameSkip);

		s_frameSkipCounter++;
		if (s_frameSkipCounter > s_framesToSkip || Core::ShouldSkipFrame(s_frameSkipCounter) == false)
			s_frameSkipCounter = 0;

		Fifo::SetRendering(!s_frameSkipCounter);
	}
}

bool IsRecordingInput()
{
	return (s_playMode == MODE_RECORDING);
}

bool IsRecordingInputFromSaveState()
{
	return s_bRecordingFromSaveState;
}

bool IsJustStartingRecordingInputFromSaveState()
{
	return IsRecordingInputFromSaveState() && g_currentFrame == 0;
}

bool IsJustStartingPlayingInputFromSaveState()
{
	return IsRecordingInputFromSaveState() && g_currentFrame == 1 && IsPlayingInput();
}

bool IsPlayingInput()
{
	return (s_playMode == MODE_PLAYING);
}

bool IsMovieActive()
{
	return s_playMode != MODE_NONE;
}

bool IsReadOnly()
{
	return s_bReadOnly;
}

u64 GetRecordingStartTime()
{
	return s_recordingStartTime;
}

bool IsUsingPad(int controller)
{
	return ((s_numPads & (1 << controller)) != 0);
}

bool IsUsingBongo(int controller)
{
	return ((s_bongos & (1 << controller)) != 0);
}

bool IsUsingWiimote(int wiimote)
{
	return ((s_numPads & (1 << (wiimote + 4))) != 0);
}

bool IsConfigSaved()
{
	return s_bSaveConfig;
}
bool IsDualCore()
{
	return s_bDualCore;
}

bool IsProgressive()
{
	return s_bProgressive;
}

bool IsPAL60()
{
	return s_bPAL60;
}

bool IsSkipIdle()
{
	return s_bSkipIdle;
}

bool IsDSPHLE()
{
	return s_bDSPHLE;
}

bool IsFastDiscSpeed()
{
	return s_bFastDiscSpeed;
}

int GetCPUMode()
{
	return s_iCPUCore;
}

u8 GetLanguage()
{
	return s_language;
}

bool IsStartingFromClearSave()
{
	return g_bClearSave;
}

bool IsUsingMemcard(int memcard)
{
	return (s_memcards & (1 << memcard)) != 0;
}
bool IsSyncGPU()
{
	return s_bSyncGPU;
}

bool IsNetPlayRecording()
{
	return s_bNetPlay;
}

// NOTE: Host Thread
void ChangePads(bool instantly)
{
	if (!Core::IsRunning())
		return;

	int controllers = 0;

	for (int i = 0; i < MAX_SI_CHANNELS; ++i)
		if (SIDevice_IsGCController(SConfig::GetInstance().m_SIDevice[i]))
			controllers |= (1 << i);

	if (instantly && (s_numPads & 0x0F) == controllers)
		return;

	for (int i = 0; i < MAX_SI_CHANNELS; ++i)
	{
		SIDevices device = SIDEVICE_NONE;
		if (IsUsingPad(i))
		{
			if (SIDevice_IsGCController(SConfig::GetInstance().m_SIDevice[i]))
				device = SConfig::GetInstance().m_SIDevice[i];
			else
				device = IsUsingBongo(i) ? SIDEVICE_GC_TARUKONGA : SIDEVICE_GC_CONTROLLER;
		}

		if (instantly) // Changes from savestates need to be instantaneous
			SerialInterface::AddDevice(device, i);
		else
			SerialInterface::ChangeDevice(device, i);
	}
}

// NOTE: Host / Emu Threads
void ChangeWiiPads(bool instantly)
{
	int controllers = 0;

	for (int i = 0; i < MAX_WIIMOTES; ++i)
		if (g_wiimote_sources[i] != WIIMOTE_SRC_NONE)
			controllers |= (1 << i);

	// This is important for Wiimotes, because they can desync easily if they get re-activated
	if (instantly && (s_numPads >> 4) == controllers)
		return;

	for (int i = 0; i < MAX_WIIMOTES; ++i)
	{
		g_wiimote_sources[i] = IsUsingWiimote(i) ? WIIMOTE_SRC_EMU : WIIMOTE_SRC_NONE;
		GetUsbPointer()->AccessWiiMote(i | 0x100)->Activate(IsUsingWiimote(i));
	}
}

// NOTE: Host Thread
bool BeginRecordingInput(int controllers)
{
	if (s_playMode != MODE_NONE || controllers == 0)
		return false;

	bool was_unpaused = Core::PauseAndLock(true);

	s_numPads = controllers;
	g_currentFrame = g_totalFrames = 0;
	g_currentLagCount = s_totalLagCount = 0;
	g_currentInputCount = g_totalInputCount = 0;
	s_totalTickCount = s_tickCountAtLastInput = 0;
	s_bongos = 0;
	s_memcards = 0;
	if (NetPlay::IsNetPlayRunning())
	{
		s_bNetPlay = true;
		s_recordingStartTime = g_netplay_initial_gctime;
	}
	else
	{
		s_recordingStartTime = Common::Timer::GetLocalTimeSinceJan1970();
	}

	s_rerecords = 0;

	for (int i = 0; i < MAX_SI_CHANNELS; ++i)
		if (SConfig::GetInstance().m_SIDevice[i] == SIDEVICE_GC_TARUKONGA)
			s_bongos |= (1 << i);

	if (Core::IsRunningAndStarted())
	{
		const std::string save_path = File::GetUserPath(D_STATESAVES_IDX) + "dtm.sav";
		if (File::Exists(save_path))
			File::Delete(save_path);

		State::SaveAs(save_path);
		s_bRecordingFromSaveState = true;

		// This is only done here if starting from save state because otherwise we won't have the titleid. Otherwise it's set in WII_IPC_HLE_Device_es.cpp.
		// TODO: find a way to GetTitleDataPath() from Movie::Init()
		if (SConfig::GetInstance().bWii)
		{
			if (File::Exists(Common::GetTitleDataPath(g_titleID, Common::FROM_SESSION_ROOT) + "banner.bin"))
				Movie::g_bClearSave = false;
			else
				Movie::g_bClearSave = true;
		}
		std::thread md5thread(GetMD5);
		md5thread.detach();
		GetSettings();
	}

	// Wiimotes cause desync issues if they're not reset before launching the game
	if (!Core::IsRunningAndStarted())
	{
	        // This will also reset the wiimotes for gamecube games, but that shouldn't do anything
	        Wiimote::ResetAllWiimotes();
	}

	s_playMode = MODE_RECORDING;
	s_author = SConfig::GetInstance().m_strMovieAuthor;
	EnsureTmpInputSize(1);

	s_currentByte = s_totalBytes = 0;

	Core::UpdateWantDeterminism();

	Core::PauseAndLock(false, was_unpaused);

	Core::DisplayMessage("Starting movie recording", 2000);
	return true;
}

static std::string Analog2DToString(u8 x, u8 y, const std::string& prefix, u8 range = 255)
{
	u8 center = range / 2 + 1;
	if ((x <= 1 || x == center || x >= range) &&
	    (y <= 1 || y == center || y >= range))
	{
		if (x != center || y != center)
		{
			if (x != center && y != center)
			{
				return StringFromFormat("%s:%s,%s", prefix.c_str(), x < center ? "LEFT" : "RIGHT", y < center ? "DOWN" : "UP");
			}
			else if (x != center)
			{
				return StringFromFormat("%s:%s", prefix.c_str(), x < center ? "LEFT" : "RIGHT");
			}
			else
			{
				return StringFromFormat("%s:%s", prefix.c_str(), y < center ? "DOWN" : "UP");
			}
		}
		else
		{
			return "";
		}
	}
	else
	{
		return StringFromFormat("%s:%d,%d", prefix.c_str(), x, y);
	}
}

static std::string Analog1DToString(u8 v, const std::string& prefix, u8 range = 255)
{
	if (v > 0)
	{
		if (v == range)
		{
			return prefix;
		}
		else
		{
			return StringFromFormat("%s:%d", prefix.c_str(), v);
		}
	}
	else
	{
		return "";
	}
}

// NOTE: CPU Thread
static void SetInputDisplayString(ControllerState padState, int controllerID)
{
	std::string display_str = StringFromFormat("P%d:", controllerID + 1);

	if (padState.A)
		display_str += " A";
	if (padState.B)
		display_str += " B";
	if (padState.X)
		display_str += " X";
	if (padState.Y)
		display_str += " Y";
	if (padState.Z)
		display_str += " Z";
	if (padState.Start)
		display_str += " START";

	if (padState.DPadUp)
		display_str += " UP";
	if (padState.DPadDown)
		display_str += " DOWN";
	if (padState.DPadLeft)
		display_str += " LEFT";
	if (padState.DPadRight)
		display_str += " RIGHT";
	if (padState.reset)
		display_str += " RESET";

	display_str += Analog1DToString(padState.TriggerL, " L");
	display_str += Analog1DToString(padState.TriggerR, " R");
	display_str += Analog2DToString(padState.AnalogStickX, padState.AnalogStickY, " ANA");
	display_str += Analog2DToString(padState.CStickX, padState.CStickY, " C");
	display_str += '\n';

	std::lock_guard<std::mutex> guard(s_input_display_lock);
	s_InputDisplay[controllerID] = std::move(display_str);
}

// NOTE: CPU Thread
static void SetWiiInputDisplayString(int remoteID, u8* const data, const WiimoteEmu::ReportFeatures& rptf, int ext, const wiimote_key key)
{
	int controllerID = remoteID + 4;

	std::string display_str = StringFromFormat("R%d:", remoteID + 1);

	u8* const coreData = rptf.core ? (data + rptf.core) : nullptr;
	u8* const accelData = rptf.accel ? (data + rptf.accel) : nullptr;
	u8* const irData = rptf.ir ? (data + rptf.ir) : nullptr;
	u8* const extData = rptf.ext ? (data + rptf.ext) : nullptr;

	if (coreData)
	{
		wm_buttons buttons = *(wm_buttons*)coreData;
		if(buttons.left)
			display_str += " LEFT";
		if(buttons.right)
			display_str += " RIGHT";
		if(buttons.down)
			display_str += " DOWN";
		if(buttons.up)
			display_str += " UP";
		if(buttons.a)
			display_str += " A";
		if(buttons.b)
			display_str += " B";
		if(buttons.plus)
			display_str += " +";
		if(buttons.minus)
			display_str += " -";
		if(buttons.one)
			display_str += " 1";
		if(buttons.two)
			display_str += " 2";
		if(buttons.home)
			display_str += " HOME";
	}

	if (accelData)
	{
		wm_accel* dt = (wm_accel*)accelData;
		display_str += StringFromFormat(" ACC:%d,%d,%d",
		                                dt->x << 2 | ((wm_buttons*)coreData)->acc_x_lsb,
		                                dt->y << 2 | ((wm_buttons*)coreData)->acc_y_lsb << 1,
		                                dt->z << 2 | ((wm_buttons*)coreData)->acc_z_lsb << 1);
	}

	if (irData)
	{
		u16 x = irData[0] | ((irData[2] >> 4 & 0x3) << 8);
		u16 y = irData[1] | ((irData[2] >> 6 & 0x3) << 8);
		display_str += StringFromFormat(" IR:%d,%d", x, y);
	}

	// Nunchuk
	if (extData && ext == 1)
	{
		wm_nc nunchuk;
		memcpy(&nunchuk, extData, sizeof(wm_nc));
		WiimoteDecrypt(&key, (u8*)&nunchuk, 0, sizeof(wm_nc));
		nunchuk.bt.hex = nunchuk.bt.hex ^ 0x3;

		std::string accel = StringFromFormat(" N-ACC:%d,%d,%d",
			(nunchuk.ax << 2) | nunchuk.bt.acc_x_lsb, (nunchuk.ay << 2) | nunchuk.bt.acc_y_lsb, (nunchuk.az << 2) | nunchuk.bt.acc_z_lsb);

		if (nunchuk.bt.c)
			display_str += " C";
		if (nunchuk.bt.z)
			display_str += " Z";
		display_str += accel;
		display_str += Analog2DToString(nunchuk.jx, nunchuk.jy, " ANA");
	}

	// Classic controller
	if (extData && ext == 2)
	{
		wm_classic_extension cc;
		memcpy(&cc, extData, sizeof(wm_classic_extension));
		WiimoteDecrypt(&key, (u8*)&cc, 0, sizeof(wm_classic_extension));
		cc.bt.hex = cc.bt.hex ^ 0xFFFF;

		if (cc.bt.regular_data.dpad_left)
			display_str += " LEFT";
		if (cc.bt.dpad_right)
			display_str += " RIGHT";
		if (cc.bt.dpad_down)
			display_str += " DOWN";
		if (cc.bt.regular_data.dpad_up)
			display_str += " UP";
		if (cc.bt.a)
			display_str += " A";
		if (cc.bt.b)
			display_str += " B";
		if (cc.bt.x)
			display_str += " X";
		if (cc.bt.y)
			display_str += " Y";
		if (cc.bt.zl)
			display_str += " ZL";
		if (cc.bt.zr)
			display_str += " ZR";
		if (cc.bt.plus)
			display_str += " +";
		if (cc.bt.minus)
			display_str += " -";
		if (cc.bt.home)
			display_str += " HOME";

		display_str += Analog1DToString(cc.lt1 | (cc.lt2 << 3), " L", 31);
		display_str += Analog1DToString(cc.rt, " R", 31);
		display_str += Analog2DToString(cc.regular_data.lx, cc.regular_data.ly, " ANA", 63);
		display_str += Analog2DToString(cc.rx1 | (cc.rx2 << 1) | (cc.rx3 << 3), cc.ry, " R-ANA", 31);
	}

	display_str += '\n';

	std::lock_guard<std::mutex> guard(s_input_display_lock);
	s_InputDisplay[controllerID] = std::move(display_str);
}

// NOTE: CPU Thread
void CheckPadStatus(GCPadStatus* PadStatus, int controllerID)
{
	s_padState.A         = ((PadStatus->button & PAD_BUTTON_A) != 0);
	s_padState.B         = ((PadStatus->button & PAD_BUTTON_B) != 0);
	s_padState.X         = ((PadStatus->button & PAD_BUTTON_X) != 0);
	s_padState.Y         = ((PadStatus->button & PAD_BUTTON_Y) != 0);
	s_padState.Z         = ((PadStatus->button & PAD_TRIGGER_Z) != 0);
	s_padState.Start     = ((PadStatus->button & PAD_BUTTON_START) != 0);

	s_padState.DPadUp    = ((PadStatus->button & PAD_BUTTON_UP) != 0);
	s_padState.DPadDown  = ((PadStatus->button & PAD_BUTTON_DOWN) != 0);
	s_padState.DPadLeft  = ((PadStatus->button & PAD_BUTTON_LEFT) != 0);
	s_padState.DPadRight = ((PadStatus->button & PAD_BUTTON_RIGHT) != 0);

	s_padState.L         = ((PadStatus->button & PAD_TRIGGER_L) != 0);
	s_padState.R         = ((PadStatus->button & PAD_TRIGGER_R) != 0);
	s_padState.TriggerL  = PadStatus->triggerLeft;
	s_padState.TriggerR  = PadStatus->triggerRight;

	s_padState.AnalogStickX = PadStatus->stickX;
	s_padState.AnalogStickY = PadStatus->stickY;

	s_padState.CStickX   = PadStatus->substickX;
	s_padState.CStickY   = PadStatus->substickY;

	s_padState.disc = g_bDiscChange;
	g_bDiscChange = false;
	s_padState.reset = g_bReset;
	g_bReset = false;

	SetInputDisplayString(s_padState, controllerID);
}

// NOTE: CPU Thread
void RecordInput(GCPadStatus* PadStatus, int controllerID)
{
	if (!IsRecordingInput() || !IsUsingPad(controllerID))
		return;

	CheckPadStatus(PadStatus, controllerID);

	EnsureTmpInputSize((size_t)(s_currentByte + 8));
	memcpy(&(tmpInput[s_currentByte]), &s_padState, 8);
	s_currentByte += 8;
	s_totalBytes = s_currentByte;
}

// NOTE: CPU Thread
void CheckWiimoteStatus(int wiimote, u8 *data, const WiimoteEmu::ReportFeatures& rptf, int ext, const wiimote_key key)
{
	SetWiiInputDisplayString(wiimote, data, rptf, ext, key);

	if (IsRecordingInput())
		RecordWiimote(wiimote, data, rptf.size);
}

void RecordWiimote(int wiimote, u8 *data, u8 size)
{
	if (!IsRecordingInput() || !IsUsingWiimote(wiimote))
		return;

	InputUpdate();
	EnsureTmpInputSize((size_t)(s_currentByte + size + 1));
	tmpInput[s_currentByte++] = size;
	memcpy(&(tmpInput[s_currentByte]), data, size);
	s_currentByte += size;
	s_totalBytes = s_currentByte;
}

// NOTE: EmuThread / Host Thread
void ReadHeader()
{
	s_numPads = tmpHeader.numControllers;
	s_recordingStartTime = tmpHeader.recordingStartTime;
	if (s_rerecords < tmpHeader.numRerecords)
		s_rerecords = tmpHeader.numRerecords;

	if (tmpHeader.bSaveConfig)
	{
		s_bSaveConfig = true;
		s_bSkipIdle = tmpHeader.bSkipIdle;
		s_bDualCore = tmpHeader.bDualCore;
		s_bProgressive = tmpHeader.bProgressive;
		s_bPAL60 = tmpHeader.bPAL60;
		s_bDSPHLE = tmpHeader.bDSPHLE;
		s_bFastDiscSpeed = tmpHeader.bFastDiscSpeed;
		s_iCPUCore = tmpHeader.CPUCore;
		g_bClearSave = tmpHeader.bClearSave;
		s_memcards = tmpHeader.memcards;
		s_bongos = tmpHeader.bongos;
		s_bSyncGPU = tmpHeader.bSyncGPU;
		s_bNetPlay = tmpHeader.bNetPlay;
		s_language = tmpHeader.language;
		memcpy(s_revision, tmpHeader.revision, ArraySize(s_revision));
	}
	else
	{
		GetSettings();
	}

	s_videoBackend = (char*) tmpHeader.videoBackend;
	g_discChange = (char*) tmpHeader.discChange;
	s_author = (char*) tmpHeader.author;
	memcpy(s_MD5, tmpHeader.md5, 16);
	s_DSPiromHash = tmpHeader.DSPiromHash;
	s_DSPcoefHash = tmpHeader.DSPcoefHash;
}

// NOTE: Host Thread
bool PlayInput(const std::string& filename)
{
	if (s_playMode != MODE_NONE)
		return false;

	if (!File::Exists(filename))
		return false;

	File::IOFile g_recordfd;

	if (!g_recordfd.Open(filename, "rb"))
		return false;

	g_recordfd.ReadArray(&tmpHeader, 1);

	if (!IsMovieHeader(tmpHeader.filetype))
	{
		PanicAlertT("Invalid recording file");
		g_recordfd.Close();
		return false;
	}

	ReadHeader();
	g_totalFrames = tmpHeader.frameCount;
	s_totalLagCount = tmpHeader.lagCount;
	g_totalInputCount = tmpHeader.inputCount;
	s_totalTickCount = tmpHeader.tickCount;
	g_currentFrame = 0;
	g_currentLagCount = 0;
	g_currentInputCount = 0;

	s_playMode = MODE_PLAYING;

	// Wiimotes cause desync issues if they're not reset before launching the game
	Wiimote::ResetAllWiimotes();

	Core::UpdateWantDeterminism();

	s_totalBytes = g_recordfd.GetSize() - 256;
	EnsureTmpInputSize((size_t)s_totalBytes);
	g_recordfd.ReadArray(tmpInput, (size_t)s_totalBytes);
	s_currentByte = 0;
	g_recordfd.Close();

	// Load savestate (and skip to frame data)
	if (tmpHeader.bFromSaveState)
	{
		const std::string stateFilename = filename + ".sav";
		if (File::Exists(stateFilename))
			Core::SetStateFileName(stateFilename);
		s_bRecordingFromSaveState = true;
		Movie::LoadInput(filename);
	}

	return true;
}

void DoState(PointerWrap &p)
{
	// many of these could be useful to save even when no movie is active,
	// and the data is tiny, so let's just save it regardless of movie state.
	p.Do(g_currentFrame);
	p.Do(s_currentByte);
	p.Do(g_currentLagCount);
	p.Do(g_currentInputCount);
	p.Do(s_bPolled);
	p.Do(s_tickCountAtLastInput);
	// other variables (such as s_totalBytes and g_totalFrames) are set in LoadInput
}

// NOTE: Host Thread
void LoadInput(const std::string& filename)
{
	File::IOFile t_record;
	if (!t_record.Open(filename, "r+b"))
	{
		PanicAlertT("Failed to read %s", filename.c_str());
		EndPlayInput(false);
		return;
	}

	t_record.ReadArray(&tmpHeader, 1);

	if (!IsMovieHeader(tmpHeader.filetype))
	{
		PanicAlertT("Savestate movie %s is corrupted, movie recording stopping...", filename.c_str());
		EndPlayInput(false);
		return;
	}
	ReadHeader();
	if (!s_bReadOnly)
	{
		s_rerecords++;
		tmpHeader.numRerecords = s_rerecords;
		t_record.Seek(0, SEEK_SET);
		t_record.WriteArray(&tmpHeader, 1);
	}

	ChangePads(true);
	if (SConfig::GetInstance().bWii)
		ChangeWiiPads(true);

	u64 totalSavedBytes = t_record.GetSize() - 256;

	bool afterEnd = false;
	// This can only happen if the user manually deletes data from the dtm.
	if (s_currentByte > totalSavedBytes)
	{
		PanicAlertT("Warning: You loaded a save whose movie ends before the current frame in the save (byte %u < %u) (frame %u < %u). You should load another save before continuing.", (u32)totalSavedBytes+256, (u32)s_currentByte+256, (u32)tmpHeader.frameCount, (u32)g_currentFrame);
		afterEnd = true;
	}

	if (!s_bReadOnly || tmpInput == nullptr)
	{
		g_totalFrames = tmpHeader.frameCount;
		s_totalLagCount = tmpHeader.lagCount;
		g_totalInputCount = tmpHeader.inputCount;
		s_totalTickCount = s_tickCountAtLastInput = tmpHeader.tickCount;

		EnsureTmpInputSize((size_t)totalSavedBytes);
		s_totalBytes = totalSavedBytes;
		t_record.ReadArray(tmpInput, (size_t)s_totalBytes);
	}
	else if (s_currentByte > 0)
	{
		if (s_currentByte > totalSavedBytes)
		{
		}
		else if (s_currentByte > s_totalBytes)
		{
			afterEnd = true;
			PanicAlertT("Warning: You loaded a save that's after the end of the current movie. (byte %u > %u) (frame %u > %u). You should load another save before continuing, or load this state with read-only mode off.", (u32)s_currentByte+256, (u32)s_totalBytes+256, (u32)g_currentFrame, (u32)g_totalFrames);
		}
		else if (s_currentByte > 0 && s_totalBytes > 0)
		{
			// verify identical from movie start to the save's current frame
			u32 len = (u32)s_currentByte;
			u8* movInput = new u8[len];
			t_record.ReadArray(movInput, (size_t)len);
			for (u32 i = 0; i < len; ++i)
			{
				if (movInput[i] != tmpInput[i])
				{
					// this is a "you did something wrong" alert for the user's benefit.
					// we'll try to say what's going on in excruciating detail, otherwise the user might not believe us.
					if (IsUsingWiimote(0))
					{
						// TODO: more detail
						PanicAlertT("Warning: You loaded a save whose movie mismatches on byte %d (0x%X). You should load another save before continuing, or load this state with read-only mode off. Otherwise you'll probably get a desync.", i+256, i+256);
						memcpy(tmpInput, movInput, s_currentByte);
					}
					else
					{
						int frame = i / 8;
						ControllerState curPadState;
						memcpy(&curPadState, &(tmpInput[frame*8]), 8);
						ControllerState movPadState;
						memcpy(&movPadState, &(movInput[frame*8]), 8);
						PanicAlertT("Warning: You loaded a save whose movie mismatches on frame %d. You should load another save before continuing, or load this state with read-only mode off. Otherwise you'll probably get a desync.\n\n"
							"More information: The current movie is %d frames long and the savestate's movie is %d frames long.\n\n"
							"On frame %d, the current movie presses:\n"
							"Start=%d, A=%d, B=%d, X=%d, Y=%d, Z=%d, DUp=%d, DDown=%d, DLeft=%d, DRight=%d, L=%d, R=%d, LT=%d, RT=%d, AnalogX=%d, AnalogY=%d, CX=%d, CY=%d"
							"\n\n"
							"On frame %d, the savestate's movie presses:\n"
							"Start=%d, A=%d, B=%d, X=%d, Y=%d, Z=%d, DUp=%d, DDown=%d, DLeft=%d, DRight=%d, L=%d, R=%d, LT=%d, RT=%d, AnalogX=%d, AnalogY=%d, CX=%d, CY=%d",
							(int)frame,
							(int)g_totalFrames, (int)tmpHeader.frameCount,
							(int)frame,
							(int)curPadState.Start, (int)curPadState.A, (int)curPadState.B, (int)curPadState.X, (int)curPadState.Y, (int)curPadState.Z, (int)curPadState.DPadUp, (int)curPadState.DPadDown, (int)curPadState.DPadLeft, (int)curPadState.DPadRight, (int)curPadState.L, (int)curPadState.R, (int)curPadState.TriggerL, (int)curPadState.TriggerR, (int)curPadState.AnalogStickX, (int)curPadState.AnalogStickY, (int)curPadState.CStickX, (int)curPadState.CStickY,
							(int)frame,
							(int)movPadState.Start, (int)movPadState.A, (int)movPadState.B, (int)movPadState.X, (int)movPadState.Y, (int)movPadState.Z, (int)movPadState.DPadUp, (int)movPadState.DPadDown, (int)movPadState.DPadLeft, (int)movPadState.DPadRight, (int)movPadState.L, (int)movPadState.R, (int)movPadState.TriggerL, (int)movPadState.TriggerR, (int)movPadState.AnalogStickX, (int)movPadState.AnalogStickY, (int)movPadState.CStickX, (int)movPadState.CStickY);

					}
					break;
				}
			}
			delete [] movInput;
		}
	}
	t_record.Close();

	s_bSaveConfig = tmpHeader.bSaveConfig;

	if (!afterEnd)
	{
		if (s_bReadOnly)
		{
			if (s_playMode != MODE_PLAYING)
			{
				s_playMode = MODE_PLAYING;
				Core::DisplayMessage("Switched to playback", 2000);
			}
		}
		else
		{
			if (s_playMode != MODE_RECORDING)
			{
				s_playMode = MODE_RECORDING;
				Core::DisplayMessage("Switched to recording", 2000);
			}
		}
	}
	else
	{
		EndPlayInput(false);
	}
}

// NOTE: CPU Thread
static void CheckInputEnd()
{
	if (g_currentFrame > g_totalFrames || s_currentByte >= s_totalBytes || (CoreTiming::GetTicks() > s_totalTickCount && !IsRecordingInputFromSaveState()))
	{
		EndPlayInput(!s_bReadOnly);
	}
}

// NOTE: CPU Thread
void PlayController(GCPadStatus* PadStatus, int controllerID)
{
	// Correct playback is entirely dependent on the emulator polling the controllers
	// in the same order done during recording
	if (!IsPlayingInput() || !IsUsingPad(controllerID) || tmpInput == nullptr)
		return;

	if (s_currentByte + 8 > s_totalBytes)
	{
		PanicAlertT("Premature movie end in PlayController. %u + 8 > %u", (u32)s_currentByte, (u32)s_totalBytes);
		EndPlayInput(!s_bReadOnly);
		return;
	}

	// dtm files don't save the mic button or error bit. not sure if they're actually used, but better safe than sorry
	signed char e = PadStatus->err;
	memset(PadStatus, 0, sizeof(GCPadStatus));
	PadStatus->err = e;


	memcpy(&s_padState, &(tmpInput[s_currentByte]), 8);
	s_currentByte += 8;

	PadStatus->triggerLeft = s_padState.TriggerL;
	PadStatus->triggerRight = s_padState.TriggerR;

	PadStatus->stickX = s_padState.AnalogStickX;
	PadStatus->stickY = s_padState.AnalogStickY;

	PadStatus->substickX = s_padState.CStickX;
	PadStatus->substickY = s_padState.CStickY;

	PadStatus->button |= PAD_USE_ORIGIN;

	if (s_padState.A)
	{
		PadStatus->button |= PAD_BUTTON_A;
		PadStatus->analogA = 0xFF;
	}
	if (s_padState.B)
	{
		PadStatus->button |= PAD_BUTTON_B;
		PadStatus->analogB = 0xFF;
	}
	if (s_padState.X)
		PadStatus->button |= PAD_BUTTON_X;
	if (s_padState.Y)
		PadStatus->button |= PAD_BUTTON_Y;
	if (s_padState.Z)
		PadStatus->button |= PAD_TRIGGER_Z;
	if (s_padState.Start)
		PadStatus->button |= PAD_BUTTON_START;

	if (s_padState.DPadUp)
		PadStatus->button |= PAD_BUTTON_UP;
	if (s_padState.DPadDown)
		PadStatus->button |= PAD_BUTTON_DOWN;
	if (s_padState.DPadLeft)
		PadStatus->button |= PAD_BUTTON_LEFT;
	if (s_padState.DPadRight)
		PadStatus->button |= PAD_BUTTON_RIGHT;

	if (s_padState.L)
		PadStatus->button |= PAD_TRIGGER_L;
	if (s_padState.R)
		PadStatus->button |= PAD_TRIGGER_R;
	if (s_padState.disc)
	{
		// This implementation assumes the disc change will only happen once. Trying to change more than that will cause
		// it to load the last disc every time. As far as i know though, there are no 3+ disc games, so this should be fine.
		CPU::Break();
		bool found = false;
		std::string path;
		for (size_t i = 0; i < SConfig::GetInstance().m_ISOFolder.size(); ++i)
		{
			path = SConfig::GetInstance().m_ISOFolder[i];
			if (File::Exists(path + '/' + g_discChange))
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			path += '/' + g_discChange;

			Core::QueueHostJob([=]
			{
				if (!Movie::IsPlayingInput())
					return;

				DVDInterface::ChangeDisc(path);
				CPU::EnableStepping(false);
			});
		}
		else
		{
			PanicAlertT("Change the disc to %s", g_discChange.c_str());
		}
	}

	if (s_padState.reset)
		ProcessorInterface::ResetButton_Tap();

	SetInputDisplayString(s_padState, controllerID);
	CheckInputEnd();
}

// NOTE: CPU Thread
bool PlayWiimote(int wiimote, u8 *data, const WiimoteEmu::ReportFeatures& rptf, int ext, const wiimote_key key)
{
	if (!IsPlayingInput() || !IsUsingWiimote(wiimote) || tmpInput == nullptr)
		return false;

	if (s_currentByte > s_totalBytes)
	{
		PanicAlertT("Premature movie end in PlayWiimote. %u > %u", (u32)s_currentByte, (u32)s_totalBytes);
		EndPlayInput(!s_bReadOnly);
		return false;
	}

	u8 size = rptf.size;

	u8 sizeInMovie = tmpInput[s_currentByte];

	if (size != sizeInMovie)
	{
		PanicAlertT("Fatal desync. Aborting playback. (Error in PlayWiimote: %u != %u, byte %u.)%s", (u32)sizeInMovie, (u32)size, (u32)s_currentByte,
					(s_numPads & 0xF)?" Try re-creating the recording with all GameCube controllers disabled (in Configure > GameCube > Device Settings)." : "");
		EndPlayInput(!s_bReadOnly);
		return false;
	}

	s_currentByte++;

	if (s_currentByte + size > s_totalBytes)
	{
		PanicAlertT("Premature movie end in PlayWiimote. %u + %d > %u", (u32)s_currentByte, size, (u32)s_totalBytes);
		EndPlayInput(!s_bReadOnly);
		return false;
	}

	memcpy(data, &(tmpInput[s_currentByte]), size);
	s_currentByte += size;

	g_currentInputCount++;

	CheckInputEnd();
	return true;
}

// NOTE: Host / EmuThread / CPU Thread
void EndPlayInput(bool cont)
{
	if (cont)
	{
		s_playMode = MODE_RECORDING;
		Core::DisplayMessage("Reached movie end. Resuming recording.", 2000);
	}
	else if (s_playMode != MODE_NONE)
	{
		// We can be called by EmuThread during boot (CPU_POWERDOWN)
		bool was_running = Core::IsRunningAndStarted() && !CPU::IsStepping();
		if (was_running)
			CPU::Break();
		s_rerecords = 0;
		s_currentByte = 0;
		s_playMode = MODE_NONE;
		Core::DisplayMessage("Movie End.", 2000);
		s_bRecordingFromSaveState = false;
		// we don't clear these things because otherwise we can't resume playback if we load a movie state later
		//g_totalFrames = s_totalBytes = 0;
		//delete tmpInput;
		//tmpInput = nullptr;

		Core::QueueHostJob([=]
		{
			Core::UpdateWantDeterminism();
			if (was_running && !SConfig::GetInstance().m_PauseMovie)
				CPU::EnableStepping(false);
		});
	}
}

// NOTE: Save State + Host Thread
void SaveRecording(const std::string& filename)
{
	File::IOFile save_record(filename, "wb");
	// Create the real header now and write it
	DTMHeader header;
	memset(&header, 0, sizeof(DTMHeader));

	header.filetype[0] = 'D'; header.filetype[1] = 'T'; header.filetype[2] = 'M'; header.filetype[3] = 0x1A;
	strncpy(header.gameID, SConfig::GetInstance().GetUniqueID().c_str(), 6);
	header.bWii = SConfig::GetInstance().bWii;
	header.numControllers = s_numPads & (SConfig::GetInstance().bWii ? 0xFF : 0x0F);

	header.bFromSaveState = s_bRecordingFromSaveState;
	header.frameCount = g_totalFrames;
	header.lagCount = s_totalLagCount;
	header.inputCount = g_totalInputCount;
	header.numRerecords = s_rerecords;
	header.recordingStartTime = s_recordingStartTime;

	header.bSaveConfig = true;
	header.bSkipIdle = s_bSkipIdle;
	header.bDualCore = s_bDualCore;
	header.bProgressive = s_bProgressive;
	header.bPAL60 = s_bPAL60;
	header.bDSPHLE = s_bDSPHLE;
	header.bFastDiscSpeed = s_bFastDiscSpeed;
	strncpy((char *)header.videoBackend, s_videoBackend.c_str(),ArraySize(header.videoBackend));
	header.CPUCore = s_iCPUCore;
	header.bEFBAccessEnable = g_ActiveConfig.bEFBAccessEnable;
	header.bEFBCopyEnable = true;
	header.bSkipEFBCopyToRam = g_ActiveConfig.bSkipEFBCopyToRam;
	header.bEFBCopyCacheEnable = false;
	header.bEFBEmulateFormatChanges = g_ActiveConfig.bEFBEmulateFormatChanges;
	header.bUseXFB = g_ActiveConfig.bUseXFB;
	header.bUseRealXFB = g_ActiveConfig.bUseRealXFB;
	header.memcards = s_memcards;
	header.bClearSave = g_bClearSave;
	header.bSyncGPU = s_bSyncGPU;
	header.bNetPlay = s_bNetPlay;
	strncpy((char *)header.discChange, g_discChange.c_str(),ArraySize(header.discChange));
	strncpy((char *)header.author, s_author.c_str(),ArraySize(header.author));
	memcpy(header.md5,s_MD5,16);
	header.bongos = s_bongos;
	memcpy(header.revision, s_revision, ArraySize(header.revision));
	header.DSPiromHash = s_DSPiromHash;
	header.DSPcoefHash = s_DSPcoefHash;
	header.tickCount = s_totalTickCount;
	header.language = s_language;

	// TODO
	header.uniqueID = 0;
	// header.audioEmulator;

	save_record.WriteArray(&header, 1);

	bool success = save_record.WriteArray(tmpInput, (size_t)s_totalBytes);

	if (success && s_bRecordingFromSaveState)
	{
		std::string stateFilename = filename + ".sav";
		success = File::Copy(File::GetUserPath(D_STATESAVES_IDX) + "dtm.sav", stateFilename);
	}

	if (success)
		Core::DisplayMessage(StringFromFormat("DTM %s saved", filename.c_str()), 2000);
	else
		Core::DisplayMessage(StringFromFormat("Failed to save %s", filename.c_str()), 2000);
}

void SetGCInputManip(GCManipFunction func)
{
	gcmfunc = func;
}
void SetWiiInputManip(WiiManipFunction func)
{
	wiimfunc = func;
}

// NOTE: CPU Thread
void CallGCInputManip(GCPadStatus* PadStatus, int controllerID)
{
	if (gcmfunc)
		(*gcmfunc)(PadStatus, controllerID);
}
// NOTE: CPU Thread
void CallWiiInputManip(u8* data, WiimoteEmu::ReportFeatures rptf, int controllerID, int ext, const wiimote_key key)
{
	if (wiimfunc)
		(*wiimfunc)(data, rptf, controllerID, ext, key);
}

// NOTE: GPU Thread
void SetGraphicsConfig()
{
	g_Config.bEFBAccessEnable = tmpHeader.bEFBAccessEnable;
	g_Config.bSkipEFBCopyToRam = tmpHeader.bSkipEFBCopyToRam;
	g_Config.bEFBEmulateFormatChanges = tmpHeader.bEFBEmulateFormatChanges;
	g_Config.bUseXFB = tmpHeader.bUseXFB;
	g_Config.bUseRealXFB = tmpHeader.bUseRealXFB;
}

// NOTE: EmuThread / Host Thread
void GetSettings()
{
	s_bSaveConfig = true;
	s_bSkipIdle = SConfig::GetInstance().bSkipIdle;
	s_bDualCore = SConfig::GetInstance().bCPUThread;
	s_bProgressive = SConfig::GetInstance().bProgressive;
	s_bPAL60 = SConfig::GetInstance().bPAL60;
	s_bDSPHLE = SConfig::GetInstance().bDSPHLE;
	s_bFastDiscSpeed = SConfig::GetInstance().bFastDiscSpeed;
	s_videoBackend = g_video_backend->GetName();
	s_bSyncGPU = SConfig::GetInstance().bSyncGPU;
	s_iCPUCore = SConfig::GetInstance().iCPUCore;
	s_bNetPlay = NetPlay::IsNetPlayRunning();
	s_language = SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.LNG");
	if (!SConfig::GetInstance().bWii)
		g_bClearSave = !File::Exists(SConfig::GetInstance().m_strMemoryCardA);
	s_memcards |= (SConfig::GetInstance().m_EXIDevice[0] == EXIDEVICE_MEMORYCARD) << 0;
	s_memcards |= (SConfig::GetInstance().m_EXIDevice[1] == EXIDEVICE_MEMORYCARD) << 1;

	std::array<u8, 20> revision = ConvertGitRevisionToBytes(scm_rev_git_str);
	std::copy(std::begin(revision), std::end(revision), std::begin(s_revision));

	if (!s_bDSPHLE)
	{
		std::string irom_file = File::GetUserPath(D_GCUSER_IDX) + DSP_IROM;
		std::string coef_file = File::GetUserPath(D_GCUSER_IDX) + DSP_COEF;

		if (!File::Exists(irom_file))
			irom_file = File::GetSysDirectory() + GC_SYS_DIR DIR_SEP DSP_IROM;
		if (!File::Exists(coef_file))
			coef_file = File::GetSysDirectory() + GC_SYS_DIR DIR_SEP DSP_COEF;
		std::vector<u16> irom(DSP_IROM_SIZE);
		File::IOFile file_irom(irom_file, "rb");

		file_irom.ReadArray(irom.data(), DSP_IROM_SIZE);
		file_irom.Close();
		for (u32 i = 0; i < DSP_IROM_SIZE; ++i)
			irom[i] = Common::swap16(irom[i]);

		std::vector<u16> coef(DSP_COEF_SIZE);
		File::IOFile file_coef(coef_file, "rb");

		file_coef.ReadArray(coef.data(), DSP_COEF_SIZE);
		file_coef.Close();
		for (u32 i = 0; i < DSP_COEF_SIZE; ++i)
			coef[i] = Common::swap16(coef[i]);
		s_DSPiromHash = HashAdler32((u8*)irom.data(), DSP_IROM_BYTE_SIZE);
		s_DSPcoefHash = HashAdler32((u8*)coef.data(), DSP_COEF_BYTE_SIZE);
	}
	else
	{
		s_DSPiromHash = 0;
		s_DSPcoefHash = 0;
	}
}

static const mbedtls_md_info_t* s_md5_info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);

// NOTE: Entrypoint for own thread
void CheckMD5()
{
	for (int i = 0, n = 0; i < 16; ++i)
	{
		if (tmpHeader.md5[i] != 0)
			continue;
		n++;
		if (n == 16)
			return;
	}
	Core::DisplayMessage("Verifying checksum...", 2000);

	unsigned char gameMD5[16];
	mbedtls_md_file(s_md5_info, SConfig::GetInstance().m_strFilename.c_str(), gameMD5);

	if (memcmp(gameMD5,s_MD5,16) == 0)
		Core::DisplayMessage("Checksum of current game matches the recorded game.", 2000);
	else
		Core::DisplayMessage("Checksum of current game does not match the recorded game!", 3000);
}

// NOTE: Entrypoint for own thread
void GetMD5()
{
	Core::DisplayMessage("Calculating checksum of game file...", 2000);
	memset(s_MD5, 0, sizeof(s_MD5));
	mbedtls_md_file(s_md5_info, SConfig::GetInstance().m_strFilename.c_str(), s_MD5);
	Core::DisplayMessage("Finished calculating checksum.", 2000);
}

// NOTE: EmuThread
void Shutdown()
{
	g_currentInputCount = g_totalInputCount = g_totalFrames = s_totalBytes = s_tickCountAtLastInput = 0;
	delete [] tmpInput;
	tmpInput = nullptr;
	tmpInputAllocated = 0;
}
};
