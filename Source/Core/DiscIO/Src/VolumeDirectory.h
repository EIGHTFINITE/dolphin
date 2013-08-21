// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _VOLUME_DIRECTORY
#define _VOLUME_DIRECTORY

#include "Volume.h"
#include "Common.h"
#include "FileUtil.h"
#include <string>
#include <map>

//
// --- this volume type is used for reading files directly from the hard drive ---
//

namespace DiscIO
{

class CVolumeDirectory : public IVolume
{
public:

	CVolumeDirectory(const std::string& _rDirectory, bool _bIsWii,
		const std::string& _rApploader = "", const std::string& _rDOL = "");

	~CVolumeDirectory();

	static bool IsValidDirectory(const std::string& _rDirectory);

	bool Read(u64 _Offset, u64 _Length, u8* _pBuffer) const;
	bool RAWRead(u64 _Offset, u64 _Length, u8* _pBuffer) const;

	std::string GetUniqueID() const;
	void SetUniqueID(std::string _ID);

	std::string GetMakerID() const;

	std::vector<std::string> GetNames() const;
	void SetName(std::string);

	u32 GetFSTSize() const;

	std::string GetApploaderDate() const;

	ECountry GetCountry() const;

	u64 GetSize() const;
	u64 GetRawSize() const;

	void BuildFST();

private:
	static std::string ExtractDirectoryName(const std::string& _rDirectory);

	void SetDiskTypeWii();
	void SetDiskTypeGC();

	bool SetApploader(const std::string& _rApploader);

	void SetDOL(const std::string& _rDOL);

	// writing to read buffer
	void WriteToBuffer(u64 _SrcStartAddress, u64 _SrcLength, u8* _Src,
					   u64& _Address, u64& _Length, u8*& _pBuffer) const;

	void PadToAddress(u64 _StartAddress, u64& _Address, u64& _Length, u8*& _pBuffer) const;

	void Write32(u32 data, u32 offset, u8* buffer);

	// FST creation
	void WriteEntryData(u32& entryOffset, u8 type, u32 nameOffset, u64 dataOffset, u32 length);
	void WriteEntryName(u32& nameOffset, const std::string& name);
	void WriteEntry(const File::FSTEntry& entry, u32& fstOffset, u32& nameOffset, u64& dataOffset, u32 parentEntryNum);

	// returns number of entries found in _Directory
	u32 AddDirectoryEntries(const std::string& _Directory, File::FSTEntry& parentEntry);

	std::string m_rootDirectory;

	std::map<u64, std::string> m_virtualDisk;

	u32 m_totalNameSize;

	// gc has no shift, wii has 2 bit shift
	u32 m_addressShift;

	// first address on disk containing file data
	u64 m_dataStartAddress;

	u64 m_fstNameOffset;
	u64 m_fstSize;
	u8* m_FSTData;

	u8* m_diskHeader;

	#pragma pack(push, 1)
	struct SDiskHeaderInfo 
	{
		u32 debug_mntr_size;
		u32 simulated_mem_size;
		u32 arg_offset;
		u32 debug_flag;
		u32 track_location;
		u32 track_size;
		u32 countrycode;
		u32 unknown;
		u32 unknown2;

		// All the data is byteswapped
		SDiskHeaderInfo() {
			debug_mntr_size = 0;
			simulated_mem_size = 0;
			arg_offset = 0;
			debug_flag = 0;
			track_location = 0;
			track_size = 0;
			countrycode = 0;
			unknown = 0;
			unknown2 = 0;
		}
	};
	#pragma pack(pop)
	SDiskHeaderInfo* m_diskHeaderInfo;

	u64 m_apploaderSize;
	u8* m_apploader;

	u64 m_DOLSize;
	u8* m_DOL;

	static const u8 ENTRY_SIZE = 0x0c;
	static const u8 FILE_ENTRY = 0;
	static const u8 DIRECTORY_ENTRY = 1;
	static const u64 DISKHEADER_ADDRESS = 0;
	static const u64 DISKHEADERINFO_ADDRESS = 0x440;
	static const u64 APPLOADER_ADDRESS = 0x2440;
	static const u32 MAX_NAME_LENGTH = 0x3df;
	u64 FST_ADDRESS;
	u64 DOL_ADDRESS;
};

} // namespace

#endif
