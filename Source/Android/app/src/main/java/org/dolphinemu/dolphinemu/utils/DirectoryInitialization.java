/*
 * Copyright 2014 Dolphin Emulator Project
 * Licensed under GPLv2+
 * Refer to the license.txt file included.
 */

package org.dolphinemu.dolphinemu.utils;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Environment;
import android.preference.PreferenceManager;

import androidx.annotation.NonNull;
import androidx.localbroadcastmanager.content.LocalBroadcastManager;

import org.dolphinemu.dolphinemu.NativeLibrary;
import org.dolphinemu.dolphinemu.activities.EmulationActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A service that spawns its own thread in order to copy several binary and shader files
 * from the Dolphin APK to the external file system.
 */
public final class DirectoryInitialization
{
  public static final String BROADCAST_ACTION =
          "org.dolphinemu.dolphinemu.DIRECTORY_INITIALIZATION";

  public static final String EXTRA_STATE = "directoryState";
  private static final int WiimoteNewVersion = 5;  // Last changed in PR 8907
  private static volatile DirectoryInitializationState directoryState =
          DirectoryInitializationState.NOT_YET_INITIALIZED;
  private static volatile boolean areDirectoriesAvailable = false;
  private static String userPath;
  private static String internalPath;
  private static AtomicBoolean isDolphinDirectoryInitializationRunning = new AtomicBoolean(false);

  public enum DirectoryInitializationState
  {
    NOT_YET_INITIALIZED,
    DOLPHIN_DIRECTORIES_INITIALIZED,
    EXTERNAL_STORAGE_PERMISSION_NEEDED,
    CANT_FIND_EXTERNAL_STORAGE
  }

  public static void start(Context context)
  {
    if (!isDolphinDirectoryInitializationRunning.compareAndSet(false, true))
      return;

    // Can take a few seconds to run, so don't block UI thread.
    //noinspection TrivialFunctionalExpressionUsage
    ((Runnable) () -> init(context)).run();
  }

  private static void init(Context context)
  {
    if (directoryState != DirectoryInitializationState.DOLPHIN_DIRECTORIES_INITIALIZED)
    {
      if (PermissionsHandler.hasWriteAccess(context))
      {
        if (setDolphinUserDirectory(context))
        {
          initializeInternalStorage(context);
          boolean wiimoteIniWritten = initializeExternalStorage(context);
          NativeLibrary.Initialize();
          NativeLibrary.ReportStartToAnalytics();

          areDirectoriesAvailable = true;

          if (wiimoteIniWritten)
          {
            // This has to be done after calling NativeLibrary.Initialize(),
            // as it relies on the config system
            EmulationActivity.updateWiimoteNewIniPreferences(context);
          }

          directoryState = DirectoryInitializationState.DOLPHIN_DIRECTORIES_INITIALIZED;
        }
        else
        {
          directoryState = DirectoryInitializationState.CANT_FIND_EXTERNAL_STORAGE;
        }
      }
      else
      {
        directoryState = DirectoryInitializationState.EXTERNAL_STORAGE_PERMISSION_NEEDED;
      }
    }

    isDolphinDirectoryInitializationRunning.set(false);
    sendBroadcastState(directoryState, context);
  }

  private static boolean setDolphinUserDirectory(Context context)
  {
    if (!Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState()))
      return false;

    File externalPath = Environment.getExternalStorageDirectory();
    if (externalPath == null)
      return false;

    userPath = externalPath.getAbsolutePath() + "/dolphin-emu";
    Log.debug("[DirectoryInitialization] User Dir: " + userPath);
    NativeLibrary.SetUserDirectory(userPath);

    File cacheDir = context.getExternalCacheDir();
    if (cacheDir == null)
      return false;

    Log.debug("[DirectoryInitialization] Cache Dir: " + cacheDir.getPath());
    NativeLibrary.SetCacheDirectory(cacheDir.getPath());

    return true;
  }

  private static void initializeInternalStorage(Context context)
  {
    File sysDirectory = new File(context.getFilesDir(), "Sys");
    internalPath = sysDirectory.getAbsolutePath();

    SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
    String revision = NativeLibrary.GetGitRevision();
    if (!preferences.getString("sysDirectoryVersion", "").equals(revision))
    {
      // There is no extracted Sys directory, or there is a Sys directory from another
      // version of Dolphin that might contain outdated files. Let's (re-)extract Sys.
      deleteDirectoryRecursively(sysDirectory);
      copyAssetFolder("Sys", sysDirectory, true, context);

      SharedPreferences.Editor editor = preferences.edit();
      editor.putString("sysDirectoryVersion", revision);
      editor.apply();
    }

    // Let the native code know where the Sys directory is.
    SetSysDirectory(sysDirectory.getPath());
  }

  // Returns whether the WiimoteNew.ini file was written to
  private static boolean initializeExternalStorage(Context context)
  {
    // Create User directory structure and copy some NAND files from the extracted Sys directory.
    CreateUserDirectories();

    // GCPadNew.ini and WiimoteNew.ini must contain specific values in order for controller
    // input to work as intended (they aren't user configurable), so we overwrite them just
    // in case the user has tried to modify them manually.
    //
    // ...Except WiimoteNew.ini contains the user configurable settings for Wii Remote
    // extensions in addition to all of its lines that aren't user configurable, so since we
    // don't want to lose the selected extensions, we don't overwrite that file if it exists.
    //
    // TODO: Redo the Android controller system so that we don't have to extract these INIs.
    String configDirectory = NativeLibrary.GetUserDirectory() + File.separator + "Config";
    String profileDirectory =
            NativeLibrary.GetUserDirectory() + File.separator + "Config/Profiles/Wiimote/";
    createWiimoteProfileDirectory(profileDirectory);

    copyAsset("GCPadNew.ini", new File(configDirectory, "GCPadNew.ini"), true, context);

    SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
    boolean overwriteWiimoteIni = prefs.getInt("WiimoteNewVersion", 0) != WiimoteNewVersion;
    boolean wiimoteIniWritten = copyAsset("WiimoteNew.ini",
            new File(configDirectory, "WiimoteNew.ini"), overwriteWiimoteIni, context);
    if (overwriteWiimoteIni)
    {
      SharedPreferences.Editor sPrefsEditor = prefs.edit();
      sPrefsEditor.putInt("WiimoteNewVersion", WiimoteNewVersion);
      sPrefsEditor.apply();
    }

    copyAsset("WiimoteProfile.ini", new File(profileDirectory, "WiimoteProfile.ini"), true,
            context);

    return wiimoteIniWritten;
  }

  private static void deleteDirectoryRecursively(@NonNull final File file)
  {
    if (file.isDirectory())
    {
      File[] files = file.listFiles();

      if (files == null)
      {
        return;
      }

      for (File child : files)
        deleteDirectoryRecursively(child);
    }
    if (!file.delete())
    {
      Log.error("[DirectoryInitialization] Failed to delete " + file.getAbsolutePath());
    }
  }

  public static boolean shouldStart(Context context)
  {
    return !isDolphinDirectoryInitializationRunning.get() &&
            getDolphinDirectoriesState(context) == DirectoryInitializationState.NOT_YET_INITIALIZED;
  }

  public static boolean areDolphinDirectoriesReady()
  {
    return directoryState == DirectoryInitializationState.DOLPHIN_DIRECTORIES_INITIALIZED;
  }

  public static DirectoryInitializationState getDolphinDirectoriesState(Context context)
  {
    if (directoryState == DirectoryInitializationState.NOT_YET_INITIALIZED &&
            !PermissionsHandler.hasWriteAccess(context))
    {
      return DirectoryInitializationState.EXTERNAL_STORAGE_PERMISSION_NEEDED;
    }
    else
    {
      return directoryState;
    }
  }

  public static String getUserDirectory()
  {
    if (!areDirectoriesAvailable)
    {
      throw new IllegalStateException(
              "DirectoryInitialization must run before accessing the user directory!");
    }
    return userPath;
  }

  public static String getDolphinInternalDirectory()
  {
    if (!areDirectoriesAvailable)
    {
      throw new IllegalStateException(
              "DirectoryInitialization must run before accessing the internal directory!");
    }
    return internalPath;
  }

  private static void sendBroadcastState(DirectoryInitializationState state, Context context)
  {
    Intent localIntent =
            new Intent(BROADCAST_ACTION)
                    .putExtra(EXTRA_STATE, state);
    LocalBroadcastManager.getInstance(context).sendBroadcast(localIntent);
  }

  private static boolean copyAsset(String asset, File output, Boolean overwrite, Context context)
  {
    Log.verbose("[DirectoryInitialization] Copying File " + asset + " to " + output);

    try
    {
      if (!output.exists() || overwrite)
      {
        try (InputStream in = context.getAssets().open(asset))
        {
          try (OutputStream out = new FileOutputStream(output))
          {
            copyFile(in, out);
            return true;
          }
        }
      }
    }
    catch (IOException e)
    {
      Log.error("[DirectoryInitialization] Failed to copy asset file: " + asset +
              e.getMessage());
    }
    return false;
  }

  private static void copyAssetFolder(String assetFolder, File outputFolder, Boolean overwrite,
          Context context)
  {
    Log.verbose("[DirectoryInitialization] Copying Folder " + assetFolder + " to " +
            outputFolder);

    try
    {
      String[] assetList = context.getAssets().list(assetFolder);

      if (assetList == null)
      {
        return;
      }

      boolean createdFolder = false;
      for (String file : assetList)
      {
        if (!createdFolder)
        {
          if (!outputFolder.mkdir())
          {
            Log.error("[DirectoryInitialization] Failed to create folder " +
                    outputFolder.getAbsolutePath());
          }
          createdFolder = true;
        }
        copyAssetFolder(assetFolder + File.separator + file, new File(outputFolder, file),
                overwrite, context);
        copyAsset(assetFolder + File.separator + file, new File(outputFolder, file), overwrite,
                context);
      }
    }
    catch (IOException e)
    {
      Log.error("[DirectoryInitialization] Failed to copy asset folder: " + assetFolder +
              e.getMessage());
    }
  }

  private static void copyFile(InputStream in, OutputStream out) throws IOException
  {
    byte[] buffer = new byte[1024];
    int read;

    while ((read = in.read(buffer)) != -1)
    {
      out.write(buffer, 0, read);
    }
  }

  private static void createWiimoteProfileDirectory(String directory)
  {
    File wiiPath = new File(directory);
    if (!wiiPath.isDirectory())
    {
      if (!wiiPath.mkdirs())
      {
        Log.error("[DirectoryInitialization] Failed to create folder " + wiiPath.getAbsolutePath());
      }
    }
  }

  public static boolean isExternalStorageLegacy()
  {
    return Build.VERSION.SDK_INT < Build.VERSION_CODES.Q || Environment.isExternalStorageLegacy();
  }

  public static boolean preferOldFolderPicker(Context context)
  {
    // As of January 2021, ACTION_OPEN_DOCUMENT_TREE seems to be broken on the Nvidia Shield TV
    // (the activity can't be navigated correctly with a gamepad). We can use the old folder picker
    // for the time being - Android 11 hasn't been released for this device. We have an explicit
    // check for Android 11 below in hopes that Nvidia will fix this before releasing Android 11.
    //
    // No Android TV device other than the Nvidia Shield TV is known to have an implementation
    // of ACTION_OPEN_DOCUMENT or ACTION_OPEN_DOCUMENT_TREE that even launches, but "fortunately"
    // for us, the Nvidia Shield TV is the only Android TV device in existence so far that can
    // run Dolphin at all (due to the 64-bit requirement), so we can ignore this problem.
    //
    // All phones which are running a compatible version of Android support ACTION_OPEN_DOCUMENT and
    // ACTION_OPEN_DOCUMENT_TREE, as this is required by the Android CTS (unlike with Android TV).

    return Build.VERSION.SDK_INT < Build.VERSION_CODES.R && isExternalStorageLegacy() &&
            TvUtil.isLeanback(context);
  }

  private static native void CreateUserDirectories();

  private static native void SetSysDirectory(String path);
}
