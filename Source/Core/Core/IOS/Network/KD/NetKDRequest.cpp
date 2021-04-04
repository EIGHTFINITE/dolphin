// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/IOS/Network/KD/NetKDRequest.h"

#include <map>
#include <string>

#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/NandPaths.h"
#include "Common/SettingsHandler.h"

#include "Core/CommonTitles.h"
#include "Core/HW/Memmap.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/IOS/Network/Socket.h"
#include "Core/IOS/Uids.h"

namespace IOS::HLE
{
NetKDRequestDevice::NetKDRequestDevice(Kernel& ios, const std::string& device_name)
    : Device(ios, device_name), config{ios.GetFS()}
{
}

NetKDRequestDevice::~NetKDRequestDevice()
{
  WiiSockMan::GetInstance().Clean();
}

std::optional<IPCReply> NetKDRequestDevice::IOCtl(const IOCtlRequest& request)
{
  s32 return_value = 0;
  switch (request.request)
  {
  case IOCTL_NWC24_SUSPEND_SCHEDULAR:
    // NWC24iResumeForCloseLib  from NWC24SuspendScheduler (Input: none, Output: 32 bytes)
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_SUSPEND_SCHEDULAR - NI");
    WriteReturnValue(0, request.buffer_out);  // no error
    break;

  case IOCTL_NWC24_EXEC_TRY_SUSPEND_SCHEDULAR:  // NWC24iResumeForCloseLib
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_EXEC_TRY_SUSPEND_SCHEDULAR - NI");
    break;

  case IOCTL_NWC24_EXEC_RESUME_SCHEDULAR:  // NWC24iResumeForCloseLib
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_EXEC_RESUME_SCHEDULAR - NI");
    WriteReturnValue(0, request.buffer_out);  // no error
    break;

  case IOCTL_NWC24_STARTUP_SOCKET:  // NWC24iStartupSocket
    WriteReturnValue(0, request.buffer_out);
    Memory::Write_U32(0, request.buffer_out + 4);
    return_value = 0;
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_STARTUP_SOCKET - NI");
    break;

  case IOCTL_NWC24_CLEANUP_SOCKET:
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_CLEANUP_SOCKET");
    WiiSockMan::GetInstance().Clean();
    break;

  case IOCTL_NWC24_LOCK_SOCKET:  // WiiMenu
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_LOCK_SOCKET - NI");
    break;

  case IOCTL_NWC24_UNLOCK_SOCKET:
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_UNLOCK_SOCKET - NI");
    break;

  case IOCTL_NWC24_REQUEST_REGISTER_USER_ID:
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_REGISTER_USER_ID");
    WriteReturnValue(0, request.buffer_out);
    Memory::Write_U32(0, request.buffer_out + 4);
    break;

  case IOCTL_NWC24_REQUEST_GENERATED_USER_ID:  // (Input: none, Output: 32 bytes)
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_GENERATED_USER_ID");
    if (config.CreationStage() == NWC24::NWC24Config::NWC24_IDCS_INITIAL)
    {
      const std::string settings_file_path =
          Common::GetTitleDataPath(Titles::SYSTEM_MENU) + "/" WII_SETTING;
      std::string area, model;

      const auto fs = m_ios.GetFS();
      if (const auto file = fs->OpenFile(PID_KD, PID_KD, settings_file_path, FS::Mode::Read))
      {
        Common::SettingsHandler::Buffer data;
        if (file->Read(data.data(), data.size()))
        {
          const Common::SettingsHandler gen{std::move(data)};
          area = gen.GetValue("AREA");
          model = gen.GetValue("MODEL");
        }
      }

      if (!area.empty() && !model.empty())
      {
        u8 area_code = GetAreaCode(area);
        u8 id_ctr = config.IdGen();
        u8 hardware_model = GetHardwareModel(model);

        u32 HollywoodID = m_ios.GetIOSC().GetDeviceId();
        u64 UserID = 0;

        s32 ret = NWC24MakeUserID(&UserID, HollywoodID, id_ctr, hardware_model, area_code);
        config.SetId(UserID);
        config.IncrementIdGen();
        config.SetCreationStage(NWC24::NWC24Config::NWC24_IDCS_GENERATED);
        config.WriteConfig();

        WriteReturnValue(ret, request.buffer_out);
      }
      else
      {
        WriteReturnValue(NWC24::WC24_ERR_FATAL, request.buffer_out);
      }
    }
    else if (config.CreationStage() == NWC24::NWC24Config::NWC24_IDCS_GENERATED)
    {
      WriteReturnValue(NWC24::WC24_ERR_ID_GENERATED, request.buffer_out);
    }
    else if (config.CreationStage() == NWC24::NWC24Config::NWC24_IDCS_REGISTERED)
    {
      WriteReturnValue(NWC24::WC24_ERR_ID_REGISTERED, request.buffer_out);
    }
    Memory::Write_U64(config.Id(), request.buffer_out + 4);
    Memory::Write_U32(config.CreationStage(), request.buffer_out + 0xC);
    break;

  case IOCTL_NWC24_GET_SCHEDULAR_STAT:
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_GET_SCHEDULAR_STAT - NI");
    break;

  case IOCTL_NWC24_SAVE_MAIL_NOW:
    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_SAVE_MAIL_NOW - NI");
    break;

  case IOCTL_NWC24_REQUEST_SHUTDOWN:
  {
    if (request.buffer_in == 0 || request.buffer_in % 4 != 0 || request.buffer_in_size < 8 ||
        request.buffer_out == 0 || request.buffer_out % 4 != 0 || request.buffer_out_size < 4)
    {
      return_value = IPC_EINVAL;
      ERROR_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_SHUTDOWN = IPC_EINVAL");
      break;
    }

    INFO_LOG_FMT(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_SHUTDOWN");
    [[maybe_unused]] const u32 event = Memory::Read_U32(request.buffer_in);
    // TODO: Advertise shutdown event
    // TODO: Shutdown USB keyboard LEDs if event == 3
    // TODO: IOCTLV_NCD_SETCONFIG
    // TODO: DHCP related features
    // SOGetInterfaceOpt(0xfffe,0x4003);  // IP settings
    // SOGetInterfaceOpt(0xfffe,0xc001);  // DHCP lease time remaining?
    // SOGetInterfaceOpt(0xfffe,0x1003);  // Error
    // Call /dev/net/ip/top 0x1b (SOCleanup), it closes all sockets
    WiiSockMan::GetInstance().Clean();
    return_value = IPC_SUCCESS;
    break;
  }

  default:
    request.Log(GetDeviceName(), Common::Log::IOS_WC24);
  }

  return IPCReply(return_value);
}

u8 NetKDRequestDevice::GetAreaCode(const std::string& area) const
{
  static const std::map<std::string, u8> regions = {
      {"JPN", 0}, {"USA", 1}, {"EUR", 2}, {"AUS", 2}, {"BRA", 1}, {"TWN", 3}, {"ROC", 3},
      {"KOR", 4}, {"HKG", 5}, {"ASI", 5}, {"LTN", 1}, {"SAF", 2}, {"CHN", 6},
  };

  auto entryPos = regions.find(area);
  if (entryPos != regions.end())
    return entryPos->second;

  return 7;  // Unknown
}

u8 NetKDRequestDevice::GetHardwareModel(const std::string& model) const
{
  static const std::map<std::string, u8> models = {
      {"RVL", MODEL_RVL},
      {"RVT", MODEL_RVT},
      {"RVV", MODEL_RVV},
      {"RVD", MODEL_RVD},
  };

  auto entryPos = models.find(model);
  if (entryPos != models.end())
    return entryPos->second;

  return MODEL_ELSE;
}

static u8 u64_get_byte(u64 value, u8 shift)
{
  return (u8)(value >> (shift * 8));
}

static u64 u64_insert_byte(u64 value, u8 shift, u8 byte)
{
  u64 mask = 0x00000000000000FFULL << (shift * 8);
  u64 inst = (u64)byte << (shift * 8);
  return (value & ~mask) | inst;
}

s32 NetKDRequestDevice::NWC24MakeUserID(u64* nwc24_id, u32 hollywood_id, u16 id_ctr,
                                        u8 hardware_model, u8 area_code)
{
  const u8 table2[8] = {0x1, 0x5, 0x0, 0x4, 0x2, 0x3, 0x6, 0x7};
  const u8 table1[16] = {0x4, 0xB, 0x7, 0x9, 0xF, 0x1, 0xD, 0x3,
                         0xC, 0x2, 0x6, 0xE, 0x8, 0x0, 0xA, 0x5};

  u64 mix_id = ((u64)area_code << 50) | ((u64)hardware_model << 47) | ((u64)hollywood_id << 15) |
               ((u64)id_ctr << 10);
  u64 mix_id_copy1 = mix_id;

  int ctr = 0;
  for (ctr = 0; ctr <= 42; ctr++)
  {
    u64 value = mix_id >> (52 - ctr);
    if (value & 1)
    {
      value = 0x0000000000000635ULL << (42 - ctr);
      mix_id ^= value;
    }
  }

  mix_id = (mix_id_copy1 | (mix_id & 0xFFFFFFFFUL)) ^ 0x0000B3B3B3B3B3B3ULL;
  mix_id = (mix_id >> 10) | ((mix_id & 0x3FF) << (11 + 32));

  for (ctr = 0; ctr <= 5; ctr++)
  {
    u8 ret = u64_get_byte(mix_id, ctr);
    u8 foobar = ((table1[(ret >> 4) & 0xF]) << 4) | (table1[ret & 0xF]);
    mix_id = u64_insert_byte(mix_id, ctr, foobar & 0xff);
  }
  u64 mix_id_copy2 = mix_id;

  for (ctr = 0; ctr <= 5; ctr++)
  {
    u8 ret = u64_get_byte(mix_id_copy2, ctr);
    mix_id = u64_insert_byte(mix_id, table2[ctr], ret);
  }

  mix_id &= 0x001FFFFFFFFFFFFFULL;
  mix_id = (mix_id << 1) | ((mix_id >> 52) & 1);

  mix_id ^= 0x00005E5E5E5E5E5EULL;
  mix_id &= 0x001FFFFFFFFFFFFFULL;

  *nwc24_id = mix_id;

  if (mix_id > 9999999999999999ULL)
    return NWC24::WC24_ERR_FATAL;

  return NWC24::WC24_OK;
}
}  // namespace IOS::HLE
