// Copyright 2011 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// File description
// -------------
// Purpose of this file: Collect boot settings for Core::Init()

// Call sequence: This file has one of the first function called when a game is booted,
// the boot sequence in the code is:

// DolphinWX:    FrameTools.cpp         StartGame
// Core          BootManager.cpp        BootCore
//               Core.cpp               Init                     Thread creation
//                                      EmuThread                Calls CBoot::BootUp
//               Boot.cpp               CBoot::BootUp()
//                                      CBoot::EmulatedBS2_Wii() / GC() or Load_BS2()

#include "Core/BootManager.h"

#include <algorithm>
#include <array>
#include <string>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"

#include "Core/Boot/Boot.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigLoaders/NetPlayConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Sram.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/WiiRoot.h"

#include "DiscIO/Enums.h"

#include "VideoCommon/VideoBackendBase.h"

namespace BootManager
{
// TODO this is an ugly hack which allows us to restore values trampled by per-game settings
// Apply fire liberally
struct ConfigCache
{
public:
  // fill the cache with values from the configuration
  void SaveConfig(const SConfig& config);
  // restore values to the configuration from the cache
  void RestoreConfig(SConfig* config);

  // These store if the relevant setting should be reset back later (true) or if it should be left
  // alone on restore (false)
  bool bSetEmulationSpeed;
  bool bSetVolume;
  std::array<bool, MAX_BBMOTES> bSetWiimoteSource;
  std::array<bool, SerialInterface::MAX_SI_CHANNELS> bSetPads;
  std::array<bool, ExpansionInterface::MAX_EXI_CHANNELS> bSetEXIDevice;

private:
  bool valid;
  bool bCPUThread;
  bool bJITFollowBranch;
  bool bEnableCheats;
  bool bSyncGPUOnSkipIdleHack;
  bool bFPRF;
  bool bAccurateNaNs;
  bool bMMU;
  bool bLowDCBZHack;
  bool bDisableICache;
  bool m_EnableJIT;
  bool bSyncGPU;
  int iSyncGpuMaxDistance;
  int iSyncGpuMinDistance;
  float fSyncGpuOverclock;
  bool bFastDiscSpeed;
  bool bDSPHLE;
  bool bHLE_BS2;
  int iSelectedLanguage;
  PowerPC::CPUCore cpu_core;
  int Volume;
  float m_EmulationSpeed;
  float m_OCFactor;
  bool m_OCEnable;
  bool m_bt_passthrough_enabled;
  std::string sBackend;
  std::string m_strGPUDeterminismMode;
  std::array<WiimoteSource, MAX_BBMOTES> iWiimoteSource;
  std::array<SerialInterface::SIDevices, SerialInterface::MAX_SI_CHANNELS> Pads;
  std::array<ExpansionInterface::TEXIDevices, ExpansionInterface::MAX_EXI_CHANNELS> m_EXIDevice;
};

void ConfigCache::SaveConfig(const SConfig& config)
{
  valid = true;

  bCPUThread = config.bCPUThread;
  bJITFollowBranch = config.bJITFollowBranch;
  bEnableCheats = config.bEnableCheats;
  bSyncGPUOnSkipIdleHack = config.bSyncGPUOnSkipIdleHack;
  bFPRF = config.bFPRF;
  bAccurateNaNs = config.bAccurateNaNs;
  bDisableICache = config.bDisableICache;
  bMMU = config.bMMU;
  m_EnableJIT = config.m_DSPEnableJIT;
  bSyncGPU = config.bSyncGPU;
  iSyncGpuMaxDistance = config.iSyncGpuMaxDistance;
  iSyncGpuMinDistance = config.iSyncGpuMinDistance;
  fSyncGpuOverclock = config.fSyncGpuOverclock;
  bFastDiscSpeed = config.bFastDiscSpeed;
  bDSPHLE = config.bDSPHLE;
  bHLE_BS2 = config.bHLE_BS2;
  iSelectedLanguage = config.SelectedLanguage;
  cpu_core = config.cpu_core;
  Volume = config.m_Volume;
  m_EmulationSpeed = config.m_EmulationSpeed;
  sBackend = config.sBackend;
  m_strGPUDeterminismMode = config.m_strGPUDeterminismMode;
  m_OCFactor = config.m_OCFactor;
  m_OCEnable = config.m_OCEnable;
  m_bt_passthrough_enabled = config.m_bt_passthrough_enabled;

  for (int i = 0; i != MAX_BBMOTES; ++i)
    iWiimoteSource[i] = WiimoteCommon::GetSource(i);

  std::copy(std::begin(config.m_SIDevice), std::end(config.m_SIDevice), std::begin(Pads));
  std::copy(std::begin(config.m_EXIDevice), std::end(config.m_EXIDevice), std::begin(m_EXIDevice));

  bSetEmulationSpeed = false;
  bSetVolume = false;
  bSetWiimoteSource.fill(false);
  bSetPads.fill(false);
  bSetEXIDevice.fill(false);
}

void ConfigCache::RestoreConfig(SConfig* config)
{
  if (!valid)
    return;

  valid = false;

  config->bCPUThread = bCPUThread;
  config->bJITFollowBranch = bJITFollowBranch;
  config->bEnableCheats = bEnableCheats;
  config->bSyncGPUOnSkipIdleHack = bSyncGPUOnSkipIdleHack;
  config->bFPRF = bFPRF;
  config->bAccurateNaNs = bAccurateNaNs;
  config->bDisableICache = bDisableICache;
  config->bMMU = bMMU;
  config->bLowDCBZHack = bLowDCBZHack;
  config->m_DSPEnableJIT = m_EnableJIT;
  config->bSyncGPU = bSyncGPU;
  config->iSyncGpuMaxDistance = iSyncGpuMaxDistance;
  config->iSyncGpuMinDistance = iSyncGpuMinDistance;
  config->fSyncGpuOverclock = fSyncGpuOverclock;
  config->bFastDiscSpeed = bFastDiscSpeed;
  config->bDSPHLE = bDSPHLE;
  config->bHLE_BS2 = bHLE_BS2;
  config->SelectedLanguage = iSelectedLanguage;
  config->cpu_core = cpu_core;

  // Only change these back if they were actually set by game ini, since they can be changed while a
  // game is running.
  if (bSetVolume)
    config->m_Volume = Volume;

  if (config->bWii)
  {
    for (unsigned int i = 0; i < MAX_BBMOTES; ++i)
    {
      if (bSetWiimoteSource[i])
        WiimoteCommon::SetSource(i, iWiimoteSource[i]);
    }
  }

  for (unsigned int i = 0; i < SerialInterface::MAX_SI_CHANNELS; ++i)
  {
    if (bSetPads[i])
      config->m_SIDevice[i] = Pads[i];
  }

  if (bSetEmulationSpeed)
    config->m_EmulationSpeed = m_EmulationSpeed;

  for (unsigned int i = 0; i < ExpansionInterface::MAX_EXI_CHANNELS; ++i)
  {
    if (bSetEXIDevice[i])
      config->m_EXIDevice[i] = m_EXIDevice[i];
  }

  config->sBackend = sBackend;
  config->m_strGPUDeterminismMode = m_strGPUDeterminismMode;
  config->m_OCFactor = m_OCFactor;
  config->m_OCEnable = m_OCEnable;
  config->m_bt_passthrough_enabled = m_bt_passthrough_enabled;
}

static ConfigCache config_cache;

void SetEmulationSpeedReset(bool value)
{
  config_cache.bSetEmulationSpeed = value;
}

static GPUDeterminismMode ParseGPUDeterminismMode(const std::string& mode)
{
  if (mode == "auto")
    return GPUDeterminismMode::Auto;
  if (mode == "none")
    return GPUDeterminismMode::Disabled;
  if (mode == "fake-completion")
    return GPUDeterminismMode::FakeCompletion;

  NOTICE_LOG_FMT(BOOT, "Unknown GPU determinism mode {}", mode);
  return GPUDeterminismMode::Auto;
}

// Boot the ISO or file
bool BootCore(std::unique_ptr<BootParameters> boot, const WindowSystemInfo& wsi)
{
  if (!boot)
    return false;

  SConfig& StartUp = SConfig::GetInstance();

  StartUp.bRunCompareClient = false;
  StartUp.bRunCompareServer = false;

  config_cache.SaveConfig(StartUp);

  if (!StartUp.SetPathsAndGameMetadata(*boot))
    return false;

  // Load game specific settings
  if (!std::holds_alternative<BootParameters::IPL>(boot->parameters))
  {
    IniFile game_ini = StartUp.LoadGameIni();

    // General settings
    IniFile::Section* core_section = game_ini.GetOrCreateSection("Core");
    IniFile::Section* dsp_section = game_ini.GetOrCreateSection("DSP");
    IniFile::Section* controls_section = game_ini.GetOrCreateSection("Controls");

    core_section->Get("CPUThread", &StartUp.bCPUThread, StartUp.bCPUThread);
    core_section->Get("JITFollowBranch", &StartUp.bJITFollowBranch, StartUp.bJITFollowBranch);
    core_section->Get("EnableCheats", &StartUp.bEnableCheats, StartUp.bEnableCheats);
    core_section->Get("SyncOnSkipIdle", &StartUp.bSyncGPUOnSkipIdleHack,
                      StartUp.bSyncGPUOnSkipIdleHack);
    core_section->Get("FPRF", &StartUp.bFPRF, StartUp.bFPRF);
    core_section->Get("AccurateNaNs", &StartUp.bAccurateNaNs, StartUp.bAccurateNaNs);
    core_section->Get("DisableICache", &StartUp.bDisableICache, StartUp.bDisableICache);
    core_section->Get("MMU", &StartUp.bMMU, StartUp.bMMU);
    core_section->Get("LowDCBZHack", &StartUp.bLowDCBZHack, StartUp.bLowDCBZHack);
    core_section->Get("SyncGPU", &StartUp.bSyncGPU, StartUp.bSyncGPU);
    core_section->Get("FastDiscSpeed", &StartUp.bFastDiscSpeed, StartUp.bFastDiscSpeed);
    core_section->Get("DSPHLE", &StartUp.bDSPHLE, StartUp.bDSPHLE);
    core_section->Get("CPUCore", &StartUp.cpu_core, StartUp.cpu_core);
    core_section->Get("HLE_BS2", &StartUp.bHLE_BS2, StartUp.bHLE_BS2);
    core_section->Get("GameCubeLanguage", &StartUp.SelectedLanguage, StartUp.SelectedLanguage);
    if (core_section->Get("EmulationSpeed", &StartUp.m_EmulationSpeed, StartUp.m_EmulationSpeed))
      config_cache.bSetEmulationSpeed = true;

    if (dsp_section->Get("Volume", &StartUp.m_Volume, StartUp.m_Volume))
      config_cache.bSetVolume = true;
    dsp_section->Get("EnableJIT", &StartUp.m_DSPEnableJIT, StartUp.m_DSPEnableJIT);
    dsp_section->Get("Backend", &StartUp.sBackend, StartUp.sBackend);
    core_section->Get("GPUDeterminismMode", &StartUp.m_strGPUDeterminismMode,
                      StartUp.m_strGPUDeterminismMode);
    core_section->Get("Overclock", &StartUp.m_OCFactor, StartUp.m_OCFactor);
    core_section->Get("OverclockEnable", &StartUp.m_OCEnable, StartUp.m_OCEnable);

    for (unsigned int i = 0; i < SerialInterface::MAX_SI_CHANNELS; ++i)
    {
      int source;
      controls_section->Get(fmt::format("PadType{}", i), &source, -1);
      if (source >= SerialInterface::SIDEVICE_NONE && source < SerialInterface::SIDEVICE_COUNT)
      {
        StartUp.m_SIDevice[i] = static_cast<SerialInterface::SIDevices>(source);
        config_cache.bSetPads[i] = true;
      }
    }

    // Wii settings
    if (StartUp.bWii)
    {
      int source;
      for (unsigned int i = 0; i < MAX_WIIMOTES; ++i)
      {
        controls_section->Get(fmt::format("WiimoteSource{}", i), &source, -1);
        if (source != -1 && WiimoteCommon::GetSource(i) != WiimoteSource(source) &&
            WiimoteSource(source) >= WiimoteSource::None &&
            WiimoteSource(source) <= WiimoteSource::Real)
        {
          config_cache.bSetWiimoteSource[i] = true;
          WiimoteCommon::SetSource(i, WiimoteSource(source));
        }
      }
      controls_section->Get("WiimoteSourceBB", &source, -1);
      if (source != -1 &&
          WiimoteCommon::GetSource(WIIMOTE_BALANCE_BOARD) != WiimoteSource(source) &&
          (WiimoteSource(source) == WiimoteSource::None ||
           WiimoteSource(source) == WiimoteSource::Real))
      {
        config_cache.bSetWiimoteSource[WIIMOTE_BALANCE_BOARD] = true;
        WiimoteCommon::SetSource(WIIMOTE_BALANCE_BOARD, WiimoteSource(source));
      }
    }
  }

  StartUp.m_GPUDeterminismMode = ParseGPUDeterminismMode(StartUp.m_strGPUDeterminismMode);

  // Movie settings
  if (Movie::IsPlayingInput() && Movie::IsConfigSaved())
  {
    // TODO: remove this once ConfigManager starts using OnionConfig.
    StartUp.bCPUThread = Config::Get(Config::MAIN_CPU_THREAD);
    StartUp.bJITFollowBranch = Config::Get(Config::MAIN_JIT_FOLLOW_BRANCH);
    StartUp.bDSPHLE = Config::Get(Config::MAIN_DSP_HLE);
    StartUp.bFastDiscSpeed = Config::Get(Config::MAIN_FAST_DISC_SPEED);
    StartUp.cpu_core = Config::Get(Config::MAIN_CPU_CORE);
    StartUp.bSyncGPU = Config::Get(Config::MAIN_SYNC_GPU);
    if (!StartUp.bWii)
      StartUp.SelectedLanguage = Config::Get(Config::MAIN_GC_LANGUAGE);
    for (int i = 0; i < 2; ++i)
    {
      if (Movie::IsUsingMemcard(i) && Movie::IsStartingFromClearSave() && !StartUp.bWii)
      {
        const auto raw_path =
            File::GetUserPath(D_GCUSER_IDX) + fmt::format("Movie{}.raw", (i == 0) ? 'A' : 'B');
        if (File::Exists(raw_path))
          File::Delete(raw_path);

        const auto movie_path = File::GetUserPath(D_GCUSER_IDX) + "Movie";
        if (File::Exists(movie_path))
          File::DeleteDirRecursively(movie_path);
      }
    }
  }

  if (NetPlay::IsNetPlayRunning())
  {
    const NetPlay::NetSettings& netplay_settings = NetPlay::GetNetSettings();
    Config::AddLayer(ConfigLoaders::GenerateNetPlayConfigLoader(netplay_settings));
    StartUp.bCPUThread = netplay_settings.m_CPUthread;
    StartUp.bEnableCheats = netplay_settings.m_EnableCheats;
    StartUp.bDSPHLE = netplay_settings.m_DSPHLE;
    StartUp.bEnableMemcardSdWriting = netplay_settings.m_WriteToMemcard;
    StartUp.bCopyWiiSaveNetplay = netplay_settings.m_CopyWiiSave;
    StartUp.cpu_core = netplay_settings.m_CPUcore;
    StartUp.SelectedLanguage = netplay_settings.m_SelectedLanguage;
    StartUp.bOverrideRegionSettings = netplay_settings.m_OverrideRegionSettings;
    StartUp.m_DSPEnableJIT = netplay_settings.m_DSPEnableJIT;
    StartUp.m_OCEnable = netplay_settings.m_OCEnable;
    StartUp.m_OCFactor = netplay_settings.m_OCFactor;
    StartUp.m_EXIDevice[0] = netplay_settings.m_EXIDevice[0];
    StartUp.m_EXIDevice[1] = netplay_settings.m_EXIDevice[1];
    StartUp.m_EXIDevice[2] = netplay_settings.m_EXIDevice[2];
    config_cache.bSetEXIDevice[0] = true;
    config_cache.bSetEXIDevice[1] = true;
    config_cache.bSetEXIDevice[2] = true;
    StartUp.bFPRF = netplay_settings.m_FPRF;
    StartUp.bAccurateNaNs = netplay_settings.m_AccurateNaNs;
    StartUp.bDisableICache = netplay_settings.m_DisableICache;
    StartUp.bSyncGPUOnSkipIdleHack = netplay_settings.m_SyncOnSkipIdle;
    StartUp.bSyncGPU = netplay_settings.m_SyncGPU;
    StartUp.iSyncGpuMaxDistance = netplay_settings.m_SyncGpuMaxDistance;
    StartUp.iSyncGpuMinDistance = netplay_settings.m_SyncGpuMinDistance;
    StartUp.fSyncGpuOverclock = netplay_settings.m_SyncGpuOverclock;
    StartUp.bJITFollowBranch = netplay_settings.m_JITFollowBranch;
    StartUp.bFastDiscSpeed = netplay_settings.m_FastDiscSpeed;
    StartUp.bMMU = netplay_settings.m_MMU;
    StartUp.bFastmem = netplay_settings.m_Fastmem;
    StartUp.bHLE_BS2 = netplay_settings.m_SkipIPL;
    StartUp.m_bt_passthrough_enabled = false;
    if (netplay_settings.m_HostInputAuthority && !netplay_settings.m_IsHosting)
      config_cache.bSetEmulationSpeed = true;
  }
  else
  {
    g_SRAM_netplay_initialized = false;
  }

  // Override out-of-region languages/countries to prevent games from crashing or behaving oddly
  if (!StartUp.bOverrideRegionSettings)
  {
    StartUp.SelectedLanguage =
        DiscIO::ToGameCubeLanguage(StartUp.GetLanguageAdjustedForRegion(false, StartUp.m_region));

    if (StartUp.bWii)
    {
      const u32 wii_language =
          static_cast<u32>(StartUp.GetLanguageAdjustedForRegion(true, StartUp.m_region));
      if (wii_language != Config::Get(Config::SYSCONF_LANGUAGE))
        Config::SetCurrent(Config::SYSCONF_LANGUAGE, wii_language);

      const u8 country_code = static_cast<u8>(Config::Get(Config::SYSCONF_COUNTRY));
      if (StartUp.m_region != DiscIO::SysConfCountryToRegion(country_code))
      {
        switch (StartUp.m_region)
        {
        case DiscIO::Region::NTSC_J:
          Config::SetCurrent(Config::SYSCONF_COUNTRY, 0x01);  // Japan
          break;
        case DiscIO::Region::NTSC_U:
          Config::SetCurrent(Config::SYSCONF_COUNTRY, 0x31);  // United States
          break;
        case DiscIO::Region::PAL:
          Config::SetCurrent(Config::SYSCONF_COUNTRY, 0x6c);  // Switzerland
          break;
        case DiscIO::Region::NTSC_K:
          Config::SetCurrent(Config::SYSCONF_COUNTRY, 0x88);  // South Korea
          break;
        case DiscIO::Region::Unknown:
          break;
        }
      }
    }
  }

  // Some NTSC Wii games such as Doc Louis's Punch-Out!! and
  // 1942 (Virtual Console) crash if the PAL60 option is enabled
  if (StartUp.bWii && DiscIO::IsNTSC(StartUp.m_region) && Config::Get(Config::SYSCONF_PAL60))
    Config::SetCurrent(Config::SYSCONF_PAL60, false);

  Core::UpdateWantDeterminism(/*initial*/ true);

  if (StartUp.bWii)
  {
    Core::InitializeWiiRoot(Core::WantsDeterminism());

    // Ensure any new settings are written to the SYSCONF
    if (!Core::WantsDeterminism())
    {
      Core::BackupWiiSettings();
      ConfigLoaders::SaveToSYSCONF(Config::LayerType::Meta);
    }
    else
    {
      ConfigLoaders::SaveToSYSCONF(Config::LayerType::Meta, [](const Config::Location& location) {
        return Config::GetActiveLayerForConfig(location) >= Config::LayerType::Movie;
      });
    }
  }

  const bool load_ipl = !StartUp.bWii && !StartUp.bHLE_BS2 &&
                        std::holds_alternative<BootParameters::Disc>(boot->parameters);
  if (load_ipl)
  {
    return Core::Init(
        std::make_unique<BootParameters>(
            BootParameters::IPL{StartUp.m_region,
                                std::move(std::get<BootParameters::Disc>(boot->parameters))},
            boot->savestate_path),
        wsi);
  }
  return Core::Init(std::move(boot), wsi);
}

// SYSCONF can be modified during emulation by the user and internally, which makes it
// a bad idea to just always overwrite it with the settings from the base layer.
//
// Conversely, we also shouldn't just accept any changes to SYSCONF, as it may cause
// temporary settings (from Movie, Netplay, game INIs, etc.) to stick around.
//
// To avoid inconveniences in most cases, we accept changes that aren't being overriden by a
// non-base layer, and restore only the overriden settings.
static void RestoreSYSCONF()
{
  // This layer contains the new SYSCONF settings (including any temporary settings).
  Config::Layer temp_layer(Config::LayerType::Base);
  // Use a separate loader so the temp layer doesn't automatically save
  ConfigLoaders::GenerateBaseConfigLoader()->Load(&temp_layer);

  for (const auto& setting : Config::SYSCONF_SETTINGS)
  {
    std::visit(
        [&](auto* info) {
          // If this setting was overridden, then we copy the base layer value back to the SYSCONF.
          // Otherwise we leave the new value in the SYSCONF.
          if (Config::GetActiveLayerForConfig(*info) == Config::LayerType::Base)
            Config::SetBase(*info, temp_layer.Get(*info));
        },
        setting.config_info);
  }
  // Save the SYSCONF.
  Config::GetLayer(Config::LayerType::Base)->Save();
}

void RestoreConfig()
{
  Core::ShutdownWiiRoot();

  if (!Core::WiiRootIsTemporary())
  {
    Core::RestoreWiiSettings(Core::RestoreReason::EmulationEnd);
    RestoreSYSCONF();
  }

  Config::ClearCurrentRunLayer();
  Config::RemoveLayer(Config::LayerType::Movie);
  Config::RemoveLayer(Config::LayerType::Netplay);
  Config::RemoveLayer(Config::LayerType::GlobalGame);
  Config::RemoveLayer(Config::LayerType::LocalGame);
  SConfig::GetInstance().ResetRunningGameMetadata();
  config_cache.RestoreConfig(&SConfig::GetInstance());
}

}  // namespace BootManager
