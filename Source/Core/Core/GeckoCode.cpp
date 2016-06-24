// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <mutex>
#include <vector>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Thread.h"

#include "Core/ConfigManager.h"
#include "Core/GeckoCode.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"

namespace Gecko
{

static const u32 INSTALLER_BASE_ADDRESS = 0x80001800;
static const u32 INSTALLER_END_ADDRESS = 0x80003000;

// return true if a code exists
bool GeckoCode::Exist(u32 address, u32 data) const
{
	for (const GeckoCode::Code& code : codes)
	{
		if (code.address == address && code.data == data)
			return true;
	}

	return false;
}

// return true if the code is identical
bool GeckoCode::Compare(const GeckoCode& compare) const
{
	if (codes.size() != compare.codes.size())
		return false;

	unsigned int exist = 0;

	for (const GeckoCode::Code& code : codes)
	{
		if (compare.Exist(code.address, code.data))
			exist++;
	}

	return exist == codes.size();
}

static bool code_handler_installed = false;
// the currently active codes
static std::vector<GeckoCode> active_codes;
static std::mutex active_codes_lock;

void SetActiveCodes(const std::vector<GeckoCode>& gcodes)
{
	std::lock_guard<std::mutex> lk(active_codes_lock);

	active_codes.clear();

	// add enabled codes
	for (const GeckoCode& gecko_code : gcodes)
	{
		if (gecko_code.enabled)
		{
			// TODO: apply modifiers
			// TODO: don't need description or creator string, just takin up memory
			active_codes.push_back(gecko_code);
		}
	}

	code_handler_installed = false;
}

static bool InstallCodeHandler()
{
	std::string data;
	std::string _rCodeHandlerFilename = File::GetSysDirectory() + GECKO_CODE_HANDLER;
	if (!File::ReadFileToString(_rCodeHandlerFilename, data))
	{
		NOTICE_LOG(ACTIONREPLAY, "Could not enable cheats because codehandler.bin was missing.");
		return false;
	}

	u8 mmioAddr = 0xCC;

	if (SConfig::GetInstance().bWii)
	{
		mmioAddr = 0xCD;
	}

	// Install code handler
	for (size_t i = 0, e = data.length(); i < e; ++i)
		PowerPC::HostWrite_U8(data[i], (u32)(INSTALLER_BASE_ADDRESS + i));

	// Patch the code handler to the system starting up
	for (unsigned int h = 0; h < data.length(); h += 4)
	{
		// Patch MMIO address
		if (PowerPC::HostRead_U32(INSTALLER_BASE_ADDRESS + h) == (0x3f000000u | ((mmioAddr ^ 1) << 8)))
		{
			NOTICE_LOG(ACTIONREPLAY, "Patching MMIO access at %08x", INSTALLER_BASE_ADDRESS + h);
			PowerPC::HostWrite_U32(0x3f000000u | mmioAddr << 8, INSTALLER_BASE_ADDRESS + h);
		}
	}

	u32 codelist_base_address = INSTALLER_BASE_ADDRESS + (u32)data.length() - 8;
	u32 codelist_end_address = INSTALLER_END_ADDRESS;

	// Write a magic value to 'gameid' (codehandleronly does not actually read this).
	PowerPC::HostWrite_U32(0xd01f1bad, INSTALLER_BASE_ADDRESS);

	// Create GCT in memory
	PowerPC::HostWrite_U32(0x00d0c0de, codelist_base_address);
	PowerPC::HostWrite_U32(0x00d0c0de, codelist_base_address + 4);

	std::lock_guard<std::mutex> lk(active_codes_lock);

	int i = 0;

	for (const GeckoCode& active_code : active_codes)
	{
		if (active_code.enabled)
		{
			for (const GeckoCode::Code& code : active_code.codes)
			{
				// Make sure we have enough memory to hold the code list
				if ((codelist_base_address + 24 + i) < codelist_end_address)
				{
					PowerPC::HostWrite_U32(code.address, codelist_base_address + 8 + i);
					PowerPC::HostWrite_U32(code.data, codelist_base_address + 12 + i);
					i += 8;
				}
			}
		}
	}

	PowerPC::HostWrite_U32(0xff000000, codelist_base_address + 8 + i);
	PowerPC::HostWrite_U32(0x00000000, codelist_base_address + 12 + i);

	// Turn on codes
	PowerPC::HostWrite_U8(1, INSTALLER_BASE_ADDRESS + 7);

	// Invalidate the icache and any asm codes
	for (unsigned int j = 0; j < (INSTALLER_END_ADDRESS - INSTALLER_BASE_ADDRESS); j += 32)
	{
		PowerPC::ppcState.iCache.Invalidate(INSTALLER_BASE_ADDRESS + j);
	}
	for (unsigned int k = codelist_base_address; k < codelist_end_address; k += 32)
	{
		PowerPC::ppcState.iCache.Invalidate(k);
	}
	return true;
}

void RunCodeHandler()
{
	if (SConfig::GetInstance().bEnableCheats && active_codes.size() > 0)
	{
		if (!code_handler_installed || PowerPC::HostRead_U32(INSTALLER_BASE_ADDRESS) - 0xd01f1bad > 5)
			code_handler_installed = InstallCodeHandler();

		if (!code_handler_installed)
		{
			// A warning was already issued.
			return;
		}

		if (PC == LR)
		{
			u32 oldLR = LR;
			PowerPC::CoreMode oldMode = PowerPC::GetMode();

			PC = INSTALLER_BASE_ADDRESS + 0xA8;
			LR = 0;

			// Execute the code handler in interpreter mode to track when it exits
			PowerPC::SetMode(PowerPC::MODE_INTERPRETER);

			while (PC != 0)
				PowerPC::SingleStep();

			PowerPC::SetMode(oldMode);
			PC = LR = oldLR;
		}
	}
}

} // namespace Gecko
