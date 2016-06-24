// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.



// =======================================================
// File description
// -------------
/*  Here we handle /dev/es requests. We have cases for these functions, the exact
	DevKitPro/libogc name is in parenthesis:

	0x20 GetTitleID (ES_GetTitleID) (Input: none, Output: 8 bytes)
	0x1d GetDataDir (ES_GetDataDir) (Input: 8 bytes, Output: 30 bytes)

	0x1b DiGetTicketView (Input: none, Output: 216 bytes)
	0x16 GetConsumption (Input: 8 bytes, Output: 0 bytes, 4 bytes) // there are two output buffers

	0x12 GetNumTicketViews (ES_GetNumTicketViews) (Input: 8 bytes, Output: 4 bytes)
	0x14 GetTMDViewSize (ES_GetTMDViewSize) (Input: ?, Output: ?) // I don't get this anymore,
		it used to come after 0x12

	but only the first two are correctly supported. For the other four we ignore any potential
	input and only write zero to the out buffer. However, most games only use first two,
	but some Nintendo developed games use the other ones to:

	0x1b: Mario Galaxy, Mario Kart, SSBB
	0x16: Mario Galaxy, Mario Kart, SSBB
	0x12: Mario Kart
	0x14: Mario Kart: But only if we don't return a zeroed out buffer for the 0x12 question,
		and instead answer for example 1 will this question appear.

*/
// =============

// need to include this before mbedtls/aes.h,
// otherwise we may not get __STDC_FORMAT_MACROS
#include <cinttypes>
#include <memory>
#include <vector>
#include <mbedtls/aes.h>

#include "Common/ChunkFile.h"
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"
#include "Core/ConfigManager.h"
#include "Core/ec_wii.h"
#include "Core/Movie.h"
#include "Core/Boot/Boot_DOL.h"
#include "Core/HW/DVDInterface.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_es.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "Core/IPC_HLE/WII_IPC_HLE_WiiMote.h"
#include "Core/PowerPC/PowerPC.h"
#include "DiscIO/NANDContentLoader.h"

#ifdef _WIN32
#include <Windows.h>
#endif

std::string CWII_IPC_HLE_Device_es::m_ContentFile;

CWII_IPC_HLE_Device_es::CWII_IPC_HLE_Device_es(u32 _DeviceID, const std::string& _rDeviceName)
	: IWII_IPC_HLE_Device(_DeviceID, _rDeviceName)
	, m_TitleID(-1)
	, m_AccessIdentID(0x6000000)
{
}

static u8 key_sd   [0x10] = {0xab, 0x01, 0xb9, 0xd8, 0xe1, 0x62, 0x2b, 0x08, 0xaf, 0xba, 0xd8, 0x4d, 0xbf, 0xc2, 0xa5, 0x5d};
static u8 key_ecc  [0x1e] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
static u8 key_empty[0x10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// default key table
u8* CWII_IPC_HLE_Device_es::keyTable[11] = {
	key_ecc,   // ECC Private Key
	key_empty, // Console ID
	key_empty, // NAND AES Key
	key_empty, // NAND HMAC
	key_empty, // Common Key
	key_empty, // PRNG seed
	key_sd,    // SD Key
	key_empty, // Unknown
	key_empty, // Unknown
	key_empty, // Unknown
	key_empty, // Unknown
};

CWII_IPC_HLE_Device_es::~CWII_IPC_HLE_Device_es()
{}

void CWII_IPC_HLE_Device_es::LoadWAD(const std::string& _rContentFile)
{
	m_ContentFile = _rContentFile;
}

void CWII_IPC_HLE_Device_es::OpenInternal()
{
	auto& contentLoader = DiscIO::CNANDContentManager::Access().GetNANDLoader(m_ContentFile);

	// check for cd ...
	if (contentLoader.IsValid())
	{
		m_TitleID = contentLoader.GetTitleID();

		m_TitleIDs.clear();
		DiscIO::cUIDsys::AccessInstance().GetTitleIDs(m_TitleIDs);
		// uncomment if  ES_GetOwnedTitlesCount / ES_GetOwnedTitles is implemented
		// m_TitleIDsOwned.clear();
		// DiscIO::cUIDsys::AccessInstance().GetTitleIDs(m_TitleIDsOwned, true);
	}
	else if (DVDInterface::VolumeIsValid())
	{
		// blindly grab the titleID from the disc - it's unencrypted at:
		// offset 0x0F8001DC and 0x0F80044C
		DVDInterface::GetVolume().GetTitleID(&m_TitleID);
	}
	else
	{
		m_TitleID = ((u64)0x00010000 << 32) | 0xF00DBEEF;
	}

	INFO_LOG(WII_IPC_ES, "Set default title to %08x/%08x", (u32)(m_TitleID>>32), (u32)m_TitleID);
}

void CWII_IPC_HLE_Device_es::DoState(PointerWrap& p)
{
	IWII_IPC_HLE_Device::DoState(p);
	p.Do(m_ContentFile);
	OpenInternal();
	p.Do(m_AccessIdentID);
	p.Do(m_TitleIDs);

	u32 Count = (u32)(m_ContentAccessMap.size());
	p.Do(Count);

	u32 CFD = 0;
	u32 Position = 0;
	u64 TitleID = 0;
	u16 Index = 0;
	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		for (u32 i = 0; i < Count; i++)
		{
			p.Do(CFD);
			p.Do(Position);
			p.Do(TitleID);
			p.Do(Index);
			CFD = OpenTitleContent(CFD, TitleID, Index);
			if (CFD != 0xffffffff)
			{
				m_ContentAccessMap[CFD].m_Position = Position;
			}
		}
	}
	else
	{
		for (auto& pair : m_ContentAccessMap)
		{
			CFD = pair.first;
			SContentAccess& Access = pair.second;
			Position = Access.m_Position;
			TitleID = Access.m_TitleID;
			Index = Access.m_Index;
			p.Do(CFD);
			p.Do(Position);
			p.Do(TitleID);
			p.Do(Index);
		}
	}
}

IPCCommandResult CWII_IPC_HLE_Device_es::Open(u32 _CommandAddress, u32 _Mode)
{
	OpenInternal();

	Memory::Write_U32(GetDeviceID(), _CommandAddress+4);
	if (m_Active)
		INFO_LOG(WII_IPC_ES, "Device was re-opened.");
	m_Active = true;
	return GetDefaultReply();
}

IPCCommandResult CWII_IPC_HLE_Device_es::Close(u32 _CommandAddress, bool _bForce)
{
	m_ContentAccessMap.clear();
	m_TitleIDs.clear();
	m_TitleID = -1;
	m_AccessIdentID = 0x6000000;

	INFO_LOG(WII_IPC_ES, "ES: Close");
	if (!_bForce)
		Memory::Write_U32(0, _CommandAddress + 4);
	m_Active = false;
	// clear the NAND content cache to make sure nothing remains open.
	DiscIO::CNANDContentManager::Access().ClearCache();
	return GetDefaultReply();
}

u32 CWII_IPC_HLE_Device_es::OpenTitleContent(u32 CFD, u64 TitleID, u16 Index)
{
	const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);

	if (!Loader.IsValid())
	{
		WARN_LOG(WII_IPC_ES, "ES: loader not valid for %" PRIx64, TitleID);
		return 0xffffffff;
	}

	const DiscIO::SNANDContent* pContent = Loader.GetContentByIndex(Index);

	if (pContent == nullptr)
	{
		return 0xffffffff; //TODO: what is the correct error value here?
	}

	SContentAccess Access;
	Access.m_Position = 0;
	Access.m_Index = pContent->m_Index;
	Access.m_Size = pContent->m_Size;
	Access.m_TitleID = TitleID;

	pContent->m_Data->Open();

	m_ContentAccessMap[CFD] = Access;
	return CFD;
}

IPCCommandResult CWII_IPC_HLE_Device_es::IOCtlV(u32 _CommandAddress)
{
	SIOCtlVBuffer Buffer(_CommandAddress);

	DEBUG_LOG(WII_IPC_ES, "%s (0x%x)", GetDeviceName().c_str(), Buffer.Parameter);

	// Prepare the out buffer(s) with zeroes as a safety precaution
	// to avoid returning bad values
	// XXX: is this still necessary?
	for (u32 i = 0; i < Buffer.NumberPayloadBuffer; i++)
	{
		u32 j;
		for (j = 0; j < Buffer.NumberInBuffer; j++)
		{
			if (Buffer.InBuffer[j].m_Address == Buffer.PayloadBuffer[i].m_Address)
			{
				// The out buffer is the same as one of the in buffers.  Don't zero it.
				break;
			}
		}
		if (j == Buffer.NumberInBuffer)
		{
			Memory::Memset(Buffer.PayloadBuffer[i].m_Address, 0,
				Buffer.PayloadBuffer[i].m_Size);
		}
	}

	switch (Buffer.Parameter)
	{
	case IOCTL_ES_GETDEVICEID:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETDEVICEID no out buffer");

			EcWii &ec = EcWii::GetInstance();
			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETDEVICEID %08X", ec.getNgId());
			Memory::Write_U32(ec.getNgId(), Buffer.PayloadBuffer[0].m_Address);
			Memory::Write_U32(0, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETTITLECONTENTSCNT:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 1);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1);

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);

			const DiscIO::CNANDContentLoader& rNANDContent = AccessContentDevice(TitleID);
			u16 NumberOfPrivateContent = 0;
			if (rNANDContent.IsValid()) // Not sure if dolphin will ever fail this check
			{
				NumberOfPrivateContent = rNANDContent.GetNumEntries();

				if ((u32)(TitleID>>32) == 0x00010000)
					Memory::Write_U32(0, Buffer.PayloadBuffer[0].m_Address);
				else
					Memory::Write_U32(NumberOfPrivateContent, Buffer.PayloadBuffer[0].m_Address);

				Memory::Write_U32(0, _CommandAddress + 0x4);
			}
			else
				Memory::Write_U32((u32)rNANDContent.GetContentSize(), _CommandAddress + 0x4);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLECONTENTSCNT: TitleID: %08x/%08x  content count %i",
				(u32)(TitleID>>32), (u32)TitleID, rNANDContent.IsValid() ? NumberOfPrivateContent : (u32)rNANDContent.GetContentSize());

			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETTITLECONTENTS:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 2, "IOCTL_ES_GETTITLECONTENTS bad in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETTITLECONTENTS bad out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);

			const DiscIO::CNANDContentLoader& rNANDContent = AccessContentDevice(TitleID);
			if (rNANDContent.IsValid()) // Not sure if dolphin will ever fail this check
			{
				for (u16 i = 0; i < rNANDContent.GetNumEntries(); i++)
				{
					Memory::Write_U32(rNANDContent.GetContentByIndex(i)->m_ContentID, Buffer.PayloadBuffer[0].m_Address + i*4);
					INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLECONTENTS: Index %d: %08x", i, rNANDContent.GetContentByIndex(i)->m_ContentID);
				}
				Memory::Write_U32(0, _CommandAddress + 0x4);
			}
			else
			{
				Memory::Write_U32((u32)rNANDContent.GetContentSize(), _CommandAddress + 0x4);
				INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLECONTENTS: Unable to open content %zu",
					rNANDContent.GetContentSize());
			}

			return GetDefaultReply();
		}
		break;


	case IOCTL_ES_OPENTITLECONTENT:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 3);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 0);

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			u32 Index = Memory::Read_U32(Buffer.InBuffer[2].m_Address);

			u32 CFD = OpenTitleContent(m_AccessIdentID++, TitleID, Index);
			Memory::Write_U32(CFD, _CommandAddress + 0x4);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_OPENTITLECONTENT: TitleID: %08x/%08x  Index %i -> got CFD %x", (u32)(TitleID>>32), (u32)TitleID, Index, CFD);

			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_OPENCONTENT:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 1);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 0);
			u32 Index = Memory::Read_U32(Buffer.InBuffer[0].m_Address);

			u32 CFD = OpenTitleContent(m_AccessIdentID++, m_TitleID, Index);
			Memory::Write_U32(CFD, _CommandAddress + 0x4);
			INFO_LOG(WII_IPC_ES, "IOCTL_ES_OPENCONTENT: Index %i -> got CFD %x", Index, CFD);

			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_READCONTENT:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 1);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1);

			u32 CFD = Memory::Read_U32(Buffer.InBuffer[0].m_Address);
			u32 Size = Buffer.PayloadBuffer[0].m_Size;
			u32 Addr = Buffer.PayloadBuffer[0].m_Address;

			auto itr = m_ContentAccessMap.find(CFD);
			if (itr == m_ContentAccessMap.end())
			{
				Memory::Write_U32(-1, _CommandAddress + 0x4);
				return GetDefaultReply();
			}
			SContentAccess& rContent = itr->second;

			u8* pDest = Memory::GetPointer(Addr);

			if (rContent.m_Position + Size > rContent.m_Size)
			{
				Size = rContent.m_Size - rContent.m_Position;
			}

			if (Size > 0)
			{
				if (pDest)
				{
					const DiscIO::CNANDContentLoader& ContentLoader = AccessContentDevice(rContent.m_TitleID);
					// ContentLoader should never be invalid; rContent has been created by it.
					if (ContentLoader.IsValid())
					{
						const DiscIO::SNANDContent* pContent = ContentLoader.GetContentByIndex(rContent.m_Index);
						if (!pContent->m_Data->GetRange(rContent.m_Position, Size, pDest))
							ERROR_LOG(WII_IPC_ES, "ES: failed to read %u bytes from %u!", Size, rContent.m_Position);
					}

					rContent.m_Position += Size;
				}
				else
				{
					PanicAlert("IOCTL_ES_READCONTENT - bad destination");
				}
			}

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_READCONTENT: CFD %x, Address 0x%x, Size %i -> stream pos %i (Index %i)", CFD, Addr, Size, rContent.m_Position, rContent.m_Index);

			Memory::Write_U32(Size, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_CLOSECONTENT:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 1);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 0);

			u32 CFD = Memory::Read_U32(Buffer.InBuffer[0].m_Address);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_CLOSECONTENT: CFD %x", CFD);

			auto itr = m_ContentAccessMap.find(CFD);
			if (itr == m_ContentAccessMap.end())
			{
				Memory::Write_U32(-1, _CommandAddress + 0x4);
				return GetDefaultReply();
			}

			const DiscIO::CNANDContentLoader& ContentLoader = AccessContentDevice(itr->second.m_TitleID);
			// ContentLoader should never be invalid; we shouldn't be here if ES_OPENCONTENT failed before.
			if (ContentLoader.IsValid())
			{
				const DiscIO::SNANDContent* pContent = ContentLoader.GetContentByIndex(itr->second.m_Index);
				pContent->m_Data->Close();
			}

			m_ContentAccessMap.erase(itr);

			Memory::Write_U32(0, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_SEEKCONTENT:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 3);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 0);

			u32 CFD = Memory::Read_U32(Buffer.InBuffer[0].m_Address);
			u32 Addr = Memory::Read_U32(Buffer.InBuffer[1].m_Address);
			u32 Mode = Memory::Read_U32(Buffer.InBuffer[2].m_Address);

			auto itr = m_ContentAccessMap.find(CFD);
			if (itr == m_ContentAccessMap.end())
			{
				Memory::Write_U32(-1, _CommandAddress + 0x4);
				return GetDefaultReply();
			}
			SContentAccess& rContent = itr->second;

			switch (Mode)
			{
			case 0:  // SET
				rContent.m_Position = Addr;
				break;

			case 1:  // CUR
				rContent.m_Position += Addr;
				break;

			case 2:  // END
				rContent.m_Position = rContent.m_Size + Addr;
				break;
			}

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_SEEKCONTENT: CFD %x, Address 0x%x, Mode %i -> Pos %i", CFD, Addr, Mode, rContent.m_Position);

			Memory::Write_U32(rContent.m_Position, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETTITLEDIR:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 1);
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1);

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);

			char* Path = (char*)Memory::GetPointer(Buffer.PayloadBuffer[0].m_Address);
			sprintf(Path, "/title/%08x/%08x/data", (u32)(TitleID >> 32), (u32)TitleID);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLEDIR: %s", Path);
		}
		break;

	case IOCTL_ES_GETTITLEID:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 0);
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETTITLEID no out buffer");

			Memory::Write_U64(m_TitleID, Buffer.PayloadBuffer[0].m_Address);
			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLEID: %08x/%08x", (u32)(m_TitleID>>32), (u32)m_TitleID);
		}
		break;

	case IOCTL_ES_SETUID:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 1, "IOCTL_ES_SETUID no in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 0, "IOCTL_ES_SETUID has a payload, it shouldn't");
			// TODO: fs permissions based on this
			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			INFO_LOG(WII_IPC_ES, "IOCTL_ES_SETUID titleID: %08x/%08x", (u32)(TitleID>>32), (u32)TitleID);
		}
		break;

	case IOCTL_ES_GETTITLECNT:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 0, "IOCTL_ES_GETTITLECNT has an in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETTITLECNT has no out buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.PayloadBuffer[0].m_Size == 4, "IOCTL_ES_GETTITLECNT payload[0].size != 4");

			Memory::Write_U32((u32)m_TitleIDs.size(), Buffer.PayloadBuffer[0].m_Address);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLECNT: Number of Titles %zu", m_TitleIDs.size());

			Memory::Write_U32(0, _CommandAddress + 0x4);

			return GetDefaultReply();
		}
		break;


	case IOCTL_ES_GETTITLES:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 1, "IOCTL_ES_GETTITLES has an in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETTITLES has no out buffer");

			u32 MaxCount = Memory::Read_U32(Buffer.InBuffer[0].m_Address);
			u32 Count = 0;
			for (int i = 0; i < (int)m_TitleIDs.size(); i++)
			{
				Memory::Write_U64(m_TitleIDs[i], Buffer.PayloadBuffer[0].m_Address + i*8);
				INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLES: %08x/%08x", (u32)(m_TitleIDs[i] >> 32), (u32)m_TitleIDs[i]);
				Count++;
				if (Count >= MaxCount)
					break;
			}

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTITLES: Number of titles returned %i", Count);
			Memory::Write_U32(0, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;


	case IOCTL_ES_GETVIEWCNT:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 1, "IOCTL_ES_GETVIEWCNT no in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETVIEWCNT no out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);

			u32 retVal = 0;
			const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);
			u32 ViewCount = static_cast<u32>(Loader.GetTicket().size()) / DiscIO::CNANDContentLoader::TICKET_SIZE;

			if (!ViewCount)
			{
				std::string TicketFilename = Common::GetTicketFileName(TitleID, Common::FROM_SESSION_ROOT);
				if (File::Exists(TicketFilename))
				{
					u32 FileSize = (u32)File::GetSize(TicketFilename);
					_dbg_assert_msg_(WII_IPC_ES, (FileSize % DiscIO::CNANDContentLoader::TICKET_SIZE) == 0, "IOCTL_ES_GETVIEWCNT ticket file size seems to be wrong");

					ViewCount = FileSize / DiscIO::CNANDContentLoader::TICKET_SIZE;
					_dbg_assert_msg_(WII_IPC_ES, (ViewCount>0) && (ViewCount<=4), "IOCTL_ES_GETVIEWCNT ticket count seems to be wrong");
				}
				else if (TitleID >> 32 == 0x00000001)
				{
					// Fake a ticket view to make IOS reload work.
					ViewCount = 1;
				}
				else
				{
					ViewCount = 0;
					if (TitleID == TITLEID_SYSMENU)
					{
						PanicAlertT("There must be a ticket for 00000001/00000002. Your NAND dump is probably incomplete.");
					}
					//retVal = ES_NO_TICKET_INSTALLED;
				}
			}

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETVIEWCNT for titleID: %08x/%08x (View Count = %i)", (u32)(TitleID>>32), (u32)TitleID, ViewCount);

			Memory::Write_U32(ViewCount, Buffer.PayloadBuffer[0].m_Address);
			Memory::Write_U32(retVal, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETVIEWS:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 2, "IOCTL_ES_GETVIEWS no in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETVIEWS no out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			u32 maxViews = Memory::Read_U32(Buffer.InBuffer[1].m_Address);
			u32 retVal = 0;

			const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);

			const std::vector<u8>& ticket = Loader.GetTicket();

			if (ticket.empty())
			{
				std::string TicketFilename = Common::GetTicketFileName(TitleID, Common::FROM_SESSION_ROOT);
				if (File::Exists(TicketFilename))
				{
					File::IOFile pFile(TicketFilename, "rb");
					if (pFile)
					{
						u8 FileTicket[DiscIO::CNANDContentLoader::TICKET_SIZE];
						for (unsigned int View = 0; View != maxViews && pFile.ReadBytes(FileTicket, DiscIO::CNANDContentLoader::TICKET_SIZE); ++View)
						{
							Memory::Write_U32(View, Buffer.PayloadBuffer[0].m_Address + View * 0xD8);
							Memory::CopyToEmu(Buffer.PayloadBuffer[0].m_Address + 4 + View * 0xD8, FileTicket+0x1D0, 212);
						}
					}
				}
				else if (TitleID >> 32 == 0x00000001)
				{
					// For IOS titles, the ticket view isn't normally parsed by either the
					// SDK or libogc, just passed to LaunchTitle, so this
					// shouldn't matter at all.  Just fill out some fields just
					// to be on the safe side.
					u32 Address = Buffer.PayloadBuffer[0].m_Address;
					Memory::Memset(Address, 0, 0xD8);
					Memory::Write_U64(TitleID, Address + 4 + (0x1dc - 0x1d0)); // title ID
					Memory::Write_U16(0xffff, Address + 4 + (0x1e4 - 0x1d0)); // unnnown
					Memory::Write_U32(0xff00, Address + 4 + (0x1ec - 0x1d0)); // access mask
					Memory::Memset(Address + 4 + (0x222 - 0x1d0), 0xff, 0x20); // content permissions
				}
				else
				{
					//retVal = ES_NO_TICKET_INSTALLED;
					PanicAlertT("IOCTL_ES_GETVIEWS: Tried to get data from an unknown ticket: %08x/%08x", (u32)(TitleID >> 32), (u32)TitleID);
				}
			}
			else
			{
				u32 view_count = static_cast<u32>(Loader.GetTicket().size()) / DiscIO::CNANDContentLoader::TICKET_SIZE;
				for (unsigned int view = 0; view != maxViews && view < view_count; ++view)
				{
					Memory::Write_U32(view, Buffer.PayloadBuffer[0].m_Address + view * 0xD8);
					Memory::CopyToEmu(Buffer.PayloadBuffer[0].m_Address + 4 + view * 0xD8,
						&ticket[0x1D0 + (view * DiscIO::CNANDContentLoader::TICKET_SIZE)], 212);
				}
			}

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETVIEWS for titleID: %08x/%08x (MaxViews = %i)", (u32)(TitleID >> 32), (u32)TitleID, maxViews);

			Memory::Write_U32(retVal, _CommandAddress + 0x4);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETTMDVIEWCNT:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 1, "IOCTL_ES_GETTMDVIEWCNT no in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETTMDVIEWCNT no out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);

			const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);

			u32 TMDViewCnt = 0;
			if (Loader.IsValid())
			{
				TMDViewCnt += DiscIO::CNANDContentLoader::TMD_VIEW_SIZE;
				TMDViewCnt += 2; // title version
				TMDViewCnt += 2; // num entries
				TMDViewCnt += (u32)Loader.GetContentSize() * (4+2+2+8); // content id, index, type, size
			}
			Memory::Write_U32(TMDViewCnt, Buffer.PayloadBuffer[0].m_Address);

			Memory::Write_U32(0, _CommandAddress + 0x4);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTMDVIEWCNT: title: %08x/%08x (view size %i)", (u32)(TitleID >> 32), (u32)TitleID, TMDViewCnt);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETTMDVIEWS:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 2, "IOCTL_ES_GETTMDVIEWCNT no in buffer");
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETTMDVIEWCNT no out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			u32 MaxCount = Memory::Read_U32(Buffer.InBuffer[1].m_Address);

			const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTMDVIEWCNT: title: %08x/%08x   buffer size: %i", (u32)(TitleID >> 32), (u32)TitleID, MaxCount);

			if (Loader.IsValid())
			{
				u32 Address = Buffer.PayloadBuffer[0].m_Address;

				Memory::CopyToEmu(Address, Loader.GetTMDView(), DiscIO::CNANDContentLoader::TMD_VIEW_SIZE);
				Address += DiscIO::CNANDContentLoader::TMD_VIEW_SIZE;

				Memory::Write_U16(Loader.GetTitleVersion(), Address); Address += 2;
				Memory::Write_U16(Loader.GetNumEntries(), Address); Address += 2;

				const std::vector<DiscIO::SNANDContent>& rContent = Loader.GetContent();
				for (size_t i=0; i<Loader.GetContentSize(); i++)
				{
					Memory::Write_U32(rContent[i].m_ContentID,  Address); Address += 4;
					Memory::Write_U16(rContent[i].m_Index,      Address); Address += 2;
					Memory::Write_U16(rContent[i].m_Type,       Address); Address += 2;
					Memory::Write_U64(rContent[i].m_Size,       Address); Address += 8;
				}

				_dbg_assert_(WII_IPC_ES, (Address-Buffer.PayloadBuffer[0].m_Address) == Buffer.PayloadBuffer[0].m_Size);
			}
			Memory::Write_U32(0, _CommandAddress + 0x4);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETTMDVIEWS: title: %08x/%08x (buffer size: %i)", (u32)(TitleID >> 32), (u32)TitleID, MaxCount);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_GETCONSUMPTION: // This is at least what crediar's ES module does
		Memory::Write_U32(0, Buffer.PayloadBuffer[1].m_Address);
		Memory::Write_U32(0, _CommandAddress + 0x4);
		WARN_LOG(WII_IPC_ES, "IOCTL_ES_GETCONSUMPTION:%d", Memory::Read_U32(_CommandAddress+4));
		return GetDefaultReply();

	case IOCTL_ES_DELETETICKET:
		{
			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			INFO_LOG(WII_IPC_ES, "IOCTL_ES_DELETETICKET: title: %08x/%08x", (u32)(TitleID >> 32), (u32)TitleID);
			if (File::Delete(Common::GetTicketFileName(TitleID, Common::FROM_SESSION_ROOT)))
			{
				Memory::Write_U32(0, _CommandAddress + 0x4);
			}
			else
			{
				// Presumably return -1017 when delete fails
				Memory::Write_U32(ES_PARAMTER_SIZE_OR_ALIGNMENT, _CommandAddress + 0x4);
			}
		}
		break;
	case IOCTL_ES_DELETETITLECONTENT:
		{
			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			INFO_LOG(WII_IPC_ES, "IOCTL_ES_DELETETITLECONTENT: title: %08x/%08x", (u32)(TitleID >> 32), (u32)TitleID);
			if (DiscIO::CNANDContentManager::Access().RemoveTitle(TitleID, Common::FROM_SESSION_ROOT))
			{
				Memory::Write_U32(0, _CommandAddress + 0x4);
			}
			else
			{
				// Presumably return -1017 when title not installed TODO verify
				Memory::Write_U32(ES_PARAMTER_SIZE_OR_ALIGNMENT, _CommandAddress + 0x4);
			}

		}
		break;
	case IOCTL_ES_GETSTOREDTMDSIZE:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer == 1, "IOCTL_ES_GETSTOREDTMDSIZE no in buffer");
			// _dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_ES_GETSTOREDTMDSIZE no out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);

			_dbg_assert_(WII_IPC_ES, Loader.IsValid());
			u32 TMDCnt = 0;
			if (Loader.IsValid())
			{
				TMDCnt += DiscIO::CNANDContentLoader::TMD_HEADER_SIZE;
				TMDCnt += (u32)Loader.GetContentSize() * DiscIO::CNANDContentLoader::CONTENT_HEADER_SIZE;
			}
			if (Buffer.NumberPayloadBuffer)
				Memory::Write_U32(TMDCnt, Buffer.PayloadBuffer[0].m_Address);

			Memory::Write_U32(0, _CommandAddress + 0x4);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETSTOREDTMDSIZE: title: %08x/%08x (view size %i)", (u32)(TitleID >> 32), (u32)TitleID, TMDCnt);
			return GetDefaultReply();
		}
		break;
	case IOCTL_ES_GETSTOREDTMD:
		{
			_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberInBuffer > 0, "IOCTL_ES_GETSTOREDTMD no in buffer");
			// requires 1 inbuffer and no outbuffer, presumably outbuffer required when second inbuffer is used for maxcount (allocated mem?)
			// called with 1 inbuffer after deleting a titleid
			//_dbg_assert_msg_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1, "IOCTL_ES_GETSTOREDTMD no out buffer");

			u64 TitleID = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			u32 MaxCount = 0;
			if (Buffer.NumberInBuffer > 1)
			{
				// TODO: actually use this param in when writing to the outbuffer :/
				MaxCount = Memory::Read_U32(Buffer.InBuffer[1].m_Address);
			}
			const DiscIO::CNANDContentLoader& Loader = AccessContentDevice(TitleID);


			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETSTOREDTMD: title: %08x/%08x   buffer size: %i", (u32)(TitleID >> 32), (u32)TitleID, MaxCount);

			if (Loader.IsValid() && Buffer.NumberPayloadBuffer)
			{
				u32 Address = Buffer.PayloadBuffer[0].m_Address;

				Memory::CopyToEmu(Address, Loader.GetTMDHeader(), DiscIO::CNANDContentLoader::TMD_HEADER_SIZE);
				Address += DiscIO::CNANDContentLoader::TMD_HEADER_SIZE;

				const std::vector<DiscIO::SNANDContent>& rContent = Loader.GetContent();
				for (size_t i=0; i<Loader.GetContentSize(); i++)
				{
					Memory::CopyToEmu(Address, rContent[i].m_Header, DiscIO::CNANDContentLoader::CONTENT_HEADER_SIZE);
					Address += DiscIO::CNANDContentLoader::CONTENT_HEADER_SIZE;
				}

				_dbg_assert_(WII_IPC_ES, (Address-Buffer.PayloadBuffer[0].m_Address) == Buffer.PayloadBuffer[0].m_Size);
			}
			Memory::Write_U32(0, _CommandAddress + 0x4);

			INFO_LOG(WII_IPC_ES, "IOCTL_ES_GETSTOREDTMD: title: %08x/%08x (buffer size: %i)", (u32)(TitleID >> 32), (u32)TitleID, MaxCount);
			return GetDefaultReply();
		}
		break;

	case IOCTL_ES_ENCRYPT:
		{
			u32 keyIndex    = Memory::Read_U32(Buffer.InBuffer[0].m_Address);
			u8* IV          = Memory::GetPointer(Buffer.InBuffer[1].m_Address);
			u8* source      = Memory::GetPointer(Buffer.InBuffer[2].m_Address);
			u32 size        = Buffer.InBuffer[2].m_Size;
			u8* newIV       = Memory::GetPointer(Buffer.PayloadBuffer[0].m_Address);
			u8* destination = Memory::GetPointer(Buffer.PayloadBuffer[1].m_Address);

			mbedtls_aes_context AES_ctx;
			mbedtls_aes_setkey_enc(&AES_ctx, keyTable[keyIndex], 128);
			memcpy(newIV, IV, 16);
			mbedtls_aes_crypt_cbc(&AES_ctx, MBEDTLS_AES_ENCRYPT, size, newIV, source, destination);

			_dbg_assert_msg_(WII_IPC_ES, keyIndex == 6, "IOCTL_ES_ENCRYPT: Key type is not SD, data will be crap");
		}
		break;

	case IOCTL_ES_DECRYPT:
		{
			u32 keyIndex    = Memory::Read_U32(Buffer.InBuffer[0].m_Address);
			u8* IV          = Memory::GetPointer(Buffer.InBuffer[1].m_Address);
			u8* source      = Memory::GetPointer(Buffer.InBuffer[2].m_Address);
			u32 size        = Buffer.InBuffer[2].m_Size;
			u8* newIV       = Memory::GetPointer(Buffer.PayloadBuffer[0].m_Address);
			u8* destination = Memory::GetPointer(Buffer.PayloadBuffer[1].m_Address);

			mbedtls_aes_context AES_ctx;
			mbedtls_aes_setkey_dec(&AES_ctx, keyTable[keyIndex], 128);
			memcpy(newIV, IV, 16);
			mbedtls_aes_crypt_cbc(&AES_ctx, MBEDTLS_AES_DECRYPT, size, newIV, source, destination);

			_dbg_assert_msg_(WII_IPC_ES, keyIndex == 6, "IOCTL_ES_DECRYPT: Key type is not SD, data will be crap");
		}
		break;


	case IOCTL_ES_LAUNCH:
		{
			_dbg_assert_(WII_IPC_ES, Buffer.NumberInBuffer == 2);
			bool bSuccess = false;
			u16 IOSv = 0xffff;

			u64 TitleID    = Memory::Read_U64(Buffer.InBuffer[0].m_Address);
			u32 view       = Memory::Read_U32(Buffer.InBuffer[1].m_Address);
			u64 ticketid   = Memory::Read_U64(Buffer.InBuffer[1].m_Address+4);
			u32 devicetype = Memory::Read_U32(Buffer.InBuffer[1].m_Address+12);
			u64 titleid    = Memory::Read_U64(Buffer.InBuffer[1].m_Address+16);
			u16 access     = Memory::Read_U16(Buffer.InBuffer[1].m_Address+24);

			// ES_LAUNCH should probably reset thw whole state, which at least means closing all open files.
			// leaving them open through ES_LAUNCH may cause hangs and other funky behavior
			// (supposedly when trying to re-open those files).
			DiscIO::CNANDContentManager::Access().ClearCache();

			std::string tContentFile;
			if ((u32)(TitleID>>32) != 0x00000001 || TitleID == TITLEID_SYSMENU)
			{
				const DiscIO::CNANDContentLoader& ContentLoader = AccessContentDevice(TitleID);
				if (ContentLoader.IsValid())
				{
					u32 bootInd = ContentLoader.GetBootIndex();
					const DiscIO::SNANDContent* pContent = ContentLoader.GetContentByIndex(bootInd);
					if (pContent)
					{
						tContentFile = Common::GetTitleContentPath(TitleID, Common::FROM_SESSION_ROOT);
						std::unique_ptr<CDolLoader> pDolLoader = std::make_unique<CDolLoader>(pContent->m_Data->Get());

						if (pDolLoader->IsValid())
						{
							pDolLoader->Load(); // TODO: Check why sysmenu does not load the DOL correctly
							PC = pDolLoader->GetEntryPoint();
							bSuccess = true;
						}
						else
						{
							PanicAlertT("IOCTL_ES_LAUNCH: The DOL file is invalid!");
							bSuccess = false;
						}

						IOSv = ContentLoader.GetIosVersion();
					}
				}
			}
			else // IOS, MIOS, BC etc
			{
				//TODO: fixme
				// The following is obviously a hack
				// Lie to mem about loading a different IOS
				// someone with an affected game should test
				IOSv = TitleID & 0xffff;
				bSuccess = true;
			}
			if (!bSuccess)
			{
				PanicAlertT("IOCTL_ES_LAUNCH: Game tried to reload a title that is not available in your NAND dump\n"
					"TitleID %016" PRIx64".\n Dolphin will likely hang now.", TitleID);
			}
			else
			{
				CWII_IPC_HLE_Device_usb_oh1_57e_305* s_Usb = GetUsbPointer();
				size_t size = s_Usb->m_WiiMotes.size();
				bool* wiiMoteConnected = new bool[size];
				for (unsigned int i = 0; i < size; i++)
					wiiMoteConnected[i] = s_Usb->m_WiiMotes[i].IsConnected();

				WII_IPC_HLE_Interface::Reset(true);
				WII_IPC_HLE_Interface::Init();
				s_Usb = GetUsbPointer();
				for (unsigned int i = 0; i < s_Usb->m_WiiMotes.size(); i++)
				{
					if (wiiMoteConnected[i])
					{
						s_Usb->m_WiiMotes[i].Activate(false);
						s_Usb->m_WiiMotes[i].Activate(true);
					}
					else
					{
						s_Usb->m_WiiMotes[i].Activate(false);
					}
				}

				delete[] wiiMoteConnected;
				WII_IPC_HLE_Interface::SetDefaultContentFile(tContentFile);
			}
			// Pass the "#002 check"
			// Apploader should write the IOS version and revision to 0x3140, and compare it
			// to 0x3188 to pass the check, but we don't do it, and i don't know where to read the IOS rev...
			// Currently we just write 0xFFFF for the revision, copy manually and it works fine :p
			// TODO : figure it correctly : where should we read the IOS rev that the wad "needs" ?
			Memory::Write_U16(IOSv, 0x00003140);
			Memory::Write_U16(0xFFFF, 0x00003142);
			Memory::Write_U32(Memory::Read_U32(0x00003140), 0x00003188);

			//TODO: provide correct return code when bSuccess= false
			Memory::Write_U32(0, _CommandAddress + 0x4);

			ERROR_LOG(WII_IPC_ES, "IOCTL_ES_LAUNCH %016" PRIx64 " %08x %016" PRIx64 " %08x %016" PRIx64 " %04x", TitleID,view,ticketid,devicetype,titleid,access);
			//                     IOCTL_ES_LAUNCH 0001000248414341 00000001 0001c0fef3df2cfa 00000000 0001000248414341 ffff

			// This is necessary because Reset(true) above deleted this object.  Ew.

			// The original hardware overwrites the command type with the async reply type.
			Memory::Write_U32(IPC_REP_ASYNC, _CommandAddress);
			// IOS also seems to write back the command that was responded to in the FD field.
			Memory::Write_U32(IPC_CMD_IOCTLV, _CommandAddress + 8);

			// Generate a "reply" to the IPC command.  ES_LAUNCH is unique because it
			// involves restarting IOS; IOS generates two acknowledgements in a row.
			WII_IPC_HLE_Interface::EnqueueCommandAcknowledgement(_CommandAddress, 0);
			return GetNoReply();
		}
		break;

	case IOCTL_ES_CHECKKOREAREGION: //note by DacoTaco : name is unknown, i just tried to name it SOMETHING
		//IOS70 has this to let system menu 4.2 check if the console is region changed. it returns -1017
		//if the IOS didn't find the Korean keys and 0 if it does. 0 leads to a error 003
		WARN_LOG(WII_IPC_ES,"IOCTL_ES_CHECKKOREAREGION: Title checked for Korean keys.");
		Memory::Write_U32(ES_PARAMTER_SIZE_OR_ALIGNMENT , _CommandAddress + 0x4);
		return GetDefaultReply();

	case IOCTL_ES_GETDEVICECERT: // (Input: none, Output: 384 bytes)
		{
			WARN_LOG(WII_IPC_ES, "IOCTL_ES_GETDEVICECERT");
			_dbg_assert_(WII_IPC_ES, Buffer.NumberPayloadBuffer == 1);
			u8* destination = Memory::GetPointer(Buffer.PayloadBuffer[0].m_Address);

			EcWii &ec = EcWii::GetInstance();
			get_ng_cert(destination, ec.getNgId(), ec.getNgKeyId(), ec.getNgPriv(), ec.getNgSig());
		}
		break;

	case IOCTL_ES_SIGN:
		{
			WARN_LOG(WII_IPC_ES, "IOCTL_ES_SIGN");
			u8 *ap_cert_out = Memory::GetPointer(Buffer.PayloadBuffer[1].m_Address);
			u8 *data = Memory::GetPointer(Buffer.InBuffer[0].m_Address);
			u32 data_size = Buffer.InBuffer[0].m_Size;
			u8 *sig_out =  Memory::GetPointer(Buffer.PayloadBuffer[0].m_Address);

			EcWii &ec = EcWii::GetInstance();
			get_ap_sig_and_cert(sig_out, ap_cert_out, m_TitleID, data, data_size, ec.getNgPriv(), ec.getNgId());
		}
		break;

	case IOCTL_ES_GETBOOT2VERSION:
		{
			WARN_LOG(WII_IPC_ES, "IOCTL_ES_GETBOOT2VERSION");

			Memory::Write_U32(4, Buffer.PayloadBuffer[0].m_Address); // as of 26/02/2012, this was latest bootmii version
		}
		break;

	// ===============================================================================================
	// unsupported functions
	// ===============================================================================================
	case IOCTL_ES_DIGETTICKETVIEW: // (Input: none, Output: 216 bytes) bug crediar :D
		WARN_LOG(WII_IPC_ES, "IOCTL_ES_DIGETTICKETVIEW: this looks really wrong...");
		break;

	case IOCTL_ES_GETOWNEDTITLECNT:
		WARN_LOG(WII_IPC_ES, "IOCTL_ES_GETOWNEDTITLECNT");
		Memory::Write_U32(0, Buffer.PayloadBuffer[0].m_Address);
		break;

	default:
		WARN_LOG(WII_IPC_ES, "CWII_IPC_HLE_Device_es: 0x%x", Buffer.Parameter);
		DumpCommands(_CommandAddress, 8, LogTypes::WII_IPC_ES);
		INFO_LOG(WII_IPC_ES, "command.Parameter: 0x%08x", Buffer.Parameter);
		break;
	}

	// Write return value (0 means OK)
	Memory::Write_U32(0, _CommandAddress + 0x4);

	return GetDefaultReply();
}

const DiscIO::CNANDContentLoader& CWII_IPC_HLE_Device_es::AccessContentDevice(u64 title_id)
{
	// for WADs, the passed title id and the stored title id match; along with m_ContentFile being set to the
	// actual WAD file name. We cannot simply get a NAND Loader for the title id in those cases, since the WAD
	// need not be installed in the NAND, but it could be opened directly from a WAD file anywhere on disk.
	if (m_TitleID == title_id && !m_ContentFile.empty())
		return DiscIO::CNANDContentManager::Access().GetNANDLoader(m_ContentFile);

	return DiscIO::CNANDContentManager::Access().GetNANDLoader(title_id, Common::FROM_SESSION_ROOT);
}

u32 CWII_IPC_HLE_Device_es::ES_DIVerify(const std::vector<u8>& tmd)
{
	u64 title_id = 0xDEADBEEFDEADBEEFull;
	u64 tmd_title_id = Common::swap64(&tmd[0x18C]);

	DVDInterface::GetVolume().GetTitleID(&title_id);
	if (title_id != tmd_title_id)
		return -1;

	std::string tmd_path  = Common::GetTMDFileName(tmd_title_id, Common::FROM_SESSION_ROOT);

	File::CreateFullPath(tmd_path);
	File::CreateFullPath(Common::GetTitleDataPath(tmd_title_id, Common::FROM_SESSION_ROOT));

	Movie::g_titleID = tmd_title_id;
	std::string save_path = Common::GetTitleDataPath(tmd_title_id, Common::FROM_SESSION_ROOT);
	if (Movie::IsRecordingInput())
	{
		// TODO: Check for the actual save data
		if (File::Exists(save_path + "banner.bin"))
			Movie::g_bClearSave = false;
		else
			Movie::g_bClearSave = true;
	}

	// TODO: Force the game to save to another location, instead of moving the user's save.
	if (Movie::IsPlayingInput() && Movie::IsConfigSaved() && Movie::IsStartingFromClearSave())
	{
		if (File::Exists(save_path + "banner.bin"))
		{
			if (File::Exists(save_path + "../backup/"))
			{
				// The last run of this game must have been to play back a movie, so their save is already backed up.
				File::DeleteDirRecursively(save_path);
			}
			else
			{
				#ifdef _WIN32
					MoveFile(UTF8ToTStr(save_path).c_str(), UTF8ToTStr(save_path + "../backup/").c_str());
				#else
					File::CopyDir(save_path, save_path + "../backup/");
					File::DeleteDirRecursively(save_path);
				#endif
			}
		}
	}
	else if (File::Exists(save_path + "../backup/"))
	{
		// Delete the save made by a previous movie, and copy back the user's save.
		if (File::Exists(save_path + "banner.bin"))
			File::DeleteDirRecursively(save_path);
		#ifdef _WIN32
			MoveFile(UTF8ToTStr(save_path + "../backup/").c_str(), UTF8ToTStr(save_path).c_str());
		#else
			File::CopyDir(save_path + "../backup/", save_path);
			File::DeleteDirRecursively(save_path + "../backup/");
		#endif
	}

	if (!File::Exists(tmd_path))
	{
		File::IOFile tmd_file(tmd_path, "wb");
		if (!tmd_file.WriteBytes(tmd.data(), tmd.size()))
			ERROR_LOG(WII_IPC_ES, "DIVerify failed to write disc TMD to NAND.");
	}
	DiscIO::cUIDsys::AccessInstance().AddTitle(tmd_title_id);
	// DI_VERIFY writes to title.tmd, which is read and cached inside the NAND Content Manager.
	// clear the cache to avoid content access mismatches.
	DiscIO::CNANDContentManager::Access().ClearCache();
	return 0;
}
