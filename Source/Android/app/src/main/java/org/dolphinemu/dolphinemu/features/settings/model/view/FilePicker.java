package org.dolphinemu.dolphinemu.features.settings.model.view;

import org.dolphinemu.dolphinemu.features.settings.model.AbstractSetting;
import org.dolphinemu.dolphinemu.features.settings.model.AbstractStringSetting;
import org.dolphinemu.dolphinemu.features.settings.model.Settings;

import androidx.annotation.Nullable;

public final class FilePicker extends SettingsItem
{
  private AbstractStringSetting mSetting;
  private int mRequestType;
  private String mDefaultPathRelativeToUserDirectory;

  public FilePicker(AbstractStringSetting setting, int titleId, int descriptionId, int requestType,
          @Nullable String defaultPathRelativeToUserDirectory)
  {
    super(titleId, descriptionId);
    mSetting = setting;
    mRequestType = requestType;
    mDefaultPathRelativeToUserDirectory = defaultPathRelativeToUserDirectory;
  }

  public String getSelectedValue(Settings settings)
  {
    return mSetting.getString(settings);
  }

  public void setSelectedValue(Settings settings, String selection)
  {
    mSetting.setString(settings, selection);
  }

  public int getRequestType()
  {
    return mRequestType;
  }

  @Nullable
  public String getDefaultPathRelativeToUserDirectory()
  {
    return mDefaultPathRelativeToUserDirectory;
  }

  @Override
  public int getType()
  {
    return TYPE_FILE_PICKER;
  }

  @Override
  public AbstractSetting getSetting()
  {
    return mSetting;
  }
}
