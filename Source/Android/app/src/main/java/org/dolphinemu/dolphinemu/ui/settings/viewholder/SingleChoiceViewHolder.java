package org.dolphinemu.dolphinemu.ui.settings.viewholder;

import android.view.View;
import android.widget.TextView;

import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.model.settings.view.SettingsItem;
import org.dolphinemu.dolphinemu.model.settings.view.SingleChoiceSetting;
import org.dolphinemu.dolphinemu.ui.settings.SettingsAdapter;

public final class SingleChoiceViewHolder extends SettingViewHolder
{
	private SingleChoiceSetting mItem;

	private TextView mTextSettingName;
	private TextView mTextSettingDescription;

	public SingleChoiceViewHolder(View itemView, SettingsAdapter adapter)
	{
		super(itemView, adapter);
	}

	@Override
	protected void findViews(View root)
	{
		mTextSettingName = (TextView) root.findViewById(R.id.text_setting_name);
		mTextSettingDescription = (TextView) root.findViewById(R.id.text_setting_description);
	}

	@Override
	public void bind(SettingsItem item)
	{
		mItem = (SingleChoiceSetting) item;

		mTextSettingName.setText(item.getNameId());

		if (item.getDescriptionId() > 0)
		{
			mTextSettingDescription.setText(item.getDescriptionId());
		}
	}

	@Override
	public void onClick(View clicked)
	{
		getAdapter().onSingleChoiceClick(mItem);
	}
}
