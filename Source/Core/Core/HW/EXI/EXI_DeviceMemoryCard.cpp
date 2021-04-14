// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/EXI/EXI_DeviceMemoryCard.h"

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "Common/ChunkFile.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Core/CommonTitles.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/CoreTiming.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Channel.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/GCMemcard/GCMemcard.h"
#include "Core/HW/GCMemcard/GCMemcardDirectory.h"
#include "Core/HW/GCMemcard/GCMemcardRaw.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/Sram.h"
#include "Core/HW/SystemTimers.h"
#include "Core/Movie.h"
#include "DiscIO/Enums.h"

namespace ExpansionInterface
{
#define MC_STATUS_BUSY 0x80
#define MC_STATUS_UNLOCKED 0x40
#define MC_STATUS_SLEEP 0x20
#define MC_STATUS_ERASEERROR 0x10
#define MC_STATUS_PROGRAMEERROR 0x08
#define MC_STATUS_READY 0x01
#define SIZE_TO_Mb (1024 * 8 * 16)

static const u32 MC_TRANSFER_RATE_READ = 512 * 1024;
static const auto MC_TRANSFER_RATE_WRITE = static_cast<u32>(96.125f * 1024.0f);

static std::array<CoreTiming::EventType*, 2> s_et_cmd_done;
static std::array<CoreTiming::EventType*, 2> s_et_transfer_complete;

// Takes care of the nasty recovery of the 'this' pointer from card_index,
// stored in the userdata parameter of the CoreTiming event.
void CEXIMemoryCard::EventCompleteFindInstance(u64 userdata,
                                               std::function<void(CEXIMemoryCard*)> callback)
{
  int card_index = (int)userdata;
  auto* self = static_cast<CEXIMemoryCard*>(
      ExpansionInterface::FindDevice(EXIDEVICE_MEMORYCARD, card_index));
  if (self == nullptr)
  {
    self = static_cast<CEXIMemoryCard*>(
        ExpansionInterface::FindDevice(EXIDEVICE_MEMORYCARDFOLDER, card_index));
  }
  if (self)
  {
    callback(self);
  }
}

void CEXIMemoryCard::CmdDoneCallback(u64 userdata, s64)
{
  EventCompleteFindInstance(userdata, [](CEXIMemoryCard* instance) { instance->CmdDone(); });
}

void CEXIMemoryCard::TransferCompleteCallback(u64 userdata, s64)
{
  EventCompleteFindInstance(userdata,
                            [](CEXIMemoryCard* instance) { instance->TransferComplete(); });
}

void CEXIMemoryCard::Init()
{
  static constexpr char DONE_PREFIX[] = "memcardDone";
  static constexpr char TRANSFER_COMPLETE_PREFIX[] = "memcardTransferComplete";

  static_assert(s_et_cmd_done.size() == s_et_transfer_complete.size(), "Event array size differs");
  for (unsigned int i = 0; i < s_et_cmd_done.size(); ++i)
  {
    std::string name = DONE_PREFIX;
    name += static_cast<char>('A' + i);
    s_et_cmd_done[i] = CoreTiming::RegisterEvent(name, CmdDoneCallback);

    name = TRANSFER_COMPLETE_PREFIX;
    name += static_cast<char>('A' + i);
    s_et_transfer_complete[i] = CoreTiming::RegisterEvent(name, TransferCompleteCallback);
  }
}

void CEXIMemoryCard::Shutdown()
{
  s_et_cmd_done.fill(nullptr);
  s_et_transfer_complete.fill(nullptr);
}

CEXIMemoryCard::CEXIMemoryCard(const int index, bool gci_folder,
                               const Memcard::HeaderData& header_data)
    : m_card_index(index)
{
  ASSERT_MSG(EXPANSIONINTERFACE, static_cast<std::size_t>(index) < s_et_cmd_done.size(),
             "Trying to create invalid memory card index %d.", index);

  // NOTE: When loading a save state, DMA completion callbacks (s_et_transfer_complete) and such
  //   may have been restored, we need to anticipate those arriving.

  m_interrupt_switch = 0;
  m_interrupt_set = false;
  m_command = Command::NintendoID;
  m_status = MC_STATUS_BUSY | MC_STATUS_UNLOCKED | MC_STATUS_READY;
  m_position = 0;
  m_programming_buffer.fill(0);
  // Nintendo Memory Card EXI IDs
  // 0x00000004 Memory Card 59     4Mbit
  // 0x00000008 Memory Card 123    8Mb
  // 0x00000010 Memory Card 251    16Mb
  // 0x00000020 Memory Card 507    32Mb
  // 0x00000040 Memory Card 1019   64Mb
  // 0x00000080 Memory Card 2043   128Mb

  // 0x00000510 16Mb "bigben" card
  // card_id = 0xc243;
  m_card_id = 0xc221;  // It's a Nintendo brand memcard

  if (gci_folder)
  {
    SetupGciFolder(header_data);
  }
  else
  {
    SetupRawMemcard(header_data.m_size_mb);
  }

  m_memory_card_size = m_memory_card->GetCardId() * SIZE_TO_Mb;
  std::array<u8, 20> header{};
  m_memory_card->Read(0, static_cast<s32>(header.size()), header.data());
  SetCardFlashID(header.data(), m_card_index);
}

std::pair<std::string /* path */, bool /* migrate */>
CEXIMemoryCard::GetGCIFolderPath(int card_index, AllowMovieFolder allow_movie_folder)
{
  std::string path_override =
      Config::Get(card_index == 0 ? Config::MAIN_GCI_FOLDER_A_PATH_OVERRIDE :
                                    Config::MAIN_GCI_FOLDER_B_PATH_OVERRIDE);

  if (!path_override.empty())
    return {std::move(path_override), false};

  std::string path = File::GetUserPath(D_GCUSER_IDX);

  const bool use_movie_folder = allow_movie_folder == AllowMovieFolder::Yes &&
                                Movie::IsPlayingInput() && Movie::IsConfigSaved() &&
                                Movie::IsUsingMemcard(card_index) &&
                                Movie::IsStartingFromClearSave();

  if (use_movie_folder)
    path += "Movie" DIR_SEP;

  const DiscIO::Region region = SConfig::ToGameCubeRegion(SConfig::GetInstance().m_region);
  path = path + SConfig::GetDirectoryForRegion(region) + DIR_SEP +
         fmt::format("Card {}", char('A' + card_index));
  return {std::move(path), !use_movie_folder};
}

void CEXIMemoryCard::SetupGciFolder(const Memcard::HeaderData& header_data)
{
  const std::string& game_id = SConfig::GetInstance().GetGameID();
  u32 current_game_id = 0;
  if (game_id.length() >= 4 && game_id != "00000000" &&
      SConfig::GetInstance().GetTitleID() != Titles::SYSTEM_MENU)
  {
    current_game_id = Common::swap32(reinterpret_cast<const u8*>(game_id.c_str()));
  }

  // TODO(C++20): Use structured bindings when we can use C++20 and refer to structured bindings
  // in lambda captures
  const auto folder_path_pair = GetGCIFolderPath(m_card_index, AllowMovieFolder::Yes);
  const std::string& dir_path = folder_path_pair.first;
  const bool migrate = folder_path_pair.second;

  const File::FileInfo file_info(dir_path);
  if (!file_info.Exists())
  {
    if (migrate)  // first use of memcard folder, migrate automatically
      MigrateFromMemcardFile(dir_path + DIR_SEP, m_card_index);
    else
      File::CreateFullPath(dir_path + DIR_SEP);
  }
  else if (!file_info.IsDirectory())
  {
    if (File::Rename(dir_path, dir_path + ".original"))
    {
      PanicAlertFmtT("{0} was not a directory, moved to *.original", dir_path);
      if (migrate)
        MigrateFromMemcardFile(dir_path + DIR_SEP, m_card_index);
      else
        File::CreateFullPath(dir_path + DIR_SEP);
    }
    else  // we tried but the user wants to crash
    {
      // TODO more user friendly abort
      PanicAlertFmtT("{0} is not a directory, failed to move to *.original.\n Verify your "
                     "write permissions or move the file outside of Dolphin",
                     dir_path);
      std::exit(0);
    }
  }

  m_memory_card = std::make_unique<GCMemcardDirectory>(dir_path + DIR_SEP, m_card_index,
                                                       header_data, current_game_id);
}

void CEXIMemoryCard::SetupRawMemcard(u16 size_mb)
{
  const bool is_slot_a = m_card_index == 0;
  std::string filename = is_slot_a ? Config::Get(Config::MAIN_MEMCARD_A_PATH) :
                                     Config::Get(Config::MAIN_MEMCARD_B_PATH);
  if (Movie::IsPlayingInput() && Movie::IsConfigSaved() && Movie::IsUsingMemcard(m_card_index) &&
      Movie::IsStartingFromClearSave())
    filename = File::GetUserPath(D_GCUSER_IDX) + fmt::format("Movie{}.raw", is_slot_a ? 'A' : 'B');

  const std::string region_dir =
      SConfig::GetDirectoryForRegion(SConfig::ToGameCubeRegion(SConfig::GetInstance().m_region));
  MemoryCard::CheckPath(filename, region_dir, is_slot_a);

  if (size_mb == Memcard::MBIT_SIZE_MEMORY_CARD_251)
    filename.insert(filename.find_last_of('.'), ".251");

  m_memory_card = std::make_unique<MemoryCard>(filename, m_card_index, size_mb);
}

CEXIMemoryCard::~CEXIMemoryCard()
{
  CoreTiming::RemoveEvent(s_et_cmd_done[m_card_index]);
  CoreTiming::RemoveEvent(s_et_transfer_complete[m_card_index]);
}

bool CEXIMemoryCard::UseDelayedTransferCompletion() const
{
  return true;
}

bool CEXIMemoryCard::IsPresent() const
{
  return true;
}

void CEXIMemoryCard::CmdDone()
{
  m_status |= MC_STATUS_READY;
  m_status &= ~MC_STATUS_BUSY;

  m_interrupt_set = true;
  ExpansionInterface::UpdateInterrupts();
}

void CEXIMemoryCard::TransferComplete()
{
  // Transfer complete, send interrupt
  ExpansionInterface::GetChannel(m_card_index)->SendTransferComplete();
}

void CEXIMemoryCard::CmdDoneLater(u64 cycles)
{
  CoreTiming::RemoveEvent(s_et_cmd_done[m_card_index]);
  CoreTiming::ScheduleEvent(cycles, s_et_cmd_done[m_card_index], m_card_index);
}

void CEXIMemoryCard::SetCS(int cs)
{
  if (cs)  // not-selected to selected
  {
    m_position = 0;
  }
  else
  {
    switch (m_command)
    {
    case Command::SectorErase:
      if (m_position > 2)
      {
        m_memory_card->ClearBlock(m_address & (m_memory_card_size - 1));
        m_status |= MC_STATUS_BUSY;
        m_status &= ~MC_STATUS_READY;

        //???

        CmdDoneLater(5000);
      }
      break;

    case Command::ChipErase:
      if (m_position > 2)
      {
        // TODO: Investigate on HW, I (LPFaint99) believe that this only
        // erases the system area (Blocks 0-4)
        m_memory_card->ClearAll();
        m_status &= ~MC_STATUS_BUSY;
      }
      break;

    case Command::PageProgram:
      if (m_position >= 5)
      {
        int count = m_position - 5;
        int i = 0;
        m_status &= ~MC_STATUS_BUSY;

        while (count--)
        {
          m_memory_card->Write(m_address, 1, &(m_programming_buffer[i++]));
          i &= 127;
          m_address = (m_address & ~0x1FF) | ((m_address + 1) & 0x1FF);
        }

        CmdDoneLater(5000);
      }
      break;

    default:
      break;
    }
  }
}

bool CEXIMemoryCard::IsInterruptSet()
{
  if (m_interrupt_switch)
    return m_interrupt_set;
  return false;
}

void CEXIMemoryCard::TransferByte(u8& byte)
{
  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI MEMCARD: > {:02x}", byte);
  if (m_position == 0)
  {
    m_command = static_cast<Command>(byte);  // first byte is command
    byte = 0xFF;                             // would be tristate, but we don't care.

    switch (m_command)  // This seems silly, do we really need it?
    {
    case Command::NintendoID:
    case Command::ReadArray:
    case Command::ArrayToBuffer:
    case Command::SetInterrupt:
    case Command::WriteBuffer:
    case Command::ReadStatus:
    case Command::ReadID:
    case Command::ReadErrorBuffer:
    case Command::WakeUp:
    case Command::Sleep:
    case Command::ClearStatus:
    case Command::SectorErase:
    case Command::PageProgram:
    case Command::ExtraByteProgram:
    case Command::ChipErase:
      DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI MEMCARD: command {:02x} at position 0. seems normal.",
                    m_command);
      break;
    default:
      WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI MEMCARD: command {:02x} at position 0", m_command);
      break;
    }
    if (m_command == Command::ClearStatus)
    {
      m_status &= ~MC_STATUS_PROGRAMEERROR;
      m_status &= ~MC_STATUS_ERASEERROR;

      m_status |= MC_STATUS_READY;

      m_interrupt_set = false;

      byte = 0xFF;
      m_position = 0;
    }
  }
  else
  {
    switch (m_command)
    {
    case Command::NintendoID:
      //
      // Nintendo card:
      // 00 | 80 00 00 00 10 00 00 00
      // "bigben" card:
      // 00 | ff 00 00 05 10 00 00 00 00 00 00 00 00 00 00
      // we do it the Nintendo way.
      if (m_position == 1)
        byte = 0x80;  // dummy cycle
      else
        byte = static_cast<u8>(m_memory_card->GetCardId() >> (24 - (((m_position - 2) & 3) * 8)));
      break;

    case Command::ReadArray:
      switch (m_position)
      {
      case 1:  // AD1
        m_address = byte << 17;
        byte = 0xFF;
        break;
      case 2:  // AD2
        m_address |= byte << 9;
        break;
      case 3:  // AD3
        m_address |= (byte & 3) << 7;
        break;
      case 4:  // BA
        m_address |= (byte & 0x7F);
        break;
      }
      if (m_position > 1)  // not specified for 1..8, anyway
      {
        m_memory_card->Read(m_address & (m_memory_card_size - 1), 1, &byte);
        // after 9 bytes, we start incrementing the address,
        // but only the sector offset - the pointer wraps around
        if (m_position >= 9)
          m_address = (m_address & ~0x1FF) | ((m_address + 1) & 0x1FF);
      }
      break;

    case Command::ReadStatus:
      // (unspecified for byte 1)
      byte = m_status;
      break;

    case Command::ReadID:
      if (m_position == 1)  // (unspecified)
        byte = static_cast<u8>(m_card_id >> 8);
      else
        byte = static_cast<u8>((m_position & 1) ? (m_card_id) : (m_card_id >> 8));
      break;

    case Command::SectorErase:
      switch (m_position)
      {
      case 1:  // AD1
        m_address = byte << 17;
        break;
      case 2:  // AD2
        m_address |= byte << 9;
        break;
      }
      byte = 0xFF;
      break;

    case Command::SetInterrupt:
      if (m_position == 1)
      {
        m_interrupt_switch = byte;
      }
      byte = 0xFF;
      break;

    case Command::ChipErase:
      byte = 0xFF;
      break;

    case Command::PageProgram:
      switch (m_position)
      {
      case 1:  // AD1
        m_address = byte << 17;
        break;
      case 2:  // AD2
        m_address |= byte << 9;
        break;
      case 3:  // AD3
        m_address |= (byte & 3) << 7;
        break;
      case 4:  // BA
        m_address |= (byte & 0x7F);
        break;
      }

      if (m_position >= 5)
        m_programming_buffer[((m_position - 5) & 0x7F)] = byte;  // wrap around after 128 bytes

      byte = 0xFF;
      break;

    default:
      WARN_LOG_FMT(EXPANSIONINTERFACE, "EXI MEMCARD: unknown command byte {:02x}", byte);
      byte = 0xFF;
    }
  }
  m_position++;
  DEBUG_LOG_FMT(EXPANSIONINTERFACE, "EXI MEMCARD: < {:02x}", byte);
}

void CEXIMemoryCard::DoState(PointerWrap& p)
{
  // for movie sync, we need to save/load memory card contents (and other data) in savestates.
  // otherwise, we'll assume the user wants to keep their memcards and saves separate,
  // unless we're loading (in which case we let the savestate contents decide, in order to stay
  // aligned with them).
  bool storeContents = (Movie::IsMovieActive());
  p.Do(storeContents);

  if (storeContents)
  {
    p.Do(m_interrupt_switch);
    p.Do(m_interrupt_set);
    p.Do(m_command);
    p.Do(m_status);
    p.Do(m_position);
    p.Do(m_programming_buffer);
    p.Do(m_address);
    m_memory_card->DoState(p);
    p.Do(m_card_index);
  }
}

IEXIDevice* CEXIMemoryCard::FindDevice(TEXIDevices device_type, int custom_index)
{
  if (device_type != m_device_type)
    return nullptr;
  if (custom_index != m_card_index)
    return nullptr;
  return this;
}

// DMA reads are preceded by all of the necessary setup via IMMRead
// read all at once instead of single byte at a time as done by IEXIDevice::DMARead
void CEXIMemoryCard::DMARead(u32 addr, u32 size)
{
  m_memory_card->Read(m_address, size, Memory::GetPointer(addr));

  if ((m_address + size) % Memcard::BLOCK_SIZE == 0)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "reading from block: {:x}", m_address / Memcard::BLOCK_SIZE);
  }

  // Schedule transfer complete later based on read speed
  CoreTiming::ScheduleEvent(size * (SystemTimers::GetTicksPerSecond() / MC_TRANSFER_RATE_READ),
                            s_et_transfer_complete[m_card_index], m_card_index);
}

// DMA write are preceded by all of the necessary setup via IMMWrite
// write all at once instead of single byte at a time as done by IEXIDevice::DMAWrite
void CEXIMemoryCard::DMAWrite(u32 addr, u32 size)
{
  m_memory_card->Write(m_address, size, Memory::GetPointer(addr));

  if (((m_address + size) % Memcard::BLOCK_SIZE) == 0)
  {
    INFO_LOG_FMT(EXPANSIONINTERFACE, "writing to block: {:x}", m_address / Memcard::BLOCK_SIZE);
  }

  // Schedule transfer complete later based on write speed
  CoreTiming::ScheduleEvent(size * (SystemTimers::GetTicksPerSecond() / MC_TRANSFER_RATE_WRITE),
                            s_et_transfer_complete[m_card_index], m_card_index);
}
}  // namespace ExpansionInterface
