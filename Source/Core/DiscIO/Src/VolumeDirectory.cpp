// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "MathUtil.h"
#include "CommonPaths.h"
#include "VolumeDirectory.h"
#include "FileBlob.h"

namespace DiscIO
{

CVolumeDirectory::CVolumeDirectory(const std::string& _rDirectory, bool _bIsWii,
								   const std::string& _rApploader, const std::string& _rDOL)
	: m_totalNameSize(0)
	, m_dataStartAddress(-1)
	, m_fstSize(0)
	, m_FSTData(NULL)
	, m_apploaderSize(0)
	, m_apploader(NULL)
	, m_DOLSize(0)
	, m_DOL(NULL)
	, FST_ADDRESS(0)
	, DOL_ADDRESS(0)
{
	m_rootDirectory = ExtractDirectoryName(_rDirectory);	

	// create the default disk header
	m_diskHeader = new u8[DISKHEADERINFO_ADDRESS];
	memset(m_diskHeader, 0, (size_t)DISKHEADERINFO_ADDRESS);
	SetUniqueID("AGBJ01");	
	SetName("Default name");

	if(_bIsWii)
	{
		SetDiskTypeWii();
	}
	else
	{
		SetDiskTypeGC();
	}

	m_diskHeaderInfo = new SDiskHeaderInfo();

	// Don't load the dol if we've no apploader...
	if (SetApploader(_rApploader))
		SetDOL(_rDOL);

	BuildFST();
}

CVolumeDirectory::~CVolumeDirectory()
{
	delete[] m_FSTData;
	m_FSTData = NULL;

	delete[] m_diskHeader;
	m_diskHeader = NULL;

	delete m_diskHeaderInfo;
	m_diskHeaderInfo = NULL;

	delete[] m_DOL;
	m_DOL = NULL;

	delete[] m_apploader;
	m_apploader = NULL;
}

bool CVolumeDirectory::IsValidDirectory(const std::string& _rDirectory)
{
	std::string directoryName = ExtractDirectoryName(_rDirectory);
	return File::IsDirectory(directoryName);
}

bool CVolumeDirectory::RAWRead( u64 _Offset, u64 _Length, u8* _pBuffer ) const
{
	return false;
}

bool CVolumeDirectory::Read(u64 _Offset, u64 _Length, u8* _pBuffer) const
{
	// header
	if(_Offset < DISKHEADERINFO_ADDRESS)
	{
		WriteToBuffer(DISKHEADER_ADDRESS, DISKHEADERINFO_ADDRESS, m_diskHeader, _Offset, _Length, _pBuffer);
	}
	// header info
	if(_Offset >= DISKHEADERINFO_ADDRESS && _Offset < APPLOADER_ADDRESS)
	{
		WriteToBuffer(DISKHEADERINFO_ADDRESS, sizeof(m_diskHeaderInfo), (u8*)m_diskHeaderInfo, _Offset, _Length, _pBuffer);
	}
	// apploader
	if(_Offset >= APPLOADER_ADDRESS && _Offset < APPLOADER_ADDRESS + m_apploaderSize)
	{
		WriteToBuffer(APPLOADER_ADDRESS, m_apploaderSize, m_apploader, _Offset, _Length, _pBuffer);
	}
	// dol
	if(_Offset >= DOL_ADDRESS && _Offset < DOL_ADDRESS + m_DOLSize)
	{
		WriteToBuffer(DOL_ADDRESS, m_DOLSize, m_DOL, _Offset, _Length, _pBuffer);
	}
	// fst
	if(_Offset >= FST_ADDRESS && _Offset < m_dataStartAddress)
	{
		WriteToBuffer(FST_ADDRESS, m_fstSize, m_FSTData, _Offset, _Length, _pBuffer);
	}
	
	if(m_virtualDisk.size() == 0)
		return true;

	// Determine which file the offset refers to
	std::map<u64, std::string>::const_iterator fileIter = m_virtualDisk.lower_bound(_Offset);
	if(fileIter->first > _Offset && fileIter != m_virtualDisk.begin())
		--fileIter;

	// zero fill to start of file data
	PadToAddress(fileIter->first, _Offset, _Length, _pBuffer);

	while(fileIter != m_virtualDisk.end() && _Length > 0)
	{	
		_dbg_assert_(DVDINTERFACE, fileIter->first <= _Offset);
		u64 fileOffset = _Offset - fileIter->first;

		PlainFileReader* reader = PlainFileReader::Create(fileIter->second.c_str());		
		if(reader == NULL)
			return false;

		u64 fileSize = reader->GetDataSize();
		
		if(fileOffset < fileSize)
		{
			u64 fileBytes = fileSize - fileOffset;
			if(_Length < fileBytes)
				fileBytes = _Length;

			if(!reader->Read(fileOffset, fileBytes, _pBuffer))
				return false;

			_Length -= fileBytes;
			_pBuffer += fileBytes;
			_Offset += fileBytes;
		}

		++fileIter;

		if(fileIter != m_virtualDisk.end())
		{
			_dbg_assert_(DVDINTERFACE, fileIter->first >= _Offset);
			PadToAddress(fileIter->first, _Offset, _Length, _pBuffer);
		}

		delete reader;
	}
	
	return true;
}

std::string CVolumeDirectory::GetUniqueID() const
{
	_dbg_assert_(DVDINTERFACE, m_diskHeader);
	
	char buffer[7];
	memcpy(buffer, m_diskHeader, 6);
	buffer[6] = 0;

	std::string id = buffer;
	return id;
}

void CVolumeDirectory::SetUniqueID(std::string _ID)
{
	_dbg_assert_(DVDINTERFACE, m_diskHeader);

	u32 length = (u32)_ID.length();
	if(length > 6)
		length = 6;

	memcpy(m_diskHeader, _ID.c_str(), length);
}

IVolume::ECountry CVolumeDirectory::GetCountry() const
{
	_dbg_assert_(DVDINTERFACE, m_diskHeader);
	
	u8 CountryCode = m_diskHeader[3];

	return CountrySwitch(CountryCode);
}

std::string CVolumeDirectory::GetMakerID() const
{
	return "VOID";
}

std::vector<std::string> CVolumeDirectory::GetNames() const
{
	_dbg_assert_(DVDINTERFACE, m_diskHeader);
	return std::vector<std::string>(1, (char*)(m_diskHeader + 0x20));
}

void CVolumeDirectory::SetName(std::string _Name)
{
	_dbg_assert_(DVDINTERFACE, m_diskHeader);

	u32 length = (u32)_Name.length();
	if(length > MAX_NAME_LENGTH)
		length = MAX_NAME_LENGTH;

	memcpy(m_diskHeader + 0x20, _Name.c_str(), length);
	m_diskHeader[length + 0x20] = 0;
}

u32 CVolumeDirectory::GetFSTSize() const
{
	return 0;
}

std::string CVolumeDirectory::GetApploaderDate() const
{
	return "VOID";
}

u64 CVolumeDirectory::GetSize() const
{
	return 0;
}

u64 CVolumeDirectory::GetRawSize() const
{
	return GetSize();
}

std::string CVolumeDirectory::ExtractDirectoryName(const std::string& _rDirectory)
{
	std::string directoryName = _rDirectory;

	size_t lastSep = directoryName.find_last_of(DIR_SEP_CHR);

	if(lastSep != directoryName.size() - 1)
	{
		// TODO: This assumes that file names will always have a dot in them
		//	   and directory names never will; both assumptions are often
		//	   right but in general wrong.
		size_t extensionStart = directoryName.find_last_of('.');
		if(extensionStart != std::string::npos && extensionStart > lastSep)
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
	_dbg_assert_(DVDINTERFACE, m_diskHeader);

	m_diskHeader[0x18] = 0x5d;
	m_diskHeader[0x19] = 0x1c;
	m_diskHeader[0x1a] = 0x9e;
	m_diskHeader[0x1b] = 0xa3;	
	memset(m_diskHeader + 0x1c, 0, 4);

	m_addressShift = 2;
}

void CVolumeDirectory::SetDiskTypeGC()
{
	_dbg_assert_(DVDINTERFACE, m_diskHeader);

	memset(m_diskHeader + 0x18, 0, 4);
	m_diskHeader[0x1c] = 0xc2;
	m_diskHeader[0x1d] = 0x33;
	m_diskHeader[0x1e] = 0x9f;
	m_diskHeader[0x1f] = 0x3d;

	m_addressShift = 0;
}

bool CVolumeDirectory::SetApploader(const std::string& _rApploader)
{
	if (!_rApploader.empty())
	{
		std::string data;
		if (!File::ReadFileToString(false, _rApploader.c_str(), data))
		{
			PanicAlertT("Apploader unable to load from file");
			return false;
		}
		m_apploaderSize = 0x20 + Common::swap32(*(u32*)&data.data()[0x14]) + Common::swap32(*(u32*)&data.data()[0x18]);
		if (m_apploaderSize != data.size())
		{
			PanicAlertT("Apploader is the wrong size...is it really an apploader?");
			return false;
		}
		m_apploader = new u8[m_apploaderSize];
		copy(data.begin(), data.end(), m_apploader);

		// 32byte aligned (plus 0x20 padding)
		DOL_ADDRESS = ROUND_UP(APPLOADER_ADDRESS + m_apploaderSize + 0x20, 0x20ull);
		return true;
	}
	else
	{
		m_apploaderSize = 0x20;
		m_apploader = new u8[m_apploaderSize];
		// Make sure BS2 HLE doesn't try to run the apploader
		*(u32*)&m_apploader[0x10] = (u32)-1;
		return false;
	}
}

void CVolumeDirectory::SetDOL(const std::string& _rDOL)
{
	if (!_rDOL.empty())
	{
		std::string data;
		File::ReadFileToString(false, _rDOL.c_str(), data);
		m_DOLSize = data.size();
		m_DOL = new u8[m_DOLSize];
		copy(data.begin(), data.end(), m_DOL);

		Write32((u32)(DOL_ADDRESS >> m_addressShift), 0x0420, m_diskHeader);

		// 32byte aligned (plus 0x20 padding)
		FST_ADDRESS = ROUND_UP(DOL_ADDRESS + m_DOLSize + 0x20, 0x20ull);
	}
}

void CVolumeDirectory::BuildFST()
{
	if(m_FSTData)
	{
		delete m_FSTData;
	}

	File::FSTEntry rootEntry;

	// read data from physical disk to rootEntry
	u32 totalEntries = AddDirectoryEntries(m_rootDirectory, rootEntry) + 1;

	m_fstNameOffset = totalEntries * ENTRY_SIZE;	// offset in FST nameTable
	m_fstSize = m_fstNameOffset + m_totalNameSize;
	m_FSTData = new u8[(u32)m_fstSize];

	// if FST hasn't been assigned (ie no apploader/dol setup), set to default
	if (FST_ADDRESS == 0)
		FST_ADDRESS = APPLOADER_ADDRESS + 0x2000;

	// 4 byte aligned start of data on disk
	m_dataStartAddress = ROUND_UP(FST_ADDRESS + m_fstSize, 0x8000ull);
	u64 curDataAddress = m_dataStartAddress;

	u32 fstOffset = 0;		// offset within FST data
	u32 nameOffset = 0;		// offset within name table
	u32 rootOffset = 0;		// offset of root of FST

	// write root entry
	WriteEntryData(fstOffset, DIRECTORY_ENTRY, 0, 0, totalEntries);

	for(std::vector<File::FSTEntry>::iterator iter = rootEntry.children.begin(); iter != rootEntry.children.end(); ++iter)
	{
		WriteEntry(*iter, fstOffset, nameOffset, curDataAddress, rootOffset);
	}

	// overflow check
	_dbg_assert_(DVDINTERFACE, nameOffset == m_totalNameSize);

	// write FST size and location
	_dbg_assert_(DVDINTERFACE, m_diskHeader);
	Write32((u32)(FST_ADDRESS >> m_addressShift), 0x0424, m_diskHeader);
	Write32((u32)(m_fstSize >> m_addressShift), 0x0428, m_diskHeader);
	Write32((u32)(m_fstSize >> m_addressShift), 0x042c, m_diskHeader);
}

void CVolumeDirectory::WriteToBuffer(u64 _SrcStartAddress, u64 _SrcLength, u8* _Src,
									 u64& _Address, u64& _Length, u8*& _pBuffer) const
{
	if(_Length == 0)
		return;

	_dbg_assert_(DVDINTERFACE, _Address >= _SrcStartAddress);

	u64 srcOffset = _Address - _SrcStartAddress;

	if(srcOffset < _SrcLength)
	{
		u64 srcBytes = _SrcLength - srcOffset;
		if(_Length < srcBytes)
			srcBytes = _Length;

		memcpy(_pBuffer, _Src + srcOffset, (size_t)srcBytes);

		_Length -= srcBytes;
		_pBuffer += srcBytes;
		_Address += srcBytes;
	}	
}

void CVolumeDirectory::PadToAddress(u64 _StartAddress, u64& _Address, u64& _Length, u8*& _pBuffer) const
{
	if(_StartAddress <= _Address)
		return;

	u64 padBytes = _StartAddress - _Address;
	if(padBytes > _Length)
		padBytes = _Length;

	if(_Length > 0)
	{
		memset(_pBuffer, 0, (size_t)padBytes);
		_Length -= padBytes;
		_pBuffer += padBytes;
		_Address += padBytes;
	}
}

void CVolumeDirectory::Write32(u32 data, u32 offset, u8* buffer)
{
	buffer[offset++] = (data >> 24);
	buffer[offset++] = (data >> 16) & 0xff;
	buffer[offset++] = (data >> 8) & 0xff;
	buffer[offset] = (data) & 0xff;
}

void CVolumeDirectory::WriteEntryData(u32& entryOffset, u8 type, u32 nameOffset, u64 dataOffset, u32 length)
{
	m_FSTData[entryOffset++] = type;

	m_FSTData[entryOffset++] = (nameOffset >> 16) & 0xff;
	m_FSTData[entryOffset++] = (nameOffset >> 8) & 0xff;
	m_FSTData[entryOffset++] = (nameOffset) & 0xff;

	Write32((u32)(dataOffset >> m_addressShift), entryOffset, m_FSTData);
	entryOffset += 4;

	Write32((u32)length, entryOffset, m_FSTData);
	entryOffset += 4;
}

void CVolumeDirectory::WriteEntryName(u32& nameOffset, const std::string& name)
{
	strncpy((char*)(m_FSTData + nameOffset + m_fstNameOffset), name.c_str(), name.length() + 1);
	
	nameOffset += (u32)(name.length() + 1);
}

void CVolumeDirectory::WriteEntry(const File::FSTEntry& entry, u32& fstOffset, u32& nameOffset, u64& dataOffset, u32 parentEntryNum)
{	
	if(entry.isDirectory)
	{
		u32 myOffset = fstOffset;
		u32 myEntryNum = myOffset / ENTRY_SIZE;
		WriteEntryData(fstOffset, DIRECTORY_ENTRY, nameOffset, parentEntryNum, (u32)(myEntryNum + entry.size + 1));
		WriteEntryName(nameOffset, entry.virtualName);

		for(std::vector<File::FSTEntry>::const_iterator iter = entry.children.begin(); iter != entry.children.end(); ++iter)
		{
			WriteEntry(*iter, fstOffset, nameOffset, dataOffset, myEntryNum);
		}
	}
	else
	{
		// put entry in FST
		WriteEntryData(fstOffset, FILE_ENTRY, nameOffset, dataOffset, (u32)entry.size);
		WriteEntryName(nameOffset, entry.virtualName);

		// write entry to virtual disk
		_dbg_assert_(DVDINTERFACE, m_virtualDisk.find(dataOffset) == m_virtualDisk.end());
		m_virtualDisk.insert(make_pair(dataOffset, entry.physicalName));

		// 4 byte aligned
		dataOffset = ROUND_UP(dataOffset + entry.size, 0x8000ull);
	}
}

static u32 ComputeNameSize(const File::FSTEntry& parentEntry)
{
	u32 nameSize = 0;
	const std::vector<File::FSTEntry>& children = parentEntry.children;
	for (std::vector<File::FSTEntry>::const_iterator it = children.begin();
		it != children.end(); ++it)
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

u32 CVolumeDirectory::AddDirectoryEntries(const std::string& _Directory, File::FSTEntry& parentEntry)
{
	u32 foundEntries = ScanDirectoryTree(_Directory, parentEntry);
	m_totalNameSize += ComputeNameSize(parentEntry);
	return foundEntries;
}

} // namespace
