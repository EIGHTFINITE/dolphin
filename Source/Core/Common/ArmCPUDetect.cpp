// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <asm/hwcap.h>
#include <sys/auxv.h>

#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtil.h"

const char procfile[] = "/proc/cpuinfo";

static std::string GetCPUString()
{
	const std::string marker = "Hardware\t: ";
	std::string cpu_string = "Unknown";

	std::string line;
	std::ifstream file(procfile);

	if (!file)
		return cpu_string;

	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
		{
			cpu_string = line.substr(marker.length());
			break;
		}
	}

	return cpu_string;
}

CPUInfo cpu_info;

CPUInfo::CPUInfo()
{
	Detect();
}

// Detects the various CPU features
void CPUInfo::Detect()
{
	// Set some defaults here
	// When ARMv8 CPUs come out, these need to be updated.
	HTT = false;
	OS64bit = true;
	CPU64bit = true;
	Mode64bit = true;
	vendor = VENDOR_ARM;

	// Get the information about the CPU
	num_cores = sysconf(_SC_NPROCESSORS_CONF);
	strncpy(cpu_string, GetCPUString().c_str(), sizeof(cpu_string));

	unsigned long hwcaps = getauxval(AT_HWCAP);
	bFP = hwcaps & HWCAP_FP;
	bASIMD = hwcaps & HWCAP_ASIMD;
	bAES = hwcaps & HWCAP_AES;
	bCRC32 = hwcaps & HWCAP_CRC32;
	bSHA1 = hwcaps & HWCAP_SHA1;
	bSHA2 = hwcaps & HWCAP_SHA2;
}

// Turn the CPU info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);

	if (bAES) sum += ", AES";
	if (bCRC32) sum += ", CRC32";
	if (bSHA1) sum += ", SHA1";
	if (bSHA2) sum += ", SHA2";
	if (CPU64bit) sum += ", 64-bit";

	return sum;
}
