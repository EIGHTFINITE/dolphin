// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "UICommon/GameFile.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <mbedtls/sha1.h>
#include <pugixml.hpp>

#include "Common/ChunkFile.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/HttpRequest.h"
#include "Common/IOFile.h"
#include "Common/Image.h"
#include "Common/IniFile.h"
#include "Common/MsgHandler.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"
#include "Common/Swap.h"

#include "Core/Config/UISettings.h"
#include "Core/ConfigManager.h"
#include "Core/IOS/ES/Formats.h"
#include "Core/TitleDatabase.h"

#include "DiscIO/Blob.h"
#include "DiscIO/DiscExtractor.h"
#include "DiscIO/Enums.h"
#include "DiscIO/Volume.h"
#include "DiscIO/WiiSaveBanner.h"

namespace UICommon
{
namespace
{
const std::string EMPTY_STRING;

bool UseGameCovers()
{
#ifdef ANDROID
  // Android has its own code for handling covers, written completely in Java.
  // It's best if we disable the C++ cover code on Android to avoid duplicated data and such.
  return false;
#else
  return Config::Get(Config::MAIN_USE_GAME_COVERS);
#endif
}
}  // Anonymous namespace

DiscIO::Language GameFile::GetConfigLanguage() const
{
  return SConfig::GetInstance().GetLanguageAdjustedForRegion(DiscIO::IsWii(m_platform), m_region);
}

bool operator==(const GameBanner& lhs, const GameBanner& rhs)
{
  return std::tie(lhs.buffer, lhs.width, lhs.height) == std::tie(rhs.buffer, rhs.width, rhs.height);
}

bool operator!=(const GameBanner& lhs, const GameBanner& rhs)
{
  return !operator==(lhs, rhs);
}

const std::string& GameFile::Lookup(DiscIO::Language language,
                                    const std::map<DiscIO::Language, std::string>& strings)
{
  auto end = strings.end();
  auto it = strings.find(language);
  if (it != end)
    return it->second;

  // English tends to be a good fallback when the requested language isn't available
  if (language != DiscIO::Language::English)
  {
    it = strings.find(DiscIO::Language::English);
    if (it != end)
      return it->second;
  }

  // If English isn't available either, just pick something
  if (!strings.empty())
    return strings.cbegin()->second;

  return EMPTY_STRING;
}

const std::string&
GameFile::LookupUsingConfigLanguage(const std::map<DiscIO::Language, std::string>& strings) const
{
  return Lookup(GetConfigLanguage(), strings);
}

GameFile::GameFile() = default;

GameFile::GameFile(std::string path) : m_file_path(std::move(path))
{
  m_file_name = PathToFileName(m_file_path);

  {
    std::unique_ptr<DiscIO::Volume> volume(DiscIO::CreateVolume(m_file_path));
    if (volume != nullptr)
    {
      m_platform = volume->GetVolumeType();

      m_short_names = volume->GetShortNames();
      m_long_names = volume->GetLongNames();
      m_short_makers = volume->GetShortMakers();
      m_long_makers = volume->GetLongMakers();
      m_descriptions = volume->GetDescriptions();

      m_region = volume->GetRegion();
      m_country = volume->GetCountry();
      m_blob_type = volume->GetBlobType();
      m_block_size = volume->GetBlobReader().GetBlockSize();
      m_compression_method = volume->GetBlobReader().GetCompressionMethod();
      m_file_size = volume->GetRawSize();
      m_volume_size = volume->GetSize();
      m_volume_size_is_accurate = volume->IsSizeAccurate();
      m_is_datel_disc = volume->IsDatelDisc();
      m_is_nkit = volume->IsNKit();

      m_internal_name = volume->GetInternalName();
      m_game_id = volume->GetGameID();
      m_gametdb_id = volume->GetGameTDBID();
      m_title_id = volume->GetTitleID().value_or(0);
      m_maker_id = volume->GetMakerID();
      m_revision = volume->GetRevision().value_or(0);
      m_disc_number = volume->GetDiscNumber().value_or(0);
      m_apploader_date = volume->GetApploaderDate();

      m_volume_banner.buffer = volume->GetBanner(&m_volume_banner.width, &m_volume_banner.height);

      m_valid = true;
    }
  }

  if (!IsValid() && IsElfOrDol())
  {
    m_valid = true;
    m_file_size = m_volume_size = File::GetSize(m_file_path);
    m_game_id = SConfig::MakeGameID(m_file_name);
    m_volume_size_is_accurate = true;
    m_is_datel_disc = false;
    m_is_nkit = false;
    m_platform = DiscIO::Platform::ELFOrDOL;
    m_blob_type = DiscIO::BlobType::DIRECTORY;
  }
}

GameFile::~GameFile() = default;

bool GameFile::IsValid() const
{
  if (!m_valid)
    return false;

  if (m_platform == DiscIO::Platform::WiiWAD && !IOS::ES::IsChannel(m_title_id))
    return false;

  return true;
}

bool GameFile::CustomCoverChanged()
{
  if (!m_custom_cover.buffer.empty() || !UseGameCovers())
    return false;

  std::string path, name;
  SplitPath(m_file_path, &path, &name, nullptr);

  std::string contents;

  // This icon naming format is intended as an alternative to Homebrew Channel icons
  // for those who don't want to have a Homebrew Channel style folder structure.
  const std::string cover_path = path + name + ".cover.png";
  bool success = File::Exists(cover_path) && File::ReadFileToString(cover_path, contents);

  if (!success)
  {
    const std::string alt_cover_path = path + "cover.png";
    success = File::Exists(alt_cover_path) && File::ReadFileToString(alt_cover_path, contents);
  }

  if (success)
    m_pending.custom_cover.buffer = {contents.begin(), contents.end()};

  return success;
}

void GameFile::DownloadDefaultCover()
{
  if (!m_default_cover.buffer.empty() || !UseGameCovers())
    return;

  const auto cover_path = File::GetUserPath(D_COVERCACHE_IDX) + DIR_SEP;
  const auto png_path = cover_path + m_gametdb_id + ".png";

  // If the cover has already been downloaded, abort
  if (File::Exists(png_path))
    return;

  std::string region_code;
  switch (m_region)
  {
  case DiscIO::Region::NTSC_J:
    region_code = "JA";
    break;
  case DiscIO::Region::NTSC_U:
    region_code = "US";
    break;
  case DiscIO::Region::NTSC_K:
    region_code = "KO";
    break;
  case DiscIO::Region::PAL:
  {
    const auto user_lang = SConfig::GetInstance().GetCurrentLanguage(DiscIO::IsWii(GetPlatform()));
    switch (user_lang)
    {
    case DiscIO::Language::German:
      region_code = "DE";
      break;
    case DiscIO::Language::French:
      region_code = "FR";
      break;
    case DiscIO::Language::Spanish:
      region_code = "ES";
      break;
    case DiscIO::Language::Italian:
      region_code = "IT";
      break;
    case DiscIO::Language::Dutch:
      region_code = "NL";
      break;
    case DiscIO::Language::English:
    default:
      region_code = "EN";
      break;
    }
    break;
  }
  case DiscIO::Region::Unknown:
    region_code = "EN";
    break;
  }

  Common::HttpRequest request;
  constexpr char cover_url[] = "https://art.gametdb.com/wii/cover/{}/{}.png";
  const auto response = request.Get(fmt::format(cover_url, region_code, m_gametdb_id));

  if (!response)
    return;

  File::WriteStringToFile(png_path, std::string(response->begin(), response->end()));
}

bool GameFile::DefaultCoverChanged()
{
  if (!m_default_cover.buffer.empty() || !UseGameCovers())
    return false;

  const auto cover_path = File::GetUserPath(D_COVERCACHE_IDX) + DIR_SEP;

  std::string contents;

  File::ReadFileToString(cover_path + m_gametdb_id + ".png", contents);

  if (contents.empty())
    return false;

  m_pending.default_cover.buffer = {contents.begin(), contents.end()};

  return true;
}

void GameFile::CustomCoverCommit()
{
  m_custom_cover = std::move(m_pending.custom_cover);
}

void GameFile::DefaultCoverCommit()
{
  m_default_cover = std::move(m_pending.default_cover);
}

void GameBanner::DoState(PointerWrap& p)
{
  p.Do(buffer);
  p.Do(width);
  p.Do(height);
}

void GameCover::DoState(PointerWrap& p)
{
  p.Do(buffer);
}

void GameFile::DoState(PointerWrap& p)
{
  p.Do(m_valid);
  p.Do(m_file_path);
  p.Do(m_file_name);

  p.Do(m_file_size);
  p.Do(m_volume_size);
  p.Do(m_volume_size_is_accurate);
  p.Do(m_is_datel_disc);
  p.Do(m_is_nkit);

  p.Do(m_short_names);
  p.Do(m_long_names);
  p.Do(m_short_makers);
  p.Do(m_long_makers);
  p.Do(m_descriptions);
  p.Do(m_internal_name);
  p.Do(m_game_id);
  p.Do(m_gametdb_id);
  p.Do(m_title_id);
  p.Do(m_maker_id);

  p.Do(m_region);
  p.Do(m_country);
  p.Do(m_platform);
  p.Do(m_blob_type);
  p.Do(m_block_size);
  p.Do(m_compression_method);
  p.Do(m_revision);
  p.Do(m_disc_number);
  p.Do(m_apploader_date);

  p.Do(m_custom_name);
  p.Do(m_custom_description);
  p.Do(m_custom_maker);
  m_volume_banner.DoState(p);
  m_custom_banner.DoState(p);
  m_default_cover.DoState(p);
  m_custom_cover.DoState(p);
}

std::string GameFile::GetExtension() const
{
  std::string extension;
  SplitPath(m_file_path, nullptr, nullptr, &extension);
  return extension;
}

bool GameFile::IsElfOrDol() const
{
  const std::string extension = GetExtension();
  return extension == ".elf" || extension == ".dol";
}

bool GameFile::ReadXMLMetadata(const std::string& path)
{
  std::string data;
  if (!File::ReadFileToString(path, data))
    return false;

  pugi::xml_document doc;
  // We use load_buffer instead of load_file to avoid path encoding problems on Windows
  if (!doc.load_buffer(data.data(), data.size()))
    return false;

  const pugi::xml_node app_node = doc.child("app");
  m_pending.custom_name = app_node.child("name").text().as_string();
  m_pending.custom_maker = app_node.child("coder").text().as_string();
  m_pending.custom_description = app_node.child("short_description").text().as_string();

  // Elements that we aren't using:
  // version (can be written in any format)
  // release_date (YYYYmmddHHMMSS format)
  // long_description (can be several screens long!)

  return true;
}

bool GameFile::XMLMetadataChanged()
{
  std::string path, name;
  SplitPath(m_file_path, &path, &name, nullptr);

  // This XML file naming format is intended as an alternative to the Homebrew Channel naming
  // for those who don't want to have a Homebrew Channel style folder structure.
  if (!ReadXMLMetadata(path + name + ".xml"))
  {
    // Homebrew Channel naming. Typical for DOLs and ELFs, but we also support it for volumes.
    if (!ReadXMLMetadata(path + "meta.xml"))
    {
      // If no XML metadata is found, remove any old XML metadata from memory.
      m_pending.custom_banner = {};
    }
  }

  return m_pending.custom_name != m_custom_name && m_pending.custom_maker != m_custom_maker &&
         m_pending.custom_description != m_custom_description;
}

void GameFile::XMLMetadataCommit()
{
  m_custom_name = std::move(m_pending.custom_name);
  m_custom_description = std::move(m_pending.custom_description);
  m_custom_maker = std::move(m_pending.custom_maker);
}

bool GameFile::WiiBannerChanged()
{
  // Wii banners can only be read if there is a save file.
  // In case the cache was created without a save file existing,
  // let's try reading the save file again, because it might exist now.

  if (!m_volume_banner.empty())
    return false;
  if (!DiscIO::IsWii(m_platform))
    return false;

  m_pending.volume_banner.buffer =
      DiscIO::WiiSaveBanner(m_title_id)
          .GetBanner(&m_pending.volume_banner.width, &m_pending.volume_banner.height);

  // We only reach here if the old banner was empty, so if the new banner isn't empty,
  // the new banner is guaranteed to be different from the old banner
  return !m_pending.volume_banner.buffer.empty();
}

void GameFile::WiiBannerCommit()
{
  m_volume_banner = std::move(m_pending.volume_banner);
}

bool GameFile::ReadPNGBanner(const std::string& path)
{
  File::IOFile file(path, "rb");
  if (!file)
    return false;

  std::vector<u8> png_data(file.GetSize());
  if (!file.ReadBytes(png_data.data(), png_data.size()))
    return false;

  GameBanner& banner = m_pending.custom_banner;
  std::vector<u8> data_out;
  if (!Common::LoadPNG(png_data, &data_out, &banner.width, &banner.height))
    return false;

  // Make an ARGB copy of the RGBA data
  banner.buffer.resize(data_out.size() / sizeof(u32));
  for (size_t i = 0; i < banner.buffer.size(); i++)
  {
    const size_t j = i * sizeof(u32);
    banner.buffer[i] = (Common::swap32(data_out.data() + j) >> 8) + (data_out[j] << 24);
  }

  return true;
}

bool GameFile::CustomBannerChanged()
{
  std::string path, name;
  SplitPath(m_file_path, &path, &name, nullptr);

  // This icon naming format is intended as an alternative to the Homebrew Channel naming
  // for those who don't want to have a Homebrew Channel style folder structure.
  if (!ReadPNGBanner(path + name + ".png"))
  {
    // Homebrew Channel icon naming. Typical for DOLs and ELFs, but we also support it for volumes.
    if (!ReadPNGBanner(path + "icon.png"))
    {
      // If no custom icon is found, go back to the non-custom one.
      m_pending.custom_banner = {};
    }
  }

  return m_pending.custom_banner != m_custom_banner;
}

void GameFile::CustomBannerCommit()
{
  m_custom_banner = std::move(m_pending.custom_banner);
}

const std::string& GameFile::GetName(const Core::TitleDatabase& title_database) const
{
  if (!m_custom_name.empty())
    return m_custom_name;

  const std::string& database_name = title_database.GetTitleName(m_gametdb_id, GetConfigLanguage());
  return database_name.empty() ? GetName(Variant::LongAndPossiblyCustom) : database_name;
}

const std::string& GameFile::GetName(Variant variant) const
{
  if (variant == Variant::LongAndPossiblyCustom && !m_custom_name.empty())
    return m_custom_name;

  const std::string& name = variant == Variant::ShortAndNotCustom ? GetShortName() : GetLongName();
  if (!name.empty())
    return name;

  // No usable name, return filename (better than nothing)
  return m_file_name;
}

const std::string& GameFile::GetMaker(Variant variant) const
{
  if (variant == Variant::LongAndPossiblyCustom && !m_custom_maker.empty())
    return m_custom_maker;

  const std::string& maker =
      variant == Variant::ShortAndNotCustom ? GetShortMaker() : GetLongMaker();
  if (!maker.empty())
    return maker;

  if (m_game_id.size() >= 6)
    return DiscIO::GetCompanyFromID(m_maker_id);

  return EMPTY_STRING;
}

const std::string& GameFile::GetDescription(Variant variant) const
{
  if (variant == Variant::LongAndPossiblyCustom && !m_custom_description.empty())
    return m_custom_description;

  return LookupUsingConfigLanguage(m_descriptions);
}

std::vector<DiscIO::Language> GameFile::GetLanguages() const
{
  std::vector<DiscIO::Language> languages;
  // TODO: What if some languages don't have long names but have other strings?
  for (const auto& name : m_long_names)
    languages.push_back(name.first);
  return languages;
}

std::string GameFile::GetNetPlayName(const Core::TitleDatabase& title_database) const
{
  std::vector<std::string> info;
  if (!GetGameID().empty())
    info.push_back(GetGameID());
  if (GetRevision() != 0)
    info.push_back("Revision " + std::to_string(GetRevision()));

  const std::string name = GetName(title_database);

  int disc_number = GetDiscNumber() + 1;

  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
  if (disc_number > 1 &&
      lower_name.find(fmt::format("disc {}", disc_number)) == std::string::npos &&
      lower_name.find(fmt::format("disc{}", disc_number)) == std::string::npos)
  {
    std::string disc_text = "Disc ";
    info.push_back(disc_text + std::to_string(disc_number));
  }
  if (info.empty())
    return name;
  std::ostringstream ss;
  std::copy(info.begin(), info.end() - 1, std::ostream_iterator<std::string>(ss, ", "));
  ss << info.back();
  return name + " (" + ss.str() + ")";
}

std::array<u8, 20> GameFile::GetSyncHash() const
{
  std::array<u8, 20> hash{};

  if (m_platform == DiscIO::Platform::ELFOrDOL)
  {
    std::string buffer;
    if (File::ReadFileToString(m_file_path, buffer))
      mbedtls_sha1_ret(reinterpret_cast<unsigned char*>(buffer.data()), buffer.size(), hash.data());
  }
  else
  {
    if (std::unique_ptr<DiscIO::Volume> volume = DiscIO::CreateVolume(m_file_path))
      hash = volume->GetSyncHash();
  }

  return hash;
}

NetPlay::SyncIdentifier GameFile::GetSyncIdentifier() const
{
  const u64 dol_elf_size = m_platform == DiscIO::Platform::ELFOrDOL ? m_file_size : 0;
  return NetPlay::SyncIdentifier{dol_elf_size,  m_game_id,       m_revision,
                                 m_disc_number, m_is_datel_disc, GetSyncHash()};
}

NetPlay::SyncIdentifierComparison
GameFile::CompareSyncIdentifier(const NetPlay::SyncIdentifier& sync_identifier) const
{
  const bool is_elf_or_dol = m_platform == DiscIO::Platform::ELFOrDOL;
  if ((is_elf_or_dol ? m_file_size : 0) != sync_identifier.dol_elf_size)
    return NetPlay::SyncIdentifierComparison::DifferentGame;

  const auto trim = [](const std::string& str, size_t n) {
    return std::string_view(str.data(), std::min(n, str.size()));
  };

  if (trim(m_game_id, 3) != trim(sync_identifier.game_id, 3))
    return NetPlay::SyncIdentifierComparison::DifferentGame;

  if (m_disc_number != sync_identifier.disc_number || m_is_datel_disc != sync_identifier.is_datel)
    return NetPlay::SyncIdentifierComparison::DifferentGame;

  const NetPlay::SyncIdentifierComparison mismatch_result =
      is_elf_or_dol || m_is_datel_disc ? NetPlay::SyncIdentifierComparison::DifferentGame :
                                         NetPlay::SyncIdentifierComparison::DifferentVersion;

  if (m_game_id != sync_identifier.game_id)
  {
    const bool game_id_is_title_id = m_game_id.size() > 6 || sync_identifier.game_id.size() > 6;
    return game_id_is_title_id ? NetPlay::SyncIdentifierComparison::DifferentGame : mismatch_result;
  }

  if (m_revision != sync_identifier.revision)
    return mismatch_result;

  return GetSyncHash() == sync_identifier.sync_hash ? NetPlay::SyncIdentifierComparison::SameGame :
                                                      mismatch_result;
}

std::string GameFile::GetWiiFSPath() const
{
  ASSERT(DiscIO::IsWii(m_platform));
  return Common::GetTitleDataPath(m_title_id, Common::FROM_CONFIGURED_ROOT);
}

bool GameFile::ShouldShowFileFormatDetails() const
{
  switch (m_blob_type)
  {
  case DiscIO::BlobType::PLAIN:
    break;
  case DiscIO::BlobType::DRIVE:
    return false;
  default:
    return true;
  }

  switch (m_platform)
  {
  case DiscIO::Platform::WiiWAD:
    return false;
  case DiscIO::Platform::ELFOrDOL:
    return false;
  default:
    return true;
  }
}

std::string GameFile::GetFileFormatName() const
{
  switch (m_platform)
  {
  case DiscIO::Platform::WiiWAD:
    return "WAD";

  case DiscIO::Platform::ELFOrDOL:
  {
    std::string extension = GetExtension();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

    // substr removes the dot
    return extension.substr(std::min<size_t>(1, extension.size()));
  }

  default:
  {
    std::string name = DiscIO::GetName(m_blob_type, true);
    if (m_is_nkit)
      name = Common::FmtFormatT("{0} (NKit)", name);
    return name;
  }
  }
}

bool GameFile::ShouldAllowConversion() const
{
  return DiscIO::IsDisc(m_platform) && m_volume_size_is_accurate;
}

const GameBanner& GameFile::GetBannerImage() const
{
  return m_custom_banner.empty() ? m_volume_banner : m_custom_banner;
}

const GameCover& GameFile::GetCoverImage() const
{
  return m_custom_cover.empty() ? m_default_cover : m_custom_cover;
}

}  // namespace UICommon
