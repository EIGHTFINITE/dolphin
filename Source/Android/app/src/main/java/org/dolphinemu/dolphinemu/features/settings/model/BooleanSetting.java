package org.dolphinemu.dolphinemu.features.settings.model;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public enum BooleanSetting implements AbstractBooleanSetting
{
  // These entries have the same names and order as in C++, just for consistency.

  MAIN_DSP_HLE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "DSPHLE", true),
  MAIN_FASTMEM(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "Fastmem", true),
  MAIN_CPU_THREAD(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "CPUThread", true),
  MAIN_SYNC_ON_SKIP_IDLE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "SyncOnSkipIdle", true),
  MAIN_OVERRIDE_REGION_SETTINGS(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE,
          "OverrideRegionSettings", false),
  MAIN_AUDIO_STRETCH(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "AudioStretch", false),
  MAIN_WII_SD_CARD(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "WiiSDCard", true),
  MAIN_WIIMOTE_CONTINUOUS_SCANNING(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE,
          "WiimoteContinuousScanning", false),
  MAIN_WIIMOTE_ENABLE_SPEAKER(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE,
          "WiimoteEnableSpeaker", false),
  MAIN_SYNC_GPU(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "SyncGPU", false),
  MAIN_OVERCLOCK_ENABLE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "OverclockEnable", false),
  MAIN_AUTO_DISC_CHANGE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "AutoDiscChange", false),
  MAIN_ALLOW_SD_WRITES(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "WiiSDCardAllowWrites",
          true),
  MAIN_ENABLE_SAVESTATES(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "EnableSaveStates",
          false),

  MAIN_DSP_JIT(Settings.FILE_DOLPHIN, Settings.SECTION_INI_DSP, "EnableJIT", true),

  MAIN_USE_PANIC_HANDLERS(Settings.FILE_DOLPHIN, Settings.SECTION_INI_INTERFACE,
          "UsePanicHandlers", true),
  MAIN_OSD_MESSAGES(Settings.FILE_DOLPHIN, Settings.SECTION_INI_INTERFACE,
          "OnScreenDisplayMessages", true),

  MAIN_ANALYTICS_ENABLED(Settings.FILE_DOLPHIN, Settings.SECTION_ANALYTICS, "Enabled", false),
  MAIN_ANALYTICS_PERMISSION_ASKED(Settings.FILE_DOLPHIN, Settings.SECTION_ANALYTICS,
          "PermissionAsked", false),

  MAIN_RECURSIVE_ISO_PATHS(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL,
          "RecursiveISOPaths", false),
  MAIN_USE_GAME_COVERS(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL,
          "UseGameCovers", true),

  MAIN_JOYSTICK_REL_CENTER(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID,
          "JoystickRelCenter", true),
  MAIN_PHONE_RUMBLE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID,
          "PhoneRumble", true),
  MAIN_SHOW_INPUT_OVERLAY(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID,
          "ShowInputOverlay", true),

  MAIN_BUTTON_TOGGLE_GC_0(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCButtonA", true),
  MAIN_BUTTON_TOGGLE_GC_1(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCButtonB", true),
  MAIN_BUTTON_TOGGLE_GC_2(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCButtonX", true),
  MAIN_BUTTON_TOGGLE_GC_3(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCButtonY", true),
  MAIN_BUTTON_TOGGLE_GC_4(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCButtonZ", true),
  MAIN_BUTTON_TOGGLE_GC_5(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCButtonStart", true),
  MAIN_BUTTON_TOGGLE_GC_6(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCTriggerL", true),
  MAIN_BUTTON_TOGGLE_GC_7(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCTriggerR", true),
  MAIN_BUTTON_TOGGLE_GC_8(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCDPad", true),
  MAIN_BUTTON_TOGGLE_GC_9(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCStickMain", true),
  MAIN_BUTTON_TOGGLE_GC_10(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleGCStickC", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_0(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonA", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_1(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonB", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_2(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonX", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_3(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonY", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_4(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonPlus", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_5(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonMinus", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_6(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonHome", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_7(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicTriggerL", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_8(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicTriggerR", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_9(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonZL", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_10(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicButtonZR", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_11(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicDPad", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_12(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicStickLeft", true),
  MAIN_BUTTON_TOGGLE_CLASSIC_13(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleClassicStickRight", true),
  MAIN_BUTTON_TOGGLE_WII_0(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButtonA", true),
  MAIN_BUTTON_TOGGLE_WII_1(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButtonB", true),
  MAIN_BUTTON_TOGGLE_WII_2(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButton1", true),
  MAIN_BUTTON_TOGGLE_WII_3(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButton2", true),
  MAIN_BUTTON_TOGGLE_WII_4(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButtonPlus", true),
  MAIN_BUTTON_TOGGLE_WII_5(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButtonMinus", true),
  MAIN_BUTTON_TOGGLE_WII_6(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteButtonHome", true),
  MAIN_BUTTON_TOGGLE_WII_7(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleWiimoteDPad", true),
  MAIN_BUTTON_TOGGLE_WII_8(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleNunchukC", true),
  MAIN_BUTTON_TOGGLE_WII_9(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleNunchukZ", true),
  MAIN_BUTTON_TOGGLE_WII_10(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "ButtonToggleNunchukStick", true),

  SYSCONF_SCREENSAVER(Settings.FILE_SYSCONF, "IPL", "SSV", false),
  SYSCONF_WIDESCREEN(Settings.FILE_SYSCONF, "IPL", "AR", true),
  SYSCONF_PAL60(Settings.FILE_SYSCONF, "IPL", "E60", true),

  SYSCONF_WIIMOTE_MOTOR(Settings.FILE_SYSCONF, "BT", "MOT", true),

  GFX_WIDESCREEN_HACK(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "wideScreenHack", false),
  GFX_SHOW_FPS(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "ShowFPS", false),
  GFX_ENABLE_GPU_TEXTURE_DECODING(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "EnableGPUTextureDecoding", false),
  GFX_ENABLE_PIXEL_LIGHTING(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "EnablePixelLighting", false),
  GFX_FAST_DEPTH_CALC(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "FastDepthCalc", true),
  GFX_DISABLE_FOG(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "DisableFog", false),
  GFX_BACKEND_MULTITHREADING(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "BackendMultithreading", false),
  GFX_WAIT_FOR_SHADERS_BEFORE_STARTING(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "WaitForShadersBeforeStarting", false),
  GFX_SAVE_TEXTURE_CACHE_TO_STATE(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "SaveTextureCacheToState", true),

  GFX_ENHANCE_FORCE_FILTERING(Settings.FILE_GFX, Settings.SECTION_GFX_ENHANCEMENTS,
          "ForceFiltering", false),
  GFX_ENHANCE_FORCE_TRUE_COLOR(Settings.FILE_GFX, Settings.SECTION_GFX_ENHANCEMENTS,
          "ForceTrueColor", true),
  GFX_ENHANCE_DISABLE_COPY_FILTER(Settings.FILE_GFX, Settings.SECTION_GFX_ENHANCEMENTS,
          "DisableCopyFilter", true),
  GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION(Settings.FILE_GFX, Settings.SECTION_GFX_ENHANCEMENTS,
          "ArbitraryMipmapDetection", true),

  GFX_STEREO_SWAP_EYES(Settings.FILE_GFX, Settings.SECTION_STEREOSCOPY, "StereoSwapEyes", false),

  GFX_HACK_EFB_ACCESS_ENABLE(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "EFBAccessEnable",
          true),
  GFX_HACK_BBOX_ENABLE(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "BBoxEnable", false),
  GFX_HACK_SKIP_EFB_COPY_TO_RAM(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS,
          "EFBToTextureEnable", true),
  GFX_HACK_SKIP_XFB_COPY_TO_RAM(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS,
          "XFBToTextureEnable", true),
  GFX_HACK_DEFER_EFB_COPIES(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "DeferEFBCopies", true),
  GFX_HACK_IMMEDIATE_XFB(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "ImmediateXFBEnable",
          false),
  GFX_HACK_SKIP_DUPLICATE_XFBS(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "SkipDuplicateXFBs",
          true),
  GFX_HACK_COPY_EFB_SCALED(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "EFBScaledCopy", true),
  GFX_HACK_EFB_EMULATE_FORMAT_CHANGES(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS,
          "EFBEmulateFormatChanges", false),
  GFX_HACK_VERTEX_ROUDING(Settings.FILE_GFX, Settings.SECTION_GFX_HACKS, "VertexRounding", false),

  LOGGER_WRITE_TO_FILE(Settings.FILE_LOGGER, Settings.SECTION_LOGGER_OPTIONS, "WriteToFile", false),

  // These settings are not yet in the new config system in C++ - please move them once they are

  MAIN_JIT_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitOff", false),
  MAIN_JIT_LOAD_STORE_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitLoadStoreOff", false),
  MAIN_JIT_LOAD_STORE_FLOATING_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG,
          "JitLoadStoreFloatingOff", false),
  MAIN_JIT_LOAD_STORE_PAIRED_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG,
          "JitLoadStorePairedOff", false),
  MAIN_JIT_FLOATING_POINT_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitFloatingPointOff",
          false),
  MAIN_JIT_INTEGER_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitIntegerOff", false),
  MAIN_JIT_PAIRED_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitPairedOff", false),
  MAIN_JIT_SYSTEM_REGISTERS_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG,
          "JitSystemRegistersOff", false),
  MAIN_JIT_BRANCH_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitBranchOff", false),
  MAIN_JIT_REGISTER_CACHE_OFF(Settings.FILE_DOLPHIN, Settings.SECTION_DEBUG, "JitRegisterCacheOff",
          false);

  private static final BooleanSetting[] NOT_RUNTIME_EDITABLE_ARRAY = new BooleanSetting[]{
          MAIN_DSP_HLE,
          MAIN_CPU_THREAD,
          MAIN_OVERRIDE_REGION_SETTINGS,
          MAIN_WII_SD_CARD,  // Can actually be changed, but specific code is required
          MAIN_DSP_JIT
  };

  private static final Set<BooleanSetting> NOT_RUNTIME_EDITABLE =
          new HashSet<>(Arrays.asList(NOT_RUNTIME_EDITABLE_ARRAY));

  private final String mFile;
  private final String mSection;
  private final String mKey;
  private final boolean mDefaultValue;

  BooleanSetting(String file, String section, String key, boolean defaultValue)
  {
    mFile = file;
    mSection = section;
    mKey = key;
    mDefaultValue = defaultValue;
  }

  @Override
  public boolean isOverridden(Settings settings)
  {
    if (settings.isGameSpecific() && !NativeConfig.isSettingSaveable(mFile, mSection, mKey))
      return settings.getSection(mFile, mSection).exists(mKey);
    else
      return NativeConfig.isOverridden(mFile, mSection, mKey);
  }

  @Override
  public boolean isRuntimeEditable()
  {
    if (mFile.equals(Settings.FILE_SYSCONF))
      return false;

    for (BooleanSetting setting : NOT_RUNTIME_EDITABLE)
    {
      if (setting == this)
        return false;
    }

    return NativeConfig.isSettingSaveable(mFile, mSection, mKey);
  }

  @Override
  public boolean delete(Settings settings)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      return NativeConfig.deleteKey(settings.getWriteLayer(), mFile, mSection, mKey);
    }
    else
    {
      return settings.getSection(mFile, mSection).delete(mKey);
    }
  }

  @Override
  public boolean getBoolean(Settings settings)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      return NativeConfig.getBoolean(NativeConfig.LAYER_ACTIVE, mFile, mSection, mKey,
              mDefaultValue);
    }
    else
    {
      return settings.getSection(mFile, mSection).getBoolean(mKey, mDefaultValue);
    }
  }

  @Override
  public void setBoolean(Settings settings, boolean newValue)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      NativeConfig.setBoolean(settings.getWriteLayer(), mFile, mSection, mKey, newValue);
    }
    else
    {
      settings.getSection(mFile, mSection).setBoolean(mKey, newValue);
    }
  }

  public boolean getBooleanGlobal()
  {
    return NativeConfig.getBoolean(NativeConfig.LAYER_ACTIVE, mFile, mSection, mKey, mDefaultValue);
  }

  public void setBooleanGlobal(int layer, boolean newValue)
  {
    NativeConfig.setBoolean(layer, mFile, mSection, mKey, newValue);
  }
}
