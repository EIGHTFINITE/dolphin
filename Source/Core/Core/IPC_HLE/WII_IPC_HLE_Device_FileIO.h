// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "Core/IPC_HLE/WII_IPC_HLE_Device.h"

class PointerWrap;
namespace File { class IOFile; }

std::string HLE_IPC_BuildFilename(std::string _pFilename);
void HLE_IPC_CreateVirtualFATFilesystem();

class CWII_IPC_HLE_Device_FileIO : public IWII_IPC_HLE_Device
{
public:
	CWII_IPC_HLE_Device_FileIO(u32 _DeviceID, const std::string& _rDeviceName);

	virtual ~CWII_IPC_HLE_Device_FileIO();

	IPCCommandResult Close(u32 _CommandAddress, bool _bForce) override;
	IPCCommandResult Open(u32 _CommandAddress, u32 _Mode) override;
	IPCCommandResult Seek(u32 _CommandAddress) override;
	IPCCommandResult Read(u32 _CommandAddress) override;
	IPCCommandResult Write(u32 _CommandAddress) override;
	IPCCommandResult IOCtl(u32 _CommandAddress) override;
	void PrepareForState(PointerWrap::Mode mode) override;
	void DoState(PointerWrap &p) override;

	void OpenFile();

private:
	enum
	{
		ISFS_OPEN_READ  = 1,
		ISFS_OPEN_WRITE = 2,
		ISFS_OPEN_RW    = (ISFS_OPEN_READ | ISFS_OPEN_WRITE)
	};

	enum
	{
		WII_SEEK_SET = 0,
		WII_SEEK_CUR = 1,
		WII_SEEK_END = 2,
	};

	enum
	{
		ISFS_FUNCNULL     = 0,
		ISFS_FUNCGETSTAT  = 1,
		ISFS_FUNCREADDIR  = 2,
		ISFS_FUNCGETATTR  = 3,
		ISFS_FUNCGETUSAGE = 4
	};

	enum
	{
		ISFS_IOCTL_FORMAT         = 1,
		ISFS_IOCTL_GETSTATS       = 2,
		ISFS_IOCTL_CREATEDIR      = 3,
		ISFS_IOCTL_READDIR        = 4,
		ISFS_IOCTL_SETATTR        = 5,
		ISFS_IOCTL_GETATTR        = 6,
		ISFS_IOCTL_DELETE         = 7,
		ISFS_IOCTL_RENAME         = 8,
		ISFS_IOCTL_CREATEFILE     = 9,
		ISFS_IOCTL_SETFILEVERCTRL = 10,
		ISFS_IOCTL_GETFILESTATS   = 11,
		ISFS_IOCTL_GETUSAGE       = 12,
		ISFS_IOCTL_SHUTDOWN       = 13
	};

	u32 m_Mode;
	u32 m_SeekPos;

	std::string m_filepath;
	std::shared_ptr<File::IOFile> m_file;
};
