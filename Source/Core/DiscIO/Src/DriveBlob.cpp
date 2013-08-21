// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "DriveBlob.h"
#include "StringUtil.h"

namespace DiscIO
{

DriveReader::DriveReader(const char *drive)
{
#ifdef _WIN32
	SectorReader::SetSectorSize(2048);
	auto const path = UTF8ToTStr(std::string("\\\\.\\") + drive);
	hDisc = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
	if (hDisc != INVALID_HANDLE_VALUE)
	{
		// Do a test read to make sure everything is OK, since it seems you can get
		// handles to empty drives.
		DWORD not_used;
		u8 *buffer = new u8[m_blocksize];
		if (!ReadFile(hDisc, buffer, m_blocksize, (LPDWORD)&not_used, NULL))
		{
			delete [] buffer;
			// OK, something is wrong.
			CloseHandle(hDisc);
			hDisc = INVALID_HANDLE_VALUE;
			return;
		}
		delete [] buffer;
		
	#ifdef _LOCKDRIVE // Do we want to lock the drive?
		// Lock the compact disc in the CD-ROM drive to prevent accidental
		// removal while reading from it.
		pmrLockCDROM.PreventMediaRemoval = TRUE;
		DeviceIoControl(hDisc, IOCTL_CDROM_MEDIA_REMOVAL,
                   &pmrLockCDROM, sizeof(pmrLockCDROM), NULL,
                   0, &dwNotUsed, NULL);
	#endif
#else
	SectorReader::SetSectorSize(2048);
	file_.Open(drive, "rb");
	if (file_)
	{
#endif
	}
	else
		NOTICE_LOG(DISCIO, "Load from DVD backup failed or no disc in drive %s", drive);
}  // DriveReader::DriveReader

DriveReader::~DriveReader()
{
#ifdef _WIN32
#ifdef _LOCKDRIVE // Do we want to lock the drive?
	// Unlock the disc in the CD-ROM drive.
	pmrLockCDROM.PreventMediaRemoval = FALSE;
	DeviceIoControl (hDisc, IOCTL_CDROM_MEDIA_REMOVAL,
		&pmrLockCDROM, sizeof(pmrLockCDROM), NULL,
		0, &dwNotUsed, NULL);
#endif
	if (hDisc != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hDisc);
		hDisc = INVALID_HANDLE_VALUE;
	}
#else
	file_.Close();
#endif	
}

DriveReader *DriveReader::Create(const char *drive)
{
	DriveReader *reader = new DriveReader(drive);
	if (!reader->IsOK())
	{
		delete reader;
		return 0;
	}
	return reader;
}

void DriveReader::GetBlock(u64 block_num, u8 *out_ptr)
{
	u8* const lpSector = new u8[m_blocksize];
#ifdef _WIN32
	u32 NotUsed;
	u64 offset = m_blocksize * block_num;
	LONG off_low = (LONG)offset & 0xFFFFFFFF;
	LONG off_high = (LONG)(offset >> 32);
	SetFilePointer(hDisc, off_low, &off_high, FILE_BEGIN);
	if (!ReadFile(hDisc, lpSector, m_blocksize, (LPDWORD)&NotUsed, NULL))
		PanicAlertT("Disc Read Error");
#else
	file_.Seek(m_blocksize * block_num, SEEK_SET);
	file_.ReadBytes(lpSector, m_blocksize);
#endif
	memcpy(out_ptr, lpSector, m_blocksize);
	delete[] lpSector;
}

bool DriveReader::ReadMultipleAlignedBlocks(u64 block_num, u64 num_blocks, u8 *out_ptr)
{
#ifdef _WIN32
	u32 NotUsed;
	u64 offset = m_blocksize * block_num;
	LONG off_low = (LONG)offset & 0xFFFFFFFF;
	LONG off_high = (LONG)(offset >> 32);
	SetFilePointer(hDisc, off_low, &off_high, FILE_BEGIN);
	if (!ReadFile(hDisc, out_ptr, (DWORD)(m_blocksize * num_blocks), (LPDWORD)&NotUsed, NULL))
	{
		PanicAlertT("Disc Read Error");
		return false;
	}
#else
	fseeko(file_.GetHandle(), m_blocksize*block_num, SEEK_SET);
	if(fread(out_ptr, 1, m_blocksize * num_blocks, file_.GetHandle()) != m_blocksize * num_blocks)
		return false;
#endif
	return true;
}

}  // namespace
