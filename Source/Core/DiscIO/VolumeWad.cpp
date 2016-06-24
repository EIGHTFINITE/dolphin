// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"
#include "Common/Logging/Log.h"
#include "DiscIO/Blob.h"
#include "DiscIO/Volume.h"
#include "DiscIO/VolumeWad.h"

#define ALIGN_40(x) ROUND_UP(Common::swap32(x), 0x40)

namespace DiscIO
{
CVolumeWAD::CVolumeWAD(std::unique_ptr<IBlobReader> reader)
	: m_pReader(std::move(reader)), m_offset(0), m_tmd_offset(0), m_opening_bnr_offset(0),
	m_hdr_size(0), m_cert_size(0), m_tick_size(0), m_tmd_size(0), m_data_size(0)
{
	// Source: http://wiibrew.org/wiki/WAD_files
	Read(0x00, 4, (u8*)&m_hdr_size);
	Read(0x08, 4, (u8*)&m_cert_size);
	Read(0x10, 4, (u8*)&m_tick_size);
	Read(0x14, 4, (u8*)&m_tmd_size);
	Read(0x18, 4, (u8*)&m_data_size);

	m_offset = ALIGN_40(m_hdr_size) + ALIGN_40(m_cert_size);
	m_tmd_offset = ALIGN_40(m_hdr_size) + ALIGN_40(m_cert_size) + ALIGN_40(m_tick_size);
	m_opening_bnr_offset = m_tmd_offset + ALIGN_40(m_tmd_size) + ALIGN_40(m_data_size);
}

CVolumeWAD::~CVolumeWAD()
{
}

bool CVolumeWAD::Read(u64 _Offset, u64 _Length, u8* _pBuffer, bool decrypt) const
{
	if (decrypt)
		PanicAlertT("Tried to decrypt data from a non-Wii volume");

	if (m_pReader == nullptr)
		return false;

	return m_pReader->Read(_Offset, _Length, _pBuffer);
}

IVolume::ECountry CVolumeWAD::GetCountry() const
{
	if (!m_pReader)
		return COUNTRY_UNKNOWN;

	// read the last digit of the titleID in the ticket
	u8 country_code;
	Read(m_tmd_offset + 0x0193, 1, &country_code);

	if (country_code == 2) // SYSMENU
	{
		u16 title_version = 0;
		Read(m_tmd_offset + 0x01dc, 2, (u8*)&title_version);
		country_code = GetSysMenuRegion(Common::swap16(title_version));
	}

	return CountrySwitch(country_code);
}

std::string CVolumeWAD::GetUniqueID() const
{
	char GameCode[6];
	if (!Read(m_offset + 0x01E0, 4, (u8*)GameCode))
		return "0";

	std::string temp = GetMakerID();
	GameCode[4] = temp.at(0);
	GameCode[5] = temp.at(1);

	return DecodeString(GameCode);
}

std::string CVolumeWAD::GetMakerID() const
{
	char temp[2] = {1};
	// Some weird channels use 0x0000 in place of the MakerID, so we need a check there
	if (!Read(0x198 + m_tmd_offset, 2, (u8*)temp) || temp[0] == 0 || temp[1] == 0)
		return "00";

	return DecodeString(temp);
}

bool CVolumeWAD::GetTitleID(u64* buffer) const
{
	if (!Read(m_offset + 0x01DC, sizeof(u64), reinterpret_cast<u8*>(buffer)))
		return false;

	*buffer = Common::swap64(*buffer);
	return true;
}

u16 CVolumeWAD::GetRevision() const
{
	u16 revision;
	if (!m_pReader->Read(m_tmd_offset + 0x1dc, 2, (u8*)&revision))
		return 0;

	return Common::swap16(revision);
}

IVolume::EPlatform CVolumeWAD::GetVolumeType() const
{
	return WII_WAD;
}

std::map<IVolume::ELanguage, std::string> CVolumeWAD::GetNames(bool prefer_long) const
{
	std::vector<u8> name_data(NAMES_TOTAL_BYTES);
	if (!Read(m_opening_bnr_offset + 0x9C, NAMES_TOTAL_BYTES, name_data.data()))
		return std::map<IVolume::ELanguage, std::string>();
	return ReadWiiNames(name_data);
}

std::vector<u32> CVolumeWAD::GetBanner(int* width, int* height) const
{
	*width = 0;
	*height = 0;

	u64 title_id;
	if (!GetTitleID(&title_id))
		return std::vector<u32>();

	return GetWiiBanner(width, height, title_id);
}

BlobType CVolumeWAD::GetBlobType() const
{
	return m_pReader ? m_pReader->GetBlobType() : BlobType::PLAIN;
}

u64 CVolumeWAD::GetSize() const
{
	if (m_pReader)
		return m_pReader->GetDataSize();
	else
		return 0;
}

u64 CVolumeWAD::GetRawSize() const
{
	if (m_pReader)
		return m_pReader->GetRawSize();
	else
		return 0;
}

} // namespace
