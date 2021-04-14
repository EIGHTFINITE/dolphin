// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/ConfigLoaders/NetPlayConfigLoader.h"

#include <memory>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/NetPlayProto.h"

namespace ConfigLoaders
{
class NetPlayConfigLayerLoader final : public Config::ConfigLayerLoader
{
public:
  explicit NetPlayConfigLayerLoader(const NetPlay::NetSettings& settings)
      : ConfigLayerLoader(Config::LayerType::Netplay), m_settings(settings)
  {
  }

  void Load(Config::Layer* layer) override
  {
    layer->Set(Config::MAIN_CPU_THREAD, m_settings.m_CPUthread);
    layer->Set(Config::MAIN_CPU_CORE, m_settings.m_CPUcore);
    layer->Set(Config::MAIN_GC_LANGUAGE, m_settings.m_SelectedLanguage);
    layer->Set(Config::MAIN_OVERRIDE_REGION_SETTINGS, m_settings.m_OverrideRegionSettings);
    layer->Set(Config::MAIN_DSP_HLE, m_settings.m_DSPHLE);
    layer->Set(Config::MAIN_OVERCLOCK_ENABLE, m_settings.m_OCEnable);
    layer->Set(Config::MAIN_OVERCLOCK, m_settings.m_OCFactor);
    layer->Set(Config::MAIN_SLOT_A, static_cast<int>(m_settings.m_EXIDevice[0]));
    layer->Set(Config::MAIN_SLOT_B, static_cast<int>(m_settings.m_EXIDevice[1]));
    layer->Set(Config::MAIN_SERIAL_PORT_1, static_cast<int>(m_settings.m_EXIDevice[2]));
    layer->Set(Config::MAIN_WII_SD_CARD_WRITABLE, m_settings.m_WriteToMemcard);
    layer->Set(Config::MAIN_MEM1_SIZE, m_settings.m_Mem1Size);
    layer->Set(Config::MAIN_MEM2_SIZE, m_settings.m_Mem2Size);
    layer->Set(Config::MAIN_FALLBACK_REGION, m_settings.m_FallbackRegion);
    layer->Set(Config::MAIN_DSP_JIT, m_settings.m_DSPEnableJIT);

    for (size_t i = 0; i < Config::SYSCONF_SETTINGS.size(); ++i)
    {
      std::visit(
          [&](auto* info) {
            layer->Set(*info, static_cast<decltype(info->GetDefaultValue())>(
                                  m_settings.m_SYSCONFSettings[i]));
          },
          Config::SYSCONF_SETTINGS[i].config_info);
    }

    layer->Set(Config::GFX_HACK_EFB_ACCESS_ENABLE, m_settings.m_EFBAccessEnable);
    layer->Set(Config::GFX_HACK_BBOX_ENABLE, m_settings.m_BBoxEnable);
    layer->Set(Config::GFX_HACK_FORCE_PROGRESSIVE, m_settings.m_ForceProgressive);
    layer->Set(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM, m_settings.m_EFBToTextureEnable);
    layer->Set(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM, m_settings.m_XFBToTextureEnable);
    layer->Set(Config::GFX_HACK_DISABLE_COPY_TO_VRAM, m_settings.m_DisableCopyToVRAM);
    layer->Set(Config::GFX_HACK_IMMEDIATE_XFB, m_settings.m_ImmediateXFBEnable);
    layer->Set(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES, m_settings.m_EFBEmulateFormatChanges);
    layer->Set(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES,
               m_settings.m_SafeTextureCacheColorSamples);
    layer->Set(Config::GFX_PERF_QUERIES_ENABLE, m_settings.m_PerfQueriesEnable);
    layer->Set(Config::MAIN_FPRF, m_settings.m_FPRF);
    layer->Set(Config::MAIN_ACCURATE_NANS, m_settings.m_AccurateNaNs);
    layer->Set(Config::MAIN_DISABLE_ICACHE, m_settings.m_DisableICache);
    layer->Set(Config::MAIN_SYNC_ON_SKIP_IDLE, m_settings.m_SyncOnSkipIdle);
    layer->Set(Config::MAIN_SYNC_GPU, m_settings.m_SyncGPU);
    layer->Set(Config::MAIN_SYNC_GPU_MAX_DISTANCE, m_settings.m_SyncGpuMaxDistance);
    layer->Set(Config::MAIN_SYNC_GPU_MIN_DISTANCE, m_settings.m_SyncGpuMinDistance);
    layer->Set(Config::MAIN_SYNC_GPU_OVERCLOCK, m_settings.m_SyncGpuOverclock);

    layer->Set(Config::MAIN_JIT_FOLLOW_BRANCH, m_settings.m_JITFollowBranch);
    layer->Set(Config::MAIN_FAST_DISC_SPEED, m_settings.m_FastDiscSpeed);
    layer->Set(Config::MAIN_MMU, m_settings.m_MMU);
    layer->Set(Config::MAIN_FASTMEM, m_settings.m_Fastmem);
    layer->Set(Config::MAIN_SKIP_IPL, m_settings.m_SkipIPL);
    layer->Set(Config::MAIN_LOAD_IPL_DUMP, m_settings.m_LoadIPLDump);

    layer->Set(Config::GFX_HACK_DEFER_EFB_COPIES, m_settings.m_DeferEFBCopies);
    layer->Set(Config::GFX_HACK_EFB_ACCESS_TILE_SIZE, m_settings.m_EFBAccessTileSize);
    layer->Set(Config::GFX_HACK_EFB_DEFER_INVALIDATION, m_settings.m_EFBAccessDeferInvalidation);

    if (m_settings.m_StrictSettingsSync)
    {
      layer->Set(Config::GFX_HACK_VERTEX_ROUDING, m_settings.m_VertexRounding);
      layer->Set(Config::GFX_EFB_SCALE, m_settings.m_InternalResolution);
      layer->Set(Config::GFX_HACK_COPY_EFB_SCALED, m_settings.m_EFBScaledCopy);
      layer->Set(Config::GFX_FAST_DEPTH_CALC, m_settings.m_FastDepthCalc);
      layer->Set(Config::GFX_ENABLE_PIXEL_LIGHTING, m_settings.m_EnablePixelLighting);
      layer->Set(Config::GFX_WIDESCREEN_HACK, m_settings.m_WidescreenHack);
      layer->Set(Config::GFX_ENHANCE_FORCE_FILTERING, m_settings.m_ForceFiltering);
      layer->Set(Config::GFX_ENHANCE_MAX_ANISOTROPY, m_settings.m_MaxAnisotropy);
      layer->Set(Config::GFX_ENHANCE_FORCE_TRUE_COLOR, m_settings.m_ForceTrueColor);
      layer->Set(Config::GFX_ENHANCE_DISABLE_COPY_FILTER, m_settings.m_DisableCopyFilter);
      layer->Set(Config::GFX_DISABLE_FOG, m_settings.m_DisableFog);
      layer->Set(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION,
                 m_settings.m_ArbitraryMipmapDetection);
      layer->Set(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD,
                 m_settings.m_ArbitraryMipmapDetectionThreshold);
      layer->Set(Config::GFX_ENABLE_GPU_TEXTURE_DECODING, m_settings.m_EnableGPUTextureDecoding);

      // Disable AA as it isn't deterministic across GPUs
      layer->Set(Config::GFX_MSAA, 1);
      layer->Set(Config::GFX_SSAA, false);
    }

    if (m_settings.m_SyncSaveData)
    {
      if (!m_settings.m_IsHosting)
      {
        const std::string path = File::GetUserPath(D_GCUSER_IDX) + GC_MEMCARD_NETPLAY DIR_SEP;
        layer->Set(Config::MAIN_GCI_FOLDER_A_PATH_OVERRIDE, path + "Card A");
        layer->Set(Config::MAIN_GCI_FOLDER_B_PATH_OVERRIDE, path + "Card B");

        const auto make_memcard_path = [this](char letter) {
          return fmt::format("{}{}{}.{}.raw", File::GetUserPath(D_GCUSER_IDX), GC_MEMCARD_NETPLAY,
                             letter, m_settings.m_SaveDataRegion);
        };
        layer->Set(Config::MAIN_MEMCARD_A_PATH, make_memcard_path('A'));
        layer->Set(Config::MAIN_MEMCARD_B_PATH, make_memcard_path('B'));
      }

      layer->Set(Config::MAIN_GCI_FOLDER_CURRENT_GAME_ONLY, true);
    }

    // Check To Override Client's Cheat Codes
    if (m_settings.m_SyncCodes && !m_settings.m_IsHosting)
    {
      // Raise flag to use host's codes
      layer->Set(Config::MAIN_CODE_SYNC_OVERRIDE, true);
    }
  }

  void Save(Config::Layer* layer) override
  {
    // Do Nothing
  }

private:
  const NetPlay::NetSettings m_settings;
};

// Loader generation
std::unique_ptr<Config::ConfigLayerLoader>
GenerateNetPlayConfigLoader(const NetPlay::NetSettings& settings)
{
  return std::make_unique<NetPlayConfigLayerLoader>(settings);
}
}  // namespace ConfigLoaders
