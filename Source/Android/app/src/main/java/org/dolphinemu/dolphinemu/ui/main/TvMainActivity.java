package org.dolphinemu.dolphinemu.ui.main;

import android.app.Activity;
import android.app.FragmentManager;
import android.content.Intent;
import android.database.Cursor;
import android.os.Bundle;
import android.support.v17.leanback.app.BrowseFragment;
import android.support.v17.leanback.database.CursorMapper;
import android.support.v17.leanback.widget.ArrayObjectAdapter;
import android.support.v17.leanback.widget.CursorObjectAdapter;
import android.support.v17.leanback.widget.HeaderItem;
import android.support.v17.leanback.widget.ListRow;
import android.support.v17.leanback.widget.ListRowPresenter;
import android.support.v17.leanback.widget.OnItemViewClickedListener;
import android.support.v17.leanback.widget.Presenter;
import android.support.v17.leanback.widget.Row;
import android.support.v17.leanback.widget.RowPresenter;

import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.activities.AddDirectoryActivity;
import org.dolphinemu.dolphinemu.activities.EmulationActivity;
import org.dolphinemu.dolphinemu.adapters.GameRowPresenter;
import org.dolphinemu.dolphinemu.adapters.SettingsRowPresenter;
import org.dolphinemu.dolphinemu.model.Game;
import org.dolphinemu.dolphinemu.model.TvSettingsItem;
import org.dolphinemu.dolphinemu.ui.settings.SettingsActivity;
import org.dolphinemu.dolphinemu.utils.StartupHandler;
import org.dolphinemu.dolphinemu.viewholders.TvGameViewHolder;

public final class TvMainActivity extends Activity implements MainView
{
	private MainPresenter mPresenter = new MainPresenter(this);

	private BrowseFragment mBrowseFragment;

	private ArrayObjectAdapter mRowsAdapter;

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_tv_main);

		final FragmentManager fragmentManager = getFragmentManager();
		mBrowseFragment = (BrowseFragment) fragmentManager.findFragmentById(
				R.id.fragment_game_list);

		// Set display parameters for the BrowseFragment
		mBrowseFragment.setHeadersState(BrowseFragment.HEADERS_ENABLED);
		mBrowseFragment.setTitle(getString(R.string.app_name));
		mBrowseFragment.setBadgeDrawable(getResources().getDrawable(
				R.drawable.ic_launcher, null));
		mBrowseFragment.setBrandColor(getResources().getColor(R.color.dolphin_blue_dark));

		buildRowsAdapter();

		mPresenter.onCreate();

		mBrowseFragment.setOnItemViewClickedListener(
				new OnItemViewClickedListener()
				{
					@Override
					public void onItemClicked(Presenter.ViewHolder itemViewHolder, Object item, RowPresenter.ViewHolder rowViewHolder, Row row)
					{
						// Special case: user clicked on a settings row item.
						if (item instanceof TvSettingsItem)
						{
							TvSettingsItem settingsItem = (TvSettingsItem) item;
							mPresenter.handleOptionSelection(settingsItem.getItemId());
						}
						else
						{
							TvGameViewHolder holder = (TvGameViewHolder) itemViewHolder;

							// Start the emulation activity and send the path of the clicked ISO to it.
							EmulationActivity.launch(TvMainActivity.this,
									holder.path,
									holder.title,
									holder.screenshotPath,
									-1,
									holder.imageScreenshot);
						}
					}
				});

		// Stuff in this block only happens when this activity is newly created (i.e. not a rotation)
		if (savedInstanceState == null)
			StartupHandler.HandleInit(this);
	}

	/**
	 * MainView
	 */

	@Override
	public void setVersionString(String version)
	{
		// No-op
	}

	@Override
	public void refresh()
	{
		recreate();
	}

	@Override
	public void refreshFragmentScreenshot(int fragmentPosition)
	{
		mRowsAdapter.notifyArrayItemRangeChanged(0, mRowsAdapter.size());
	}

	@Override
	public void launchSettingsActivity(String menuTag)
	{
		SettingsActivity.launch(this, menuTag);
	}

	@Override
	public void launchFileListActivity()
	{
		AddDirectoryActivity.launch(this);
	}

	@Override
	public void showGames(int platformIndex, Cursor games)
	{
		ListRow row = buildGamesRow(platformIndex, games);

		// Add row to the adapter only if it is not empty.
		if (row != null)
		{
			mRowsAdapter.add(row);
		}
	}

	/**
	 * Callback from AddDirectoryActivity. Applies any changes necessary to the GameGridActivity.
	 *
	 * @param requestCode An int describing whether the Activity that is returning did so successfully.
	 * @param resultCode  An int describing what Activity is giving us this callback.
	 * @param result      The information the returning Activity is providing us.
	 */
	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent result)
	{
		mPresenter.handleActivityResult(requestCode, resultCode);
	}

	private void buildRowsAdapter()
	{
		mRowsAdapter = new ArrayObjectAdapter(new ListRowPresenter());

		// For each platform
		for (int platformIndex = 0; platformIndex <= Game.PLATFORM_ALL; ++platformIndex)
		{
			mPresenter.loadGames(platformIndex);
		}

		mRowsAdapter.add(buildSettingsRow());

		mBrowseFragment.setAdapter(mRowsAdapter);
	}

	private ListRow buildGamesRow(int platform, Cursor games)
	{
		// Create an adapter for this row.
		CursorObjectAdapter row = new CursorObjectAdapter(new GameRowPresenter());

		// If cursor is empty, don't return a Row.
		if (!games.moveToFirst())
		{
			return null;
		}

		row.changeCursor(games);
		row.setMapper(new CursorMapper()
		{
			@Override
			protected void bindColumns(Cursor cursor)
			{
				// No-op? Not sure what this does.
			}

			@Override
			protected Object bind(Cursor cursor)
			{
				return Game.fromCursor(cursor);
			}
		});

		String headerName;
		switch (platform)
		{
			case Game.PLATFORM_GC:
				headerName = "GameCube Games";
				break;

			case Game.PLATFORM_WII:
				headerName = "Wii Games";
				break;

			case Game.PLATFORM_WII_WARE:
				headerName = "WiiWare";
				break;

			case Game.PLATFORM_ALL:
				headerName = "All Games";
				break;

			default:
				headerName = "Error";
				break;
		}

		// Create a header for this row.
		HeaderItem header = new HeaderItem(platform, headerName);

		// Create the row, passing it the filled adapter and the header, and give it to the master adapter.
		return new ListRow(header, row);
	}

	private ListRow buildSettingsRow()
	{
		ArrayObjectAdapter rowItems = new ArrayObjectAdapter(new SettingsRowPresenter());

		rowItems.add(new TvSettingsItem(R.id.menu_settings_core,
				R.drawable.ic_settings_core_tv,
				R.string.grid_menu_core_settings));

		rowItems.add(new TvSettingsItem(R.id.menu_settings_video,
				R.drawable.ic_settings_graphics_tv,
				R.string.grid_menu_video_settings));

		rowItems.add(new TvSettingsItem(R.id.menu_settings_gcpad,
				R.drawable.ic_settings_gcpad,
				R.string.grid_menu_gcpad_settings));

		rowItems.add(new TvSettingsItem(R.id.menu_settings_wiimote,
				R.drawable.ic_settings_wiimote,
				R.string.grid_menu_wiimote_settings));

		rowItems.add(new TvSettingsItem(R.id.button_add_directory,
				R.drawable.ic_add_tv,
				R.string.add_directory_title));

		rowItems.add(new TvSettingsItem(R.id.menu_refresh,
				R.drawable.ic_refresh_tv,
				R.string.grid_menu_refresh));

		// Create a header for this row.
		HeaderItem header = new HeaderItem(R.string.settings, getString(R.string.settings));

		return new ListRow(header, rowItems);
	}
}
