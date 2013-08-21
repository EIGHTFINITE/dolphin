// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _DVDINTERFACE_H
#define _DVDINTERFACE_H

#include "Common.h"
class PointerWrap;

namespace DVDInterface
{

void Init();
void Shutdown();
void DoState(PointerWrap &p);

// Disc detection and swapping
void SetDiscInside(bool _DiscInside);
bool IsDiscInside();
void ChangeDisc(const char* _FileName);

// Lid Functions
void SetLidOpen(bool _bOpen = true);
bool IsLidOpen();

// Used as low level control by WII_IPC_HLE_Device_DI
void ClearCoverInterrupt();

// DVD Access Functions
bool DVDRead(u32 _iDVDOffset, u32 _iRamAddress, u32 _iLength);
// For AudioInterface
bool DVDReadADPCM(u8* _pDestBuffer, u32 _iNumSamples);
extern bool g_bStream;

// Read32
void Read32(u32& _uReturnValue, const u32 _iAddress);

// Write32
void Write32(const u32 _iValue, const u32 _iAddress);


// Not sure about endianness here. I'll just name them like this...
enum DIErrorLow
{
	ERROR_READY			= 0x00000000, // Ready.
	ERROR_COVER_L		= 0x01000000, // Cover is opened.
	ERROR_CHANGE_DISK	= 0x02000000, // Disk change.
	ERROR_NO_DISK		= 0x03000000, // No Disk.
	ERROR_MOTOR_STOP_L	= 0x04000000, // Motor stop.
	ERROR_NO_DISKID_L	= 0x05000000 // Disk ID not read.
};
enum DIErrorHigh
{
	ERROR_NONE			= 0x000000, // No error.
	ERROR_MOTOR_STOP_H	= 0x020400, // Motor stopped.
	ERROR_NO_DISKID_H	= 0x020401, // Disk ID not read.
	ERROR_COVER_H		= 0x023a00, // Medium not present / Cover opened.
	ERROR_SEEK_NDONE	= 0x030200, // No Seek complete.
	ERROR_READ			= 0x031100, // UnRecoverd read error.
	ERROR_PROTOCOL		= 0x040800, // Transfer protocol error.
	ERROR_INV_CMD		= 0x052000, // Invalid command operation code.
	ERROR_AUDIO_BUF		= 0x052001, // Audio Buffer not set.
	ERROR_BLOCK_OOB		= 0x052100, // Logical block address out of bounds.
	ERROR_INV_FIELD		= 0x052400, // Invalid Field in command packet.
	ERROR_INV_AUDIO		= 0x052401, // Invalid audio command.
	ERROR_INV_PERIOD	= 0x052402, // Configuration out of permitted period.
	ERROR_END_USR_AREA	= 0x056300, // End of user area encountered on this track.
	ERROR_MEDIUM		= 0x062800, // Medium may have changed.
	ERROR_MEDIUM_REQ	= 0x0b5a01 // Operator medium removal request.
};

enum DICommand
{
	DVDLowInquiry				= 0x12,
	DVDLowReadDiskID			= 0x70,
	DVDLowRead					= 0x71,
	DVDLowWaitForCoverClose		= 0x79,
	DVDLowGetCoverReg			= 0x7a, // DVDLowPrepareCoverRegister?
	DVDLowNotifyReset			= 0x7e,
	DVDLowReadDvdPhysical		= 0x80,
	DVDLowReadDvdCopyright		= 0x81,
	DVDLowReadDvdDiscKey		= 0x82,
	DVDLowClearCoverInterrupt	= 0x86,
	DVDLowGetCoverStatus		= 0x88,
	DVDLowReset					= 0x8a,
	DVDLowOpenPartition			= 0x8b,
	DVDLowClosePartition		= 0x8c,
	DVDLowUnencryptedRead		= 0x8d,
	DVDLowEnableDvdVideo		= 0x8e,
	DVDLowReportKey				= 0xa4,
	DVDLowSeek					= 0xab,
	DVDLowReadDvd				= 0xd0,
	DVDLowReadDvdConfig			= 0xd1,
	DVDLowStopLaser				= 0xd2,
	DVDLowOffset				= 0xd9,
	DVDLowReadDiskBca			= 0xda,
	DVDLowRequestDiscStatus		= 0xdb,
	DVDLowRequestRetryNumber	= 0xdc,
	DVDLowSetMaximumRotation	= 0xdd,
	DVDLowSerMeasControl		= 0xdf,
	DVDLowRequestError			= 0xe0,
	DVDLowStopMotor				= 0xe3,
	DVDLowAudioBufferConfig		= 0xe4
};

} // end of namespace DVDInterface

#endif
