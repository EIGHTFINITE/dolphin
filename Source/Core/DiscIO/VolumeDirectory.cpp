// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Common/Assert.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/MathUtil.h"
#include "Common/Logging/Log.h"
#include "DiscIO/Blob.h"
#include "DiscIO/FileMonitor.h"
#include "DiscIO/Volume.h"
#include "DiscIO/VolumeDirectory.h"

namespace DiscIO
{

const size_t CVolumeDirectory::MAX_NAME_LENGTH;
const size_t CVolumeDirectory::MAX_ID_LENGTH;

CVolumeDirectory::CVolumeDirectory(const std::string& _rDirectory, bool _bIsWii,
								   const std::string& _rApploader, const std::string& _rDOL)
	: m_totalNameSize(0)
	, m_dataStartAddress(-1)
	, m_diskHeader(DISKHEADERINFO_ADDRESS)
	, m_diskHeaderInfo(std::make_unique<SDiskHeaderInfo>())
	, m_fst_address(0)
	, m_dol_address(0)
{
	m_rootDirectory = ExtractDirectoryName(_rDirectory);

	// create the default disk header
	SetUniqueID("AGBJ01");
	SetName("Default name");

	if (_bIsWii)
		SetDiskTypeWii();
	else
		SetDiskTypeGC();

	// Don't load the DOL if we don't have an apploader
	if (SetApploader(_rApploader))
		SetDOL(_rDOL);

	BuildFST();
}

CVolumeDirectory::~CVolumeDirectory()
{
}

bool CVolumeDirectory::IsValidDirectory(const std::string& _rDirectory)
{
	return File::IsDirectory(ExtractDirectoryName(_rDirectory));
}

bool CVolumeDirectory::Read(u64 _Offset, u64 _Length, u8* _pBuffer, bool decrypt) const
{
	if (!decrypt && (_Offset + _Length >= 0x400) && m_is_wii)
	{
		// Fully supporting this would require re-encrypting every file that's read.
		// Only supporting the areas that IOS allows software to read could be more feasible.
		// Currently, only the header (up to 0x400) is supported, though we're cheating a bit
		// with it by reading the header inside the current partition instead. Supporting the
		// header is enough for booting games, but not for running things like the Disc Channel.
		return false;
	}

	if (decrypt && !m_is_wii)
		PanicAlertT("Tried to decrypt data from a non-Wii volume");

	// header
	if (_Offset < DISKHEADERINFO_ADDRESS)
	{
		WriteToBuffer(DISKHEADER_ADDRESS, DISKHEADERINFO_ADDRESS, m_diskHeader.data(), _Offset, _Length, _pBuffer);
	}
	// header info
	if (_Offset >= DISKHEADERINFO_ADDRESS && _Offset < APPLOADER_ADDRESS)
	{
		WriteToBuffer(DISKHEADERINFO_ADDRESS, sizeof(m_diskHeaderInfo), (u8*)m_diskHeaderInfo.get(), _Offset, _Length, _pBuffer);
	}
	// apploader
	if (_Offset >= APPLOADER_ADDRESS && _Offset < APPLOADER_ADDRESS + m_apploader.size())
	{
		WriteToBuffer(APPLOADER_ADDRESS, m_apploader.size(), m_apploader.data(), _Offset, _Length, _pBuffer);
	}
	// dol
	if (_Offset >= m_dol_address && _Offset < m_dol_address + m_DOL.size())
	{
		WriteToBuffer(m_dol_address, m_DOL.size(), m_DOL.data(), _Offset, _Length, _pBuffer);
	}
	// fst
	if (_Offset >= m_fst_address && _Offset < m_dataStartAddress)
	{
		WriteToBuffer(m_fst_address, m_FSTData.size(), m_FSTData.data(), _Offset, _Length, _pBuffer);
	}

	if (m_virtualDisk.empty())
		return true;

	// Determine which file the offset refers to
	std::map<u64, std::string>::const_iterator fileIter = m_virtualDisk.lower_bound(_Offset);
	if (fileIter->first > _Offset && fileIter != m_virtualDisk.begin())
		--fileIter;

	// zero fill to start of file data
	PadToAddress(fileIter->first, _Offset, _Length, _pBuffer);

	while (fileIter != m_virtualDisk.end() && _Length > 0)
	{
		_dbg_assert_(DVDINTERFACE, fileIter->first <= _Offset);
		u64 fileOffset = _Offset - fileIter->first;
		const std::string fileName = fileIter->second;

		File::IOFile file(fileName, "rb");
		if (!file)
			return false;

		u64 fileSize = file.GetSize();

		FileMon::CheckFile(fileName, fileSize);

		if (fileOffset < fileSize)
		{
			u64 fileBytes = std::min(fileSize - fileOffset, _Length);

			if (!file.Seek(fileOffset, SEEK_SET))
				return false;
			if (!file.ReadBytes(_pBuffer, fileBytes))
				return false;

			_Length  -= fileBytes;
			_pBuffer += fileBytes;
			_Offset  += fileBytes;
		}

		++fileIter;

		if (fileIter != m_virtualDisk.end())
		{
			_dbg_assert_(DVDINTERFACE, fileIter->first >= _Offset);
			PadToAddress(fileIter->first, _Offset, _Length, _pBuffer);
		}
	}

	return true;
}

std::string CVolumeDirectory::GetUniqueID() const
{
	return std::string(m_diskHeader.begin(), m_diskHeader.begin() + MAX_ID_LENGTH);
}

void CVolumeDirectory::SetUniqueID(const std::string& id)
{
	memcpy(m_diskHeader.data(), id.c_str(), std::min(id.length(), MAX_ID_LENGTH));
}

IVolume::ECountry CVolumeDirectory::GetCountry() const
{
	return CountrySwitch(m_diskHeader[3]);
}

std::string CVolumeDirectory::GetMakerID() const
{
	// Not implemented
	return "00";
}

std::string CVolumeDirectory::GetInternalName() const
{
	char name[0x60];
	if (Read(0x20, 0x60, (u8*)name, false))
		return DecodeString(name);
	else
		return "";
}

std::map<IVolume::ELanguage, std::string> CVolumeDirectory::GetNames(bool prefer_long) const
{
	std::string name = GetInternalName();
	if (name.empty())
		return { { } };
	return { { IVolume::LANGUAGE_UNKNOWN, name } };
}

std::vector<u32> CVolumeDirectory::GetBanner(int* width, int* height) const
{
	// Not implemented
	*width = 0;
	*height = 0;
	return std::vector<u32>();
}

void CVolumeDirectory::SetName(const std::string& name)
{
	size_t length = std::min(name.length(), MAX_NAME_LENGTH);
	memcpy(&m_diskHeader[0x20], name.c_str(), length);
	m_diskHeader[length + 0x20] = 0;
}

u64 CVolumeDirectory::GetFSTSize() const
{
	// Not implemented
	return 0;
}

std::string CVolumeDirectory::GetApploaderDate() const
{
	// Not implemented
	return "VOID";
}

IVolume::EPlatform CVolumeDirectory::GetVolumeType() const
{
	return m_is_wii ? WII_DISC : GAMECUBE_DISC;
}

BlobType CVolumeDirectory::GetBlobType() const
{
	// VolumeDirectory isn't actually a blob, but it sort of acts
	// like one, so it makes sense that it has its own blob type.
	// It should be made into a proper blob in the future.
	return BlobType::DIRECTORY;
}

u64 CVolumeDirectory::GetSize() const
{
	// Not implemented
	return 0;
}

u64 CVolumeDirectory::GetRawSize() const
{
	// Not implemented
	return 0;
}

std::string CVolumeDirectory::ExtractDirectoryName(const std::string& _rDirectory)
{
	std::string directoryName = _rDirectory;

	size_t lastSep = directoryName.find_last_of(DIR_SEP_CHR);

	if (lastSep != directoryName.size() - 1)
	{
		// TODO: This assumes that file names will always have a dot in them
		//       and directory names never will; both assumptions are often
		//       right but in general wrong.
		size_t extensionStart = directoryName.find_last_of('.');
		if (extensionStart != std::string::npos && extensionStart > lastSep)
		{
			directoryName.resize(lastSep);
		}
	}
	else
	{
		directoryName.resize(lastSep);
	}

	return directoryName;
}

void CVolumeDirectory::SetDiskTypeWii()
{
	Write32(0x5d1c9ea3, 0x18, &m_diskHeader);
	memset(&m_diskHeader[0x1c], 0, 4);

	m_is_wii = true;
	m_addressShift = 2;
}

void CVolumeDirectory::SetDiskTypeGC()
{
	memset(&m_diskHeader[0x18], 0, 4);
	Write32(0xc2339f3d, 0x1c, &m_diskHeader);

	m_is_wii = false;
	m_addressShift = 0;
}

bool CVolumeDirectory::SetApploader(const std::string& _rApploader)
{
	if (!_rApploader.empty())
	{
		std::string data;
		if (!File::ReadFileToString(_rApploader, data))
		{
			PanicAlertT("Apploader unable to load from file");
			return false;
		}
		size_t apploaderSize = 0x20 + Common::swap32(*(u32*)&data.data()[0x14]) + Common::swap32(*(u32*)&data.data()[0x18]);
		if (apploaderSize != data.size())
		{
			PanicAlertT("Apploader is the wrong size...is it really an apploader?");
			return false;
		}
		m_apploader.resize(apploaderSize);
		std::copy(data.begin(), data.end(), m_apploader.begin());

		// 32byte aligned (plus 0x20 padding)
		m_dol_address = ROUND_UP(APPLOADER_ADDRESS + m_apploader.size() + 0x20, 0x20ull);
		return true;
	}
	else
	{
		m_apploader.resize(0x20);
		// Make sure BS2 HLE doesn't try to run the apploader
		*(u32*)&m_apploader[0x10] = (u32)-1;
		return false;
	}
}

void CVolumeDirectory::SetDOL(const std::string& rDOL)
{
	if (!rDOL.empty())
	{
		std::string data;
		File::ReadFileToString(rDOL, data);
		m_DOL.resize(data.size());
		std::copy(data.begin(), data.end(), m_DOL.begin());

		Write32((u32)(m_dol_address >> m_addressShift), 0x0420, &m_diskHeader);

		// 32byte aligned (plus 0x20 padding)
		m_fst_address = ROUND_UP(m_dol_address + m_DOL.size() + 0x20, 0x20ull);
	}
}

void CVolumeDirectory::BuildFST()
{
	m_FSTData.clear();

	File::FSTEntry rootEntry;

	// read data from physical disk to rootEntry
	u64 totalEntries = AddDirectoryEntries(m_rootDirectory, rootEntry) + 1;

	m_fstNameOffset = totalEntries * ENTRY_SIZE; // offset in FST nameTable
	m_FSTData.resize(m_fstNameOffset + m_totalNameSize);

	// if FST hasn't been assigned (ie no apploader/dol setup), set to default
	if (m_fst_address == 0)
		m_fst_address = APPLOADER_ADDRESS + 0x2000;

	// 4 byte aligned start of data on disk
	m_dataStartAddress = ROUND_UP(m_fst_address + m_FSTData.size(), 0x8000ull);
	u64 curDataAddress = m_dataStartAddress;

	u32 fstOffset = 0;  // Offset within FST data
	u32 nameOffset = 0; // Offset within name table
	u32 rootOffset = 0; // Offset of root of FST

	// write root entry
	WriteEntryData(fstOffset, DIRECTORY_ENTRY, 0, 0, totalEntries);

	for (auto& entry : rootEntry.children)
	{
		WriteEntry(entry, fstOffset, nameOffset, curDataAddress, rootOffset);
	}

	// overflow check
	_dbg_assert_(DVDINTERFACE, nameOffset == m_totalNameSize);

	// write FST size and location
	Write32((u32)(m_fst_address >> m_addressShift), 0x0424, &m_diskHeader);
	Write32((u32)(m_FSTData.size() >> m_addressShift), 0x0428, &m_diskHeader);
	Write32((u32)(m_FSTData.size() >> m_addressShift), 0x042c, &m_diskHeader);
}

void CVolumeDirectory::WriteToBuffer(u64 _SrcStartAddress, u64 _SrcLength, const u8* _Src,
									 u64& _Address, u64& _Length, u8*& _pBuffer) const
{
	if (_Length == 0)
		return;

	_dbg_assert_(DVDINTERFACE, _Address >= _SrcStartAddress);

	u64 srcOffset = _Address - _SrcStartAddress;

	if (srcOffset < _SrcLength)
	{
		u64 srcBytes = std::min(_SrcLength - srcOffset, _Length);

		memcpy(_pBuffer, _Src + srcOffset, (size_t)srcBytes);

		_Length -= srcBytes;
		_pBuffer += srcBytes;
		_Address += srcBytes;
	}
}

void CVolumeDirectory::PadToAddress(u64 _StartAddress, u64& _Address, u64& _Length, u8*& _pBuffer) const
{
	if (_StartAddress > _Address && _Length > 0)
	{
		u64 padBytes = std::min(_StartAddress - _Address, _Length);
		memset(_pBuffer, 0, (size_t)padBytes);
		_Length -= padBytes;
		_pBuffer += padBytes;
		_Address += padBytes;
	}
}

void CVolumeDirectory::Write32(u32 data, u32 offset, std::vector<u8>* const buffer)
{
	(*buffer)[offset++] = (data >> 24);
	(*buffer)[offset++] = (data >> 16) & 0xff;
	(*buffer)[offset++] = (data >> 8) & 0xff;
	(*buffer)[offset] = (data) & 0xff;
}

void CVolumeDirectory::WriteEntryData(u32& entryOffset, u8 type, u32 nameOffset, u64 dataOffset, u64 length)
{
	m_FSTData[entryOffset++] = type;

	m_FSTData[entryOffset++] = (nameOffset >> 16) & 0xff;
	m_FSTData[entryOffset++] = (nameOffset >> 8) & 0xff;
	m_FSTData[entryOffset++] = (nameOffset) & 0xff;

	Write32((u32)(dataOffset >> m_addressShift), entryOffset, &m_FSTData);
	entryOffset += 4;

	Write32((u32)length, entryOffset, &m_FSTData);
	entryOffset += 4;
}

void CVolumeDirectory::WriteEntryName(u32& nameOffset, const std::string& name)
{
	strncpy((char*)&m_FSTData[nameOffset + m_fstNameOffset], name.c_str(), name.length() + 1);

	nameOffset += (u32)(name.length() + 1);
}

void CVolumeDirectory::WriteEntry(const File::FSTEntry& entry, u32& fstOffset, u32& nameOffset, u64& dataOffset, u32 parentEntryNum)
{
	if (entry.isDirectory)
	{
		u32 myOffset = fstOffset;
		u32 myEntryNum = myOffset / ENTRY_SIZE;
		WriteEntryData(fstOffset, DIRECTORY_ENTRY, nameOffset, parentEntryNum, myEntryNum + entry.size + 1);
		WriteEntryName(nameOffset, entry.virtualName);

		for (const auto& child : entry.children)
		{
			WriteEntry(child, fstOffset, nameOffset, dataOffset, myEntryNum);
		}
	}
	else
	{
		// put entry in FST
		WriteEntryData(fstOffset, FILE_ENTRY, nameOffset, dataOffset, entry.size);
		WriteEntryName(nameOffset, entry.virtualName);

		// write entry to virtual disk
		_dbg_assert_(DVDINTERFACE, m_virtualDisk.find(dataOffset) == m_virtualDisk.end());
		m_virtualDisk.emplace(dataOffset, entry.physicalName);

		// 4 byte aligned
		dataOffset = ROUND_UP(dataOffset + std::max<u64>(entry.size, 1ull), 0x8000ull);
	}
}

static u32 ComputeNameSize(const File::FSTEntry& parentEntry)
{
	u32 nameSize = 0;
	const std::vector<File::FSTEntry>& children = parentEntry.children;
	for (auto it = children.cbegin(); it != children.cend(); ++it)
	{
		const File::FSTEntry& entry = *it;
		if (entry.isDirectory)
		{
			nameSize += ComputeNameSize(entry);
		}
		nameSize += (u32)entry.virtualName.length() + 1;
	}
	return nameSize;
}

u64 CVolumeDirectory::AddDirectoryEntries(const std::string& _Directory, File::FSTEntry& parentEntry)
{
	parentEntry = File::ScanDirectoryTree(_Directory, true);
	m_totalNameSize += ComputeNameSize(parentEntry);
	return parentEntry.size;
}

} // namespace
