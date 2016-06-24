package org.dolphinemu.dolphinemu.ui.settings;


import android.app.FragmentTransaction;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.widget.Toast;

import org.dolphinemu.dolphinemu.BuildConfig;
import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.model.settings.SettingSection;

import java.util.HashMap;

public final class SettingsActivity extends AppCompatActivity implements SettingsActivityView
{
	private SettingsActivityPresenter mPresenter = new SettingsActivityPresenter(this);

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);

		setContentView(R.layout.activity_settings);

		Intent launcher = getIntent();
		String filename = launcher.getStringExtra(ARGUMENT_FILE_NAME);

		mPresenter.onCreate(savedInstanceState, filename);
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		MenuInflater inflater = getMenuInflater();
		inflater.inflate(R.menu.menu_settings, menu);

		return true;
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item)
	{
		return mPresenter.handleOptionsItem(item.getItemId());
	}

	@Override
	protected void onSaveInstanceState(Bundle outState)
	{
		// Critical: If super method is not called, rotations will be busted.
		super.onSaveInstanceState(outState);
		mPresenter.saveState(outState);
	}

	/**
	 * If this is called, the user has left the settings screen (potentially through the
	 * home button) and will expect their changes to be persisted. So we kick off an
	 * IntentService which will do so on a background thread.
	 */
	@Override
	protected void onStop()
	{
		super.onStop();

		mPresenter.onStop(isFinishing());
	}

	@Override
	public void onBackPressed()
	{
		mPresenter.onBackPressed();
	}


	@Override
	public void showSettingsFragment(String menuTag, boolean addToStack)
	{
		FragmentTransaction transaction = getFragmentManager().beginTransaction();

		if (addToStack)
		{
			transaction.setCustomAnimations(
					R.animator.settings_enter,
					R.animator.settings_exit,
					R.animator.settings_pop_enter,
					R.animator.setttings_pop_exit);

			transaction.addToBackStack(null);
			mPresenter.addToStack();
		}
		transaction.replace(R.id.frame_content, SettingsFragment.newInstance(menuTag), SettingsFragment.FRAGMENT_TAG);

		transaction.commit();
	}

	@Override
	public HashMap<String, SettingSection> getSettings()
	{
		return mPresenter.getSettings();
	}

	@Override
	public void setSettings(HashMap<String, SettingSection> settings)
	{
		mPresenter.setSettings(settings);
	}

	@Override
	public void onSettingsFileLoaded(HashMap<String, SettingSection> settings)
	{
		SettingsFragmentView fragment = getFragment();

		if (fragment != null)
		{
			fragment.onSettingsFileLoaded(settings);
		}
	}

	@Override
	public void onSettingsFileNotFound()
	{
		SettingsFragmentView fragment = getFragment();

		if (fragment != null)
		{
			fragment.loadDefaultSettings();
		}
	}

	@Override
	public void showToastMessage(String message)
	{
		Toast.makeText(this, message, Toast.LENGTH_SHORT).show();
	}

	@Override
	public void popBackStack()
	{
		getFragmentManager().popBackStackImmediate();
	}

	@Override
	public void onSettingChanged()
	{
		mPresenter.onSettingChanged();
	}

	@Override
	public void onGcPadSettingChanged(String key, int value)
	{
		mPresenter.onGcPadSettingChanged(key, value);
	}

	@Override
	public void onWiimoteSettingChanged(String section, int value)
	{
		mPresenter.onWiimoteSettingChanged(section, value);
	}

	private SettingsFragment getFragment()
	{
		return (SettingsFragment) getFragmentManager().findFragmentByTag(SettingsFragment.FRAGMENT_TAG);
	}

	public static final String ARGUMENT_FILE_NAME = BuildConfig.APPLICATION_ID + ".file_name";

	public static void launch(Context context, String menuTag)
	{
		Intent settings = new Intent(context, SettingsActivity.class);

		settings.putExtra(ARGUMENT_FILE_NAME, menuTag);

		context.startActivity(settings);
	}
}
