// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// Based off of tachtig/twintig http://git.infradead.org/?p=users/segher/wii.git
// Copyright 2007,2008  Segher Boessenkool  <segher@kernel.crashing.org>
// Licensed under the terms of the GNU GPL, version 2
// http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

#include "WiiSaveCrypted.h"
#include "FileUtil.h"
#include "MathUtil.h"
#include "NandPaths.h"
#include <algorithm>

static Common::replace_v replacements;

const u8 SDKey[16] =	{0xAB, 0x01, 0xB9, 0xD8, 0xE1, 0x62, 0x2B, 0x08,
						 0xAF, 0xBA, 0xD8, 0x4D, 0xBF, 0xC2, 0xA5, 0x5D};
const u8 MD5_BLANKER[0x10] = {0x0E, 0x65, 0x37, 0x81, 0x99, 0xBE, 0x45, 0x17,
						0xAB, 0x06, 0xEC, 0x22, 0x45, 0x1A, 0x57, 0x93};
const u32 NG_id = 0x0403AC68;

CWiiSaveCrypted::CWiiSaveCrypted(const char* FileName, u64 TitleID)
 : m_TitleID(TitleID)
{
	Common::ReadReplacements(replacements);
	strcpy(pathData_bin, FileName);
	memcpy(SD_IV, "\x21\x67\x12\xE6\xAA\x1F\x68\x9F\x95\xC5\xA2\x23\x24\xDC\x6A\x98", 0x10);

	if (!TitleID) // Import
	{
		AES_set_decrypt_key(SDKey, 128, &m_AES_KEY);
		do
		{
			b_valid = true;
			ReadHDR();
			ReadBKHDR();
			ImportWiiSaveFiles();
			// TODO: check_sig()
			if (b_valid)
			{
				SuccessAlertT("Successfully imported save files");
				b_tryAgain = false;
			}
			else
			{
				b_tryAgain = AskYesNoT("Import failed, try again?");
			}
		} while(b_tryAgain);
	}
	else
	{
		AES_set_encrypt_key(SDKey, 128, &m_AES_KEY);
		
		if (getPaths(true))
		{
			do
			{
				b_valid = true;
				WriteHDR();
				WriteBKHDR();
				ExportWiiSaveFiles();
				do_sig();
				if (b_valid)
				{
					SuccessAlertT("Successfully exported file to %s", pathData_bin);
					b_tryAgain = false;
				}
				else
				{
					b_tryAgain = AskYesNoT("Export failed, try again?");
				}
			} while(b_tryAgain);
		}
	}
}

void CWiiSaveCrypted::ReadHDR()
{
	File::IOFile fpData_bin(pathData_bin, "rb");
	if (!fpData_bin)
	{
		PanicAlertT("Cannot open %s", pathData_bin);
		b_valid = false;
		return;
	}
	if (!fpData_bin.ReadBytes(&_encryptedHeader, HEADER_SZ))
	{
		PanicAlertT("Failed to read header");
		b_valid = false;
		return;
	}
	fpData_bin.Close();

	AES_cbc_encrypt((const u8*)&_encryptedHeader, (u8*)&_header, HEADER_SZ, &m_AES_KEY, SD_IV, AES_DECRYPT);
	_bannerSize = Common::swap32(_header.hdr.BannerSize);
	if ((_bannerSize < FULL_BNR_MIN) || (_bannerSize > FULL_BNR_MAX) ||
		(((_bannerSize - BNR_SZ) % ICON_SZ) != 0))
	{
		PanicAlertT("Not a Wii save or read failure for file header size %x", _bannerSize);
		b_valid = false;
		return;
	}
	m_TitleID = Common::swap64(_header.hdr.SaveGameTitle);

	memcpy(md5_file, _header.hdr.Md5, 0x10);
	memcpy(_header.hdr.Md5, MD5_BLANKER, 0x10);
	md5((u8*)&_header, HEADER_SZ, md5_calc);
	if (memcmp(md5_file, md5_calc, 0x10))
	{
		PanicAlertT("MD5 mismatch\n %016llx%016llx != %016llx%016llx", Common::swap64(md5_file),Common::swap64(md5_file+8), Common::swap64(md5_calc), Common::swap64(md5_calc+8));
		b_valid= false;
	}
	
	if (!getPaths())
	{
		b_valid = false;
		return;	
	}
	if (!File::Exists(BannerFilePath) || AskYesNoT("%s already exists, overwrite?", BannerFilePath.c_str()))
	{
		INFO_LOG(CONSOLE, "Creating file %s", BannerFilePath.c_str());
		File::IOFile fpBanner_bin(BannerFilePath, "wb");
		fpBanner_bin.WriteBytes(_header.BNR, _bannerSize);
	}
}

void CWiiSaveCrypted::WriteHDR()
{
	if (!b_valid) return;
	memset(&_header, 0, HEADER_SZ);

	u32 bannerSize = File::GetSize(BannerFilePath);
	_header.hdr.BannerSize =  Common::swap32(bannerSize);

	_header.hdr.SaveGameTitle = Common::swap64(m_TitleID);
	memcpy(_header.hdr.Md5, MD5_BLANKER, 0x10);
	_header.hdr.Permissions = 0x35;

	File::IOFile fpBanner_bin(BannerFilePath, "rb");
	if (!fpBanner_bin.ReadBytes(_header.BNR, bannerSize))
	{
		PanicAlertT("Failed to read banner.bin");
		b_valid = false;
		return;
	}
	
	md5((u8*)&_header, HEADER_SZ, md5_calc);
	memcpy(_header.hdr.Md5, md5_calc, 0x10);

	AES_cbc_encrypt((const unsigned char *)&_header, (u8*)&_encryptedHeader, HEADER_SZ, &m_AES_KEY, SD_IV, AES_ENCRYPT);
	
	File::IOFile fpData_bin(pathData_bin, "wb");
	if (!fpData_bin.WriteBytes(&_encryptedHeader, HEADER_SZ))
	{
		PanicAlertT("Failed to write header for %s", pathData_bin);
		b_valid = false;
	}
}



void CWiiSaveCrypted::ReadBKHDR()
{
	if (!b_valid) return;
	
	File::IOFile fpData_bin(pathData_bin, "rb");
	if (!fpData_bin)
	{
		PanicAlertT("Cannot open %s", pathData_bin);
		b_valid = false;
		return;
	}
	fpData_bin.Seek(HEADER_SZ, SEEK_SET);
	if (!fpData_bin.ReadBytes(&bkhdr, BK_SZ))
	{
		PanicAlertT("Failed to read bk header");
		b_valid = false;
		return;
	}
	fpData_bin.Close();
	
	if (bkhdr.size  != Common::swap32(BK_LISTED_SZ) ||
		bkhdr.magic != Common::swap32(BK_HDR_MAGIC))
	{
		PanicAlertT("Invalid Size(%x) or Magic word (%x)", bkhdr.size, bkhdr.magic);
		b_valid = false;
		return;
	}
	
	_numberOfFiles = Common::swap32(bkhdr.numberOfFiles);
	_sizeOfFiles = Common::swap32(bkhdr.sizeOfFiles);
	_totalSize = Common::swap32(bkhdr.totalSize);
	
	if (_sizeOfFiles + FULL_CERT_SZ != _totalSize)
		WARN_LOG(CONSOLE, "Size(%x) + cert(%x) does not equal totalsize(%x)", _sizeOfFiles, FULL_CERT_SZ, _totalSize);
	if (m_TitleID != Common::swap64(bkhdr.SaveGameTitle))
		WARN_LOG(CONSOLE, "Encrypted title (%llx) does not match unencrypted title (%llx)", m_TitleID,  Common::swap64(bkhdr.SaveGameTitle));
}

void CWiiSaveCrypted::WriteBKHDR()
{
	if (!b_valid) return;
	_numberOfFiles = 0;
	_sizeOfFiles = 0;
	
	ScanForFiles(WiiTitlePath, FilesList, &_numberOfFiles, &_sizeOfFiles);
	memset(&bkhdr, 0, BK_SZ);
	bkhdr.size = Common::swap32(BK_LISTED_SZ);
	bkhdr.magic = Common::swap32(BK_HDR_MAGIC);
	bkhdr.NGid = NG_id;
	bkhdr.numberOfFiles = Common::swap32(_numberOfFiles);
	bkhdr.sizeOfFiles = Common::swap32(_sizeOfFiles);
	bkhdr.totalSize = Common::swap32(_sizeOfFiles + FULL_CERT_SZ);
	bkhdr.SaveGameTitle = Common::swap64(m_TitleID);

	File::IOFile fpData_bin(pathData_bin, "ab");
	if (!fpData_bin.WriteBytes(&bkhdr, BK_SZ))
	{
		PanicAlertT("Failed to write bkhdr");
		b_valid = false;
	}
}

void CWiiSaveCrypted::ImportWiiSaveFiles()
{
	if (!b_valid) return;

	File::IOFile fpData_bin(pathData_bin, "rb");
	if (!fpData_bin)
	{
		PanicAlertT("Cannot open %s", pathData_bin);
		b_valid = false;
		return;
	}

	fpData_bin.Seek(HEADER_SZ + BK_SZ, SEEK_SET);


	FileHDR _tmpFileHDR;

	for(u32 i = 0; i < _numberOfFiles; i++)
	{
		memset(&_tmpFileHDR, 0, FILE_HDR_SZ);
		memset(IV, 0, 0x10);
		_fileSize = 0;
		
		if (!fpData_bin.ReadBytes(&_tmpFileHDR, FILE_HDR_SZ))
		{
			PanicAlertT("Failed to write header for file %d", i);
			b_valid = false;
		}
		
		if (Common::swap32(_tmpFileHDR.magic) != FILE_HDR_MAGIC)
		{
			PanicAlertT("Bad File Header");
			break;
		}
		else
		{
			std::string fileName ((char*)_tmpFileHDR.name);
			for (Common::replace_v::const_iterator iter = replacements.begin(); iter != replacements.end(); ++iter)
			{
				for (size_t j = 0; (j = fileName.find(iter->first, j)) != fileName.npos; ++j)
					fileName.replace(j, 1, iter->second);
			}

			std::string fullFilePath = WiiTitlePath + fileName;
			File::CreateFullPath(fullFilePath);
			if (_tmpFileHDR.type == 1)
			{
				_fileSize = Common::swap32(_tmpFileHDR.size);
				u32 RoundedFileSize = ROUND_UP(_fileSize, BLOCK_SZ);
				_encryptedData = new u8[RoundedFileSize];
				_data = new u8[RoundedFileSize];
				if (!fpData_bin.ReadBytes(_encryptedData, RoundedFileSize))
				{
					PanicAlertT("Failed to read data from file %d", i);
					b_valid = false;
					break;
				}
				
				
				memcpy(IV, _tmpFileHDR.IV, 0x10);
				AES_cbc_encrypt((const unsigned char *)_encryptedData, _data, RoundedFileSize, &m_AES_KEY, IV, AES_DECRYPT);
				delete []_encryptedData;
	
				if (!File::Exists(fullFilePath) || AskYesNoT("%s already exists, overwrite?", fullFilePath.c_str()))
				{
					INFO_LOG(CONSOLE, "Creating file %s", fullFilePath.c_str());
	
					File::IOFile fpRawSaveFile(fullFilePath, "wb");
					fpRawSaveFile.WriteBytes(_data, _fileSize);
				}
				delete []_data;
			}
		}	
	}
}

void CWiiSaveCrypted::ExportWiiSaveFiles()
{
	if (!b_valid) return;

	u8 *__ENCdata,
		*__data;

	for(u32 i = 0; i < _numberOfFiles; i++)
	{
		FileHDR tmpFileHDR;
		std::string __name, __ext;
		memset(&tmpFileHDR, 0, FILE_HDR_SZ);

		_fileSize = File::GetSize(FilesList[i]);
		_roundedfileSize = ROUND_UP(_fileSize, BLOCK_SZ);

		tmpFileHDR.magic = Common::swap32(FILE_HDR_MAGIC);
		tmpFileHDR.size = Common::swap32(_fileSize);
		tmpFileHDR.Permissions = 0x35;
		tmpFileHDR.type = File::IsDirectory(FilesList[i]) ? 2 : 1;

		SplitPath(FilesList[i], NULL, &__name, &__ext);
		__name += __ext;

		
		for (Common::replace_v::const_iterator iter = replacements.begin(); iter != replacements.end(); ++iter)
		{
			for (size_t j = 0; (j = __name.find(iter->second, j)) != __name.npos; ++j)
			{
				__name.replace(j, iter->second.length(), 1, iter->first);
			}
		}
		
		if (__name.length() > 0x44)
		{
			PanicAlertT("%s is too long for the filename, max chars is 45", __name.c_str());
			b_valid = false;
			return;
		}
		strncpy((char *)tmpFileHDR.name, __name.c_str(), __name.length());
		
		{
		File::IOFile fpData_bin(pathData_bin, "ab");
		fpData_bin.WriteBytes(&tmpFileHDR, FILE_HDR_SZ);
		}

		if (tmpFileHDR.type == 1)
		{
			if (_fileSize == 0)
			{
				PanicAlertT("%s is a 0 byte file", FilesList[i].c_str());
				b_valid = false;
				return;
			}
			File::IOFile fpRawSaveFile(FilesList[i], "rb");
			if (!fpRawSaveFile)
			{
				PanicAlertT("%s failed to open", FilesList[i].c_str());
				b_valid = false;
			}
			__data = new u8[_roundedfileSize];
			__ENCdata = new u8[_roundedfileSize];
			memset(__data, 0, _roundedfileSize);
			if (!fpRawSaveFile.ReadBytes(__data, _fileSize))
			{
				PanicAlertT("Failed to read data from file: %s", FilesList[i].c_str());
				b_valid = false;
			}

			AES_cbc_encrypt((const u8*)__data, __ENCdata, _roundedfileSize, &m_AES_KEY, tmpFileHDR.IV, AES_ENCRYPT);
			
			File::IOFile fpData_bin(pathData_bin, "ab");
			fpData_bin.WriteBytes(__ENCdata, _roundedfileSize);

			delete [] __data;
			delete [] __ENCdata;

		}
	}
}

void CWiiSaveCrypted::do_sig()
{
	if (!b_valid) return;
	u8 sig[0x40];
	u8 ng_cert[0x180];
	u8 ap_cert[0x180];
	u8 hash[0x14];
	u8 ap_priv[30];
	u8 ap_sig[60];
	char signer[64];
	char name[64];
	u8 *data;
	u32 data_size;
	
	u32 NG_key_id = 0x6AAB8C59;

	u8 NG_priv[30] = { 0, 0xAB, 0xEE, 0xC1, 0xDD, 0xB4, 0xA6, 0x16, 0x6B, 0x70, 0xFD, 0x7E, 0x56, 0x67, 0x70,
					0x57, 0x55, 0x27, 0x38, 0xA3, 0x26, 0xC5, 0x46, 0x16, 0xF7, 0x62, 0xC9, 0xED, 0x73, 0xF2};

	u8 NG_sig[0x3C] = {0, 0xD8, 0x81, 0x63, 0xB2, 0x00, 0x6B, 0x0B, 0x54, 0x82, 0x88, 0x63, 0x81, 0x1C, 0x00,
					0x71, 0x12, 0xED, 0xB7, 0xFD, 0x21, 0xAB, 0x0E, 0x50, 0x0E, 0x1F, 0xBF, 0x78, 0xAD, 0x37,
					0x00, 0x71, 0x8D, 0x82, 0x41, 0xEE, 0x45, 0x11, 0xC7, 0x3B, 0xAC, 0x08, 0xB6, 0x83, 0xDC,
					0x05, 0xB8, 0xA8, 0x90, 0x1F, 0xA8, 0x2A, 0x0E, 0x4E, 0x76, 0xEF, 0x44, 0x72, 0x99, 0xF8};

	sprintf(signer, "Root-CA00000001-MS00000002");
	sprintf(name, "NG%08x", NG_id);
	make_ec_cert(ng_cert, NG_sig, signer, name, NG_priv, NG_key_id);


	memset(ap_priv, 0, sizeof ap_priv);
	ap_priv[10] = 1;

	memset(ap_sig, 81, sizeof ap_sig);	// temp

	sprintf(signer, "Root-CA00000001-MS00000002-NG%08x", NG_id);
	sprintf(name, "AP%08x%08x", 1, 2);
	make_ec_cert(ap_cert, ap_sig, signer, name, ap_priv, 0);

	sha1(ap_cert + 0x80, 0x100, hash);
	generate_ecdsa(ap_sig, ap_sig + 30, NG_priv, hash);
	make_ec_cert(ap_cert, ap_sig, signer, name, ap_priv, 0);

	data_size = Common::swap32(bkhdr.sizeOfFiles)  + 0x80;

	File::IOFile fpData_bin(pathData_bin, "rb");
	if (!fpData_bin)
	{
		b_valid = false;
		return;
	}
	data = new u8[data_size];

	fpData_bin.Seek(0xf0c0, SEEK_SET);
	if (!fpData_bin.ReadBytes(data, data_size))
	{
		b_valid = false;
		return;
	}

	sha1(data, data_size, hash);
	sha1(hash, 20, hash);
	delete []data;

	fpData_bin.Open(pathData_bin, "ab");
	if (!fpData_bin)
	{
		b_valid = false;
		return;
	}
	generate_ecdsa(sig, sig + 30, ap_priv, hash);
	*(u32*)(sig + 60) = Common::swap32(0x2f536969);
	
	fpData_bin.WriteArray(sig, sizeof(sig));
	fpData_bin.WriteArray(ng_cert, sizeof(ng_cert));
	fpData_bin.WriteArray(ap_cert, sizeof(ap_cert));

	b_valid = fpData_bin.IsGood();
}


void CWiiSaveCrypted::make_ec_cert(u8 *cert, u8 *sig, char *signer, char *name, u8 *priv, u32 key_id)
{
	memset(cert, 0, 0x180);
	*(u32*)cert = Common::swap32(0x10002);

	memcpy(cert + 4, sig, 60);
	strcpy((char*)cert + 0x80, signer);
	*(u32*)(cert + 0xc0) = Common::swap32(2);
	strcpy((char*)cert + 0xc4, name);
	*(u32*)(cert + 0x104) = Common::swap32(key_id);
	ec_priv_to_pub(priv, cert + 0x108);
}

bool CWiiSaveCrypted::getPaths(bool forExport)
{
	if (m_TitleID)
	{
		WiiTitlePath = Common::GetTitleDataPath(m_TitleID);
		BannerFilePath = WiiTitlePath + "banner.bin";
	}

	if (forExport)
	{
		char GameID[5];
		sprintf(GameID, "%c%c%c%c",
			(u8)(m_TitleID >> 24) & 0xFF, (u8)(m_TitleID >> 16) & 0xFF,
			(u8)(m_TitleID >>  8) & 0xFF, (u8)m_TitleID & 0xFF);

		if(!File::IsDirectory(WiiTitlePath))
		{
			b_valid = false;
			PanicAlertT("No save folder found for title %s", GameID);
			return false;
		}
		
		if(!File::Exists(BannerFilePath))
		{
			b_valid = false;
			PanicAlertT("No banner file found for title  %s", GameID);
			return false;
		}
		if (strlen(pathData_bin) == 0) 
			strcpy(pathData_bin, "."); // If no path was passed, use current dir
		sprintf(pathData_bin, "%s/private/wii/title/%s/data.bin", pathData_bin, GameID);
		File::CreateFullPath(pathData_bin);
	}
	else
	{
		File::CreateFullPath(WiiTitlePath);	
		if (!AskYesNoT("Warning! it is advised to backup all files in the folder:\n%s\nDo you wish to continue?", WiiTitlePath.c_str()))
			return false;
	}
	return true;
}

void CWiiSaveCrypted::ScanForFiles(std::string savDir, std::vector<std::string>& FileList, u32 *_numFiles, u32 *_sizeFiles)
{
	std::vector<std::string> Directories;
	*_numFiles = *_sizeFiles = 0;

	Directories.push_back(savDir);
	for (u32 i = 0; i < Directories.size(); i++)
	{
		if (i != 0)
		{
			FileList.push_back(Directories[i]);//add dir to fst
		}

		File::FSTEntry FST_Temp;
		File::ScanDirectoryTree(Directories[i], FST_Temp);
		for (u32 j = 0; j < FST_Temp.children.size(); j++)
		{
			if (strncmp(FST_Temp.children.at(j).virtualName.c_str(), "banner.bin", 10) != 0)
			{
				(*_numFiles)++;
				*_sizeFiles += FILE_HDR_SZ + ROUND_UP(FST_Temp.children.at(j).size, BLOCK_SZ);
				
				if (FST_Temp.children.at(j).isDirectory)
				{
					Directories.push_back(FST_Temp.children.at(j).physicalName);
				}
				else
				{
					FileList.push_back(FST_Temp.children.at(j).physicalName);
				}
			}
		}
	}
}

CWiiSaveCrypted::~CWiiSaveCrypted()
{
}

