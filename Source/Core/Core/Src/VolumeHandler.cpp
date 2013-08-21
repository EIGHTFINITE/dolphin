// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "VolumeHandler.h"
#include "VolumeCreator.h"

namespace VolumeHandler
{

DiscIO::IVolume* g_pVolume = NULL;

DiscIO::IVolume *GetVolume()
{
	return g_pVolume;
}

void EjectVolume() 
{
	if (g_pVolume)
	{
		// This code looks scary. Can the try/catch stuff be removed?
		// This cause a "Unhandled exception ... Access violation
		// reading location ..." after you have started and stopped two
		// or three games
		delete g_pVolume;
		g_pVolume = NULL;
	}
}

bool SetVolumeName(const std::string& _rFullPath)
{
	if (g_pVolume)
	{
		delete g_pVolume;
		g_pVolume = NULL;
	}

	g_pVolume = DiscIO::CreateVolumeFromFilename(_rFullPath);

	return (g_pVolume != NULL);
}

void SetVolumeDirectory(const std::string& _rFullPath, bool _bIsWii, const std::string& _rApploader, const std::string& _rDOL)
{
	if (g_pVolume)
	{
		delete g_pVolume;
		g_pVolume = NULL;
	}

	g_pVolume = DiscIO::CreateVolumeFromDirectory(_rFullPath, _bIsWii, _rApploader, _rDOL);
}

u32 Read32(u64 _Offset)
{
	if (g_pVolume != NULL)
	{
		u32 Temp;
		g_pVolume->Read(_Offset, 4, (u8*)&Temp);
		return Common::swap32(Temp);
	}
	return 0;
}

bool ReadToPtr(u8* ptr, u64 _dwOffset, u64 _dwLength)
{
	if (g_pVolume != NULL && ptr)
	{
		g_pVolume->Read(_dwOffset, _dwLength, ptr);
		return true;
	}
	return false;
}

bool RAWReadToPtr( u8* ptr, u64 _dwOffset, u64 _dwLength )
{
	if (g_pVolume != NULL && ptr)
	{
		g_pVolume->RAWRead(_dwOffset, _dwLength, ptr);
		return true;
	}
	return false;
}

bool IsValid()
{
	return (g_pVolume != NULL);
}

bool IsWii()
{
	if (g_pVolume)
		return IsVolumeWiiDisc(g_pVolume);

	return false;
}

} // end of namespace VolumeHandler
