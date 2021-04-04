package org.dolphinemu.dolphinemu.fragments;

import android.content.Context;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;

import androidx.annotation.NonNull;
import androidx.fragment.app.Fragment;

import org.dolphinemu.dolphinemu.NativeLibrary;
import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.activities.EmulationActivity;
import org.dolphinemu.dolphinemu.features.settings.model.BooleanSetting;
import org.dolphinemu.dolphinemu.features.settings.model.Settings;
import org.dolphinemu.dolphinemu.overlay.InputOverlay;
import org.dolphinemu.dolphinemu.utils.Log;

import java.io.File;

public final class EmulationFragment extends Fragment implements SurfaceHolder.Callback
{
  private static final String KEY_GAMEPATHS = "gamepaths";

  private InputOverlay mInputOverlay;

  private EmulationState mEmulationState;

  private EmulationActivity activity;

  public static EmulationFragment newInstance(String[] gamePaths)
  {
    Bundle args = new Bundle();
    args.putStringArray(KEY_GAMEPATHS, gamePaths);

    EmulationFragment fragment = new EmulationFragment();
    fragment.setArguments(args);
    return fragment;
  }

  @Override
  public void onAttach(@NonNull Context context)
  {
    super.onAttach(context);

    if (context instanceof EmulationActivity)
    {
      activity = (EmulationActivity) context;
      NativeLibrary.setEmulationActivity((EmulationActivity) context);
    }
    else
    {
      throw new IllegalStateException("EmulationFragment must have EmulationActivity parent");
    }
  }

  /**
   * Initialize anything that doesn't depend on the layout / views in here.
   */
  @Override
  public void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);

    // So this fragment doesn't restart on configuration changes; i.e. rotation.
    setRetainInstance(true);

    String[] gamePaths = getArguments().getStringArray(KEY_GAMEPATHS);
    mEmulationState = new EmulationState(gamePaths, getTemporaryStateFilePath());
  }

  /**
   * Initialize the UI and start emulation in here.
   */
  @Override
  public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState)
  {
    View contents = inflater.inflate(R.layout.fragment_emulation, container, false);

    SurfaceView surfaceView = contents.findViewById(R.id.surface_emulation);
    surfaceView.getHolder().addCallback(this);

    mInputOverlay = contents.findViewById(R.id.surface_input_overlay);

    Button doneButton = contents.findViewById(R.id.done_control_config);
    if (doneButton != null)
    {
      doneButton.setOnClickListener(v -> stopConfiguringControls());
    }

    if (mInputOverlay != null)
    {
      contents.post(() ->
      {
        int overlayX = mInputOverlay.getLeft();
        int overlayY = mInputOverlay.getTop();
        mInputOverlay.setSurfacePosition(new Rect(
                surfaceView.getLeft() - overlayX, surfaceView.getTop() - overlayY,
                surfaceView.getRight() - overlayX, surfaceView.getBottom() - overlayY));
      });
    }

    // The new Surface created here will get passed to the native code via onSurfaceChanged.

    return contents;
  }

  @Override
  public void onResume()
  {
    super.onResume();
    mEmulationState.run(activity.isActivityRecreated());
  }

  @Override
  public void onPause()
  {
    if (mEmulationState.isRunning() && !NativeLibrary.IsShowingAlertMessage())
      mEmulationState.pause();
    super.onPause();
  }

  @Override
  public void onDetach()
  {
    NativeLibrary.clearEmulationActivity();
    super.onDetach();
  }

  public void toggleInputOverlayVisibility(Settings settings)
  {
    BooleanSetting.MAIN_SHOW_INPUT_OVERLAY
            .setBoolean(settings, !BooleanSetting.MAIN_SHOW_INPUT_OVERLAY.getBoolean(settings));

    if (mInputOverlay != null)
      mInputOverlay.refreshControls();
  }

  public void initInputPointer()
  {
    if (mInputOverlay != null)
      mInputOverlay.initTouchPointer();
  }

  public void refreshInputOverlay()
  {
    if (mInputOverlay != null)
      mInputOverlay.refreshControls();
  }

  public void resetInputOverlay()
  {
    if (mInputOverlay != null)
      mInputOverlay.resetButtonPlacement();
  }

  @Override
  public void surfaceCreated(@NonNull SurfaceHolder holder)
  {
    // We purposely don't do anything here.
    // All work is done in surfaceChanged, which we are guaranteed to get even for surface creation.
  }

  @Override
  public void surfaceChanged(SurfaceHolder holder, int format, int width, int height)
  {
    Log.debug("[EmulationFragment] Surface changed. Resolution: " + width + "x" + height);
    mEmulationState.newSurface(holder.getSurface());
  }

  @Override
  public void surfaceDestroyed(@NonNull SurfaceHolder holder)
  {
    mEmulationState.clearSurface();
  }

  public void stopEmulation()
  {
    mEmulationState.stop();
  }

  public void startConfiguringControls()
  {
    if (mInputOverlay != null)
    {
      requireView().findViewById(R.id.done_control_config).setVisibility(View.VISIBLE);
      mInputOverlay.setIsInEditMode(true);
    }
  }

  public void stopConfiguringControls()
  {
    if (mInputOverlay != null)
    {
      requireView().findViewById(R.id.done_control_config).setVisibility(View.GONE);
      mInputOverlay.setIsInEditMode(false);
    }
  }

  public boolean isConfiguringControls()
  {
    return mInputOverlay != null && mInputOverlay.isInEditMode();
  }

  private static class EmulationState
  {
    private enum State
    {
      STOPPED, RUNNING, PAUSED
    }

    private final String[] mGamePaths;
    private State state;
    private Surface mSurface;
    private boolean mRunWhenSurfaceIsValid;
    private boolean loadPreviousTemporaryState;
    private final String temporaryStatePath;

    EmulationState(String[] gamePaths, String temporaryStatePath)
    {
      mGamePaths = gamePaths;
      this.temporaryStatePath = temporaryStatePath;
      // Starting state is stopped.
      state = State.STOPPED;
    }

    // Getters for the current state

    public synchronized boolean isStopped()
    {
      return state == State.STOPPED;
    }

    public synchronized boolean isPaused()
    {
      return state == State.PAUSED;
    }

    public synchronized boolean isRunning()
    {
      return state == State.RUNNING;
    }

    // State changing methods

    public synchronized void stop()
    {
      if (state != State.STOPPED)
      {
        Log.debug("[EmulationFragment] Stopping emulation.");
        state = State.STOPPED;
        NativeLibrary.StopEmulation();
      }
      else
      {
        Log.warning("[EmulationFragment] Stop called while already stopped.");
      }
    }

    public synchronized void pause()
    {
      if (state != State.PAUSED)
      {
        state = State.PAUSED;
        Log.debug("[EmulationFragment] Pausing emulation.");

        NativeLibrary.PauseEmulation();
      }
      else
      {
        Log.warning("[EmulationFragment] Pause called while already paused.");
      }
    }

    public synchronized void run(boolean isActivityRecreated)
    {
      if (isActivityRecreated)
      {
        if (NativeLibrary.IsRunning())
        {
          loadPreviousTemporaryState = false;
          state = State.PAUSED;
          deleteFile(temporaryStatePath);
        }
        else
        {
          loadPreviousTemporaryState = true;
        }
      }
      else
      {
        Log.debug("[EmulationFragment] activity resumed or fresh start");
        loadPreviousTemporaryState = false;
        // activity resumed without being killed or this is the first run
        deleteFile(temporaryStatePath);
      }

      // If the surface is set, run now. Otherwise, wait for it to get set.
      if (mSurface != null)
      {
        runWithValidSurface();
      }
      else
      {
        mRunWhenSurfaceIsValid = true;
      }
    }

    // Surface callbacks
    public synchronized void newSurface(Surface surface)
    {
      mSurface = surface;
      if (mRunWhenSurfaceIsValid)
      {
        runWithValidSurface();
      }
    }

    public synchronized void clearSurface()
    {
      if (mSurface == null)
      {
        Log.warning("[EmulationFragment] clearSurface called, but surface already null.");
      }
      else
      {
        mSurface = null;
        Log.debug("[EmulationFragment] Surface destroyed.");

        NativeLibrary.SurfaceDestroyed();
      }
    }

    private void runWithValidSurface()
    {
      mRunWhenSurfaceIsValid = false;
      if (state == State.STOPPED)
      {
        Thread emulationThread = new Thread(() ->
        {
          NativeLibrary.SurfaceChanged(mSurface);
          if (loadPreviousTemporaryState)
          {
            Log.debug("[EmulationFragment] Starting emulation thread from previous state.");
            NativeLibrary.Run(mGamePaths, temporaryStatePath, true);
          }
          else
          {
            Log.debug("[EmulationFragment] Starting emulation thread.");
            NativeLibrary.Run(mGamePaths);
          }
          EmulationActivity.stopIgnoringLaunchRequests();
        }, "NativeEmulation");
        emulationThread.start();
      }
      else if (state == State.PAUSED)
      {
        NativeLibrary.SurfaceChanged(mSurface);
        if (!EmulationActivity.getHasUserPausedEmulation() &&
                !NativeLibrary.IsShowingAlertMessage())
        {
          Log.debug("[EmulationFragment] Resuming emulation.");
          NativeLibrary.UnPauseEmulation();
        }
      }
      else
      {
        Log.debug("[EmulationFragment] Bug, run called while already running.");
      }
      state = State.RUNNING;
    }
  }

  public void saveTemporaryState()
  {
    NativeLibrary.SaveStateAs(getTemporaryStateFilePath(), true);
  }

  private String getTemporaryStateFilePath()
  {
    return getContext().getFilesDir() + File.separator + "temp.sav";
  }

  private static void deleteFile(String path)
  {
    try
    {
      File file = new File(path);
      if (!file.delete())
      {
        Log.error("[EmulationFragment] Failed to delete " + file.getAbsolutePath());
      }
    }
    catch (Exception ignored)
    {
    }
  }
}
