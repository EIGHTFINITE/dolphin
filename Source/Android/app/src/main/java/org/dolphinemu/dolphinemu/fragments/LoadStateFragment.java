package org.dolphinemu.dolphinemu.fragments;

import android.app.Fragment;
import android.os.Bundle;
import android.support.annotation.Nullable;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.GridLayout;

import org.dolphinemu.dolphinemu.BuildConfig;
import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.activities.EmulationActivity;

public final class LoadStateFragment extends Fragment implements View.OnClickListener
{
	public static final String FRAGMENT_TAG = BuildConfig.APPLICATION_ID + ".load_state";
	public static final int FRAGMENT_ID = R.layout.fragment_state_load;

	public static LoadStateFragment newInstance()
	{
		LoadStateFragment fragment = new LoadStateFragment();

		// TODO Add any appropriate arguments to this fragment.

		return fragment;
	}

	@Nullable
	@Override
	public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState)
	{
		View rootView = inflater.inflate(FRAGMENT_ID, container, false);

		GridLayout grid = (GridLayout) rootView.findViewById(R.id.grid_state_slots);
		for (int childIndex = 0; childIndex < grid.getChildCount(); childIndex++)
		{
			Button button = (Button) grid.getChildAt(childIndex);

			button.setOnClickListener(this);
		}

		// So that item clicked to start this Fragment is no longer the focused item.
		grid.requestFocus();

		return rootView;
	}

	@Override
	public void onClick(View button)
	{
		((EmulationActivity) getActivity()).onMenuItemClicked(button.getId());
	}
}
