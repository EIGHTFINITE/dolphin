package org.dolphinemu.dolphinemu.features.settings.model;

import android.content.pm.ActivityInfo;

import org.dolphinemu.dolphinemu.NativeLibrary;
import org.dolphinemu.dolphinemu.overlay.InputOverlayPointer;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public enum IntSetting implements AbstractIntSetting
{
  // These entries have the same names and order as in C++, just for consistency.

  MAIN_CPU_CORE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "CPUCore",
          NativeLibrary.DefaultCPUCore()),
  MAIN_GC_LANGUAGE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "SelectedLanguage", 0),
  MAIN_SLOT_A(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "SlotA", 8),
  MAIN_SLOT_B(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "SlotB", 255),

  MAIN_AUDIO_VOLUME(Settings.FILE_DOLPHIN, Settings.SECTION_INI_DSP, "Volume", 100),

  MAIN_CONTROL_SCALE(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID, "ControlScale", 50),
  MAIN_CONTROL_OPACITY(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID, "ControlOpacity", 50),
  MAIN_EMULATION_ORIENTATION(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID,
          "EmulationOrientation", ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE),
  MAIN_LAST_PLATFORM_TAB(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID, "LastPlatformTab", 0),
  MAIN_MOTION_CONTROLS(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID, "MotionControls", 1),

  MAIN_DOUBLE_TAP_BUTTON(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID_OVERLAY_BUTTONS,
          "DoubleTapButton",
          InputOverlayPointer.DOUBLE_TAP_OPTIONS.get(InputOverlayPointer.DOUBLE_TAP_A)),

  SYSCONF_LANGUAGE(Settings.FILE_SYSCONF, "IPL", "LNG", 0x01),
  SYSCONF_SOUND_MODE(Settings.FILE_SYSCONF, "IPL", "SND", 0x01),

  SYSCONF_SENSOR_BAR_POSITION(Settings.FILE_SYSCONF, "BT", "BAR", 0x01),
  SYSCONF_SENSOR_BAR_SENSITIVITY(Settings.FILE_SYSCONF, "BT", "SENS", 0x03),
  SYSCONF_SPEAKER_VOLUME(Settings.FILE_SYSCONF, "BT", "SPKV", 0x58),

  GFX_ASPECT_RATIO(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "AspectRatio", 0),
  GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "SafeTextureCacheColorSamples", 128),
  GFX_MSAA(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "MSAA", 1),
  GFX_EFB_SCALE(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS, "InternalResolution", 1),
  GFX_SHADER_COMPILATION_MODE(Settings.FILE_GFX, Settings.SECTION_GFX_SETTINGS,
          "ShaderCompilationMode", 0),

  GFX_ENHANCE_MAX_ANISOTROPY(Settings.FILE_GFX, Settings.SECTION_GFX_ENHANCEMENTS, "MaxAnisotropy",
          0),

  GFX_STEREO_MODE(Settings.FILE_GFX, Settings.SECTION_STEREOSCOPY, "StereoMode", 0),
  GFX_STEREO_DEPTH(Settings.FILE_GFX, Settings.SECTION_STEREOSCOPY, "StereoDepth", 20),
  GFX_STEREO_CONVERGENCE_PERCENTAGE(Settings.FILE_GFX, Settings.SECTION_STEREOSCOPY,
          "StereoConvergencePercentage", 100),

  LOGGER_VERBOSITY(Settings.FILE_LOGGER, Settings.SECTION_LOGGER_OPTIONS, "Verbosity", 1);

  private static final IntSetting[] NOT_RUNTIME_EDITABLE_ARRAY = new IntSetting[]{
          MAIN_CPU_CORE,
          MAIN_GC_LANGUAGE,
          MAIN_SLOT_A,  // Can actually be changed, but specific code is required
          MAIN_SLOT_B,  // Can actually be changed, but specific code is required
  };

  private static final Set<IntSetting> NOT_RUNTIME_EDITABLE =
          new HashSet<>(Arrays.asList(NOT_RUNTIME_EDITABLE_ARRAY));

  private final String mFile;
  private final String mSection;
  private final String mKey;
  private final int mDefaultValue;

  IntSetting(String file, String section, String key, int defaultValue)
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

    for (IntSetting setting : NOT_RUNTIME_EDITABLE)
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
  public int getInt(Settings settings)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      return NativeConfig.getInt(NativeConfig.LAYER_ACTIVE, mFile, mSection, mKey, mDefaultValue);
    }
    else
    {
      return settings.getSection(mFile, mSection).getInt(mKey, mDefaultValue);
    }
  }

  @Override
  public void setInt(Settings settings, int newValue)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      NativeConfig.setInt(settings.getWriteLayer(), mFile, mSection, mKey, newValue);
    }
    else
    {
      settings.getSection(mFile, mSection).setInt(mKey, newValue);
    }
  }

  public int getIntGlobal()
  {
    return NativeConfig.getInt(NativeConfig.LAYER_ACTIVE, mFile, mSection, mKey, mDefaultValue);
  }

  public void setIntGlobal(int layer, int newValue)
  {
    NativeConfig.setInt(layer, mFile, mSection, mKey, newValue);
  }
}
