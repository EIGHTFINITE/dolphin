// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "FileUtil.h"

#include <string>
#include <vector>
#include <algorithm>

#include "FileSystemGCWii.h"
#include "StringUtil.h"

namespace DiscIO
{
CFileSystemGCWii::CFileSystemGCWii(const IVolume *_rVolume)
	: IFileSystem(_rVolume)
	, m_Initialized(false)
	, m_Valid(false)
	, m_OffsetShift(0)
{
	m_Valid = DetectFileSystem();
}

CFileSystemGCWii::~CFileSystemGCWii()
{
	m_FileInfoVector.clear();
}

u64 CFileSystemGCWii::GetFileSize(const char* _rFullPath)
{
	if (!m_Initialized)
		InitFileSystem();

	const SFileInfo* pFileInfo = FindFileInfo(_rFullPath);

	if (pFileInfo != NULL && !pFileInfo->IsDirectory())
		return pFileInfo->m_FileSize;

	return 0;
}

const char* CFileSystemGCWii::GetFileName(u64 _Address)
{
	if (!m_Initialized)
		InitFileSystem();

	for (size_t i = 0; i < m_FileInfoVector.size(); i++)
	{
		if ((m_FileInfoVector[i].m_Offset <= _Address) &&
		    ((m_FileInfoVector[i].m_Offset + m_FileInfoVector[i].m_FileSize) > _Address))
		{
			return m_FileInfoVector[i].m_FullPath;
		}
	}

	return 0;
}

u64 CFileSystemGCWii::ReadFile(const char* _rFullPath, u8* _pBuffer, size_t _MaxBufferSize)
{
	if (!m_Initialized)
		InitFileSystem();

	const SFileInfo* pFileInfo = FindFileInfo(_rFullPath);
	if (pFileInfo == NULL)
		return 0;

	if (pFileInfo->m_FileSize > _MaxBufferSize)
		return 0;

	DEBUG_LOG(DISCIO, "Filename: %s. Offset: %llx. Size: %llx",_rFullPath,
		pFileInfo->m_Offset, pFileInfo->m_FileSize);

	m_rVolume->Read(pFileInfo->m_Offset, pFileInfo->m_FileSize, _pBuffer);
	return pFileInfo->m_FileSize;
}

bool CFileSystemGCWii::ExportFile(const char* _rFullPath, const char* _rExportFilename)
{
	if (!m_Initialized)
		InitFileSystem();

	const SFileInfo* pFileInfo = FindFileInfo(_rFullPath);

	if (!pFileInfo)
		return false;

	u64 remainingSize = pFileInfo->m_FileSize;
	u64 fileOffset = pFileInfo->m_Offset;

	File::IOFile f(_rExportFilename, "wb");
	if (!f)
		return false;

	bool result = true;

	while (remainingSize)
	{
		// Limit read size to 128 MB
		size_t readSize = (size_t)min(remainingSize, (u64)0x08000000);

		std::vector<u8> buffer(readSize);

		result = m_rVolume->Read(fileOffset, readSize, &buffer[0]);

		if (!result)
			break;

		f.WriteBytes(&buffer[0], readSize);

		remainingSize -= readSize;
		fileOffset += readSize;
	}

	return result;
}

bool CFileSystemGCWii::ExportApploader(const char* _rExportFolder) const
{
	u32 AppSize = Read32(0x2440 + 0x14);// apploader size
	AppSize += Read32(0x2440 + 0x18);	// + trailer size
	AppSize += 0x20;					// + header size
	DEBUG_LOG(DISCIO,"AppSize -> %x", AppSize);

	std::vector<u8> buffer(AppSize);
	if (m_rVolume->Read(0x2440, AppSize, &buffer[0]))
	{
		std::string exportName(_rExportFolder);
		exportName += "/apploader.img";

		File::IOFile AppFile(exportName, "wb");
		if (AppFile)
		{
			AppFile.WriteBytes(&buffer[0], AppSize);
			return true;
		}
	}

	return false;
}

u32 CFileSystemGCWii::GetBootDOLSize() const
{
	u32 DolOffset = Read32(0x420) << m_OffsetShift;
	u32 DolSize = 0, offset = 0, size = 0;

	// Iterate through the 7 code segments
	for (u8 i = 0; i < 7; i++)
	{
		offset	= Read32(DolOffset + 0x00 + i * 4);
		size	= Read32(DolOffset + 0x90 + i * 4);
		if (offset + size > DolSize)
			DolSize = offset + size;
	}

	// Iterate through the 11 data segments
	for (u8 i = 0; i < 11; i++)
	{
		offset	= Read32(DolOffset + 0x1c + i * 4);
		size	= Read32(DolOffset + 0xac + i * 4);
		if (offset + size > DolSize)
			DolSize = offset + size;
	}
	return DolSize;
}

bool CFileSystemGCWii::GetBootDOL(u8* &buffer, u32 DolSize) const
{
	u32 DolOffset = Read32(0x420) << m_OffsetShift;
	return m_rVolume->Read(DolOffset, DolSize, buffer);
}

bool CFileSystemGCWii::ExportDOL(const char* _rExportFolder) const
{
	u32 DolOffset = Read32(0x420) << m_OffsetShift;
	u32 DolSize = GetBootDOLSize();

	std::vector<u8> buffer(DolSize);
	if (m_rVolume->Read(DolOffset, DolSize, &buffer[0]))
	{
		std::string exportName(_rExportFolder);
		exportName += "/boot.dol";

		File::IOFile DolFile(exportName, "wb");
		if (DolFile)
		{
			DolFile.WriteBytes(&buffer[0], DolSize);
			return true;
		}
	}
	
	return false;
}

u32 CFileSystemGCWii::Read32(u64 _Offset) const
{
	u32 Temp = 0;
	m_rVolume->Read(_Offset, 4, (u8*)&Temp);
	return Common::swap32(Temp);
}

std::string CFileSystemGCWii::GetStringFromOffset(u64 _Offset) const
{
	std::string data;
	data.resize(255);
	m_rVolume->Read(_Offset, data.size(), (u8*)&data[0]);
	data.erase(std::find(data.begin(), data.end(), 0x00), data.end());
	
	// TODO: Should we really always use SHIFT-JIS?
	// It makes some filenames in Pikmin (NTSC-U) sane, but is it correct?
	return SHIFTJISToUTF8(data);
}

size_t CFileSystemGCWii::GetFileList(std::vector<const SFileInfo *> &_rFilenames)
{	
	if (!m_Initialized)
		InitFileSystem();

	if (_rFilenames.size())
		PanicAlert("GetFileList : input list has contents?");
	_rFilenames.clear();
	_rFilenames.reserve(m_FileInfoVector.size());
	for (size_t i = 0; i < m_FileInfoVector.size(); i++)
		_rFilenames.push_back(&m_FileInfoVector[i]);
	return m_FileInfoVector.size();
}

const SFileInfo* CFileSystemGCWii::FindFileInfo(const char* _rFullPath)
{
	if (!m_Initialized)
		InitFileSystem();

	for (size_t i = 0; i < m_FileInfoVector.size(); i++)
	{
		if (!strcasecmp(m_FileInfoVector[i].m_FullPath, _rFullPath))
			return &m_FileInfoVector[i];
	}

	return NULL;
}

bool CFileSystemGCWii::DetectFileSystem()
{
	if (Read32(0x18) == 0x5D1C9EA3)
	{
		m_OffsetShift = 2; // Wii file system
		return true;
	}
	else if (Read32(0x1c) == 0xC2339F3D)
	{
		m_OffsetShift = 0; // GC file system
		return true;
	}

	return false;
}

void CFileSystemGCWii::InitFileSystem()
{
	m_Initialized = true;

	// read the whole FST
	u64 FSTOffset = (u64)Read32(0x424) << m_OffsetShift;
	// u32 FSTSize     = Read32(0x428);
	// u32 FSTMaxSize  = Read32(0x42C);


	// read all fileinfos
	SFileInfo Root;
	Root.m_NameOffset = Read32(FSTOffset + 0x0);
	Root.m_Offset     = (u64)Read32(FSTOffset + 0x4) << m_OffsetShift;
	Root.m_FileSize   = Read32(FSTOffset + 0x8);

	if (Root.IsDirectory())
	{
		if (m_FileInfoVector.size())
			PanicAlert("Wtf?");
		u64 NameTableOffset = FSTOffset;

		m_FileInfoVector.reserve((unsigned int)Root.m_FileSize);
		for (u32 i = 0; i < Root.m_FileSize; i++)
		{
			SFileInfo sfi;
			u64 Offset = FSTOffset + (i * 0xC);
			sfi.m_NameOffset = Read32(Offset + 0x0);
			sfi.m_Offset     = (u64)Read32(Offset + 0x4) << m_OffsetShift;
			sfi.m_FileSize   = Read32(Offset + 0x8);

			m_FileInfoVector.push_back(sfi);
			NameTableOffset += 0xC;
		}

		BuildFilenames(1, m_FileInfoVector.size(), NULL, NameTableOffset);
	}
}

// Changed this stuff from C++ string to C strings for speed in debug mode. Doesn't matter in release, but
// std::string is SLOW in debug mode.
size_t CFileSystemGCWii::BuildFilenames(const size_t _FirstIndex, const size_t _LastIndex, const char* _szDirectory, u64 _NameTableOffset)
{
	size_t CurrentIndex = _FirstIndex;

	while (CurrentIndex < _LastIndex)
	{
		SFileInfo *rFileInfo = &m_FileInfoVector[CurrentIndex];
		u64 uOffset = _NameTableOffset + (rFileInfo->m_NameOffset & 0xFFFFFF);
		std::string filename = GetStringFromOffset(uOffset);

		// check next index
		if (rFileInfo->IsDirectory())
		{
			// this is a directory, build up the new szDirectory
			if (_szDirectory != NULL)
				CharArrayFromFormat(rFileInfo->m_FullPath, "%s%s/", _szDirectory, filename.c_str());
			else
				CharArrayFromFormat(rFileInfo->m_FullPath, "%s/", filename.c_str());

			CurrentIndex = BuildFilenames(CurrentIndex + 1, (size_t) rFileInfo->m_FileSize, rFileInfo->m_FullPath, _NameTableOffset);
		}
		else
		{
			// this is a filename
			if (_szDirectory != NULL)
				CharArrayFromFormat(rFileInfo->m_FullPath, "%s%s", _szDirectory, filename.c_str());
			else
				CharArrayFromFormat(rFileInfo->m_FullPath, "%s", filename.c_str());

			CurrentIndex++;
		}
	}

	return CurrentIndex;
}

}  // namespace
