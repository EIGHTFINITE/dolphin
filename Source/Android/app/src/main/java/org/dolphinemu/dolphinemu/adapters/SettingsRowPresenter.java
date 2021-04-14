package org.dolphinemu.dolphinemu.adapters;

import android.content.res.Resources;
import android.view.ViewGroup;

import androidx.leanback.widget.ImageCardView;
import androidx.leanback.widget.Presenter;

import org.dolphinemu.dolphinemu.model.TvSettingsItem;
import org.dolphinemu.dolphinemu.viewholders.TvSettingsViewHolder;

public final class SettingsRowPresenter extends Presenter
{
  public Presenter.ViewHolder onCreateViewHolder(ViewGroup parent)
  {
    // Create a new view.
    ImageCardView settingsCard = new ImageCardView(parent.getContext());

    settingsCard.setMainImageAdjustViewBounds(true);
    settingsCard.setMainImageDimensions(192, 160);


    settingsCard.setFocusable(true);
    settingsCard.setFocusableInTouchMode(true);

    // Use that view to create a ViewHolder.
    return new TvSettingsViewHolder(settingsCard);
  }

  public void onBindViewHolder(Presenter.ViewHolder viewHolder, Object item)
  {
    TvSettingsViewHolder holder = (TvSettingsViewHolder) viewHolder;
    TvSettingsItem settingsItem = (TvSettingsItem) item;

    Resources resources = holder.cardParent.getResources();

    holder.itemId = settingsItem.getItemId();

    holder.cardParent.setTitleText(resources.getString(settingsItem.getLabelId()));
    holder.cardParent.setMainImage(resources.getDrawable(settingsItem.getIconId(), null));
  }

  public void onUnbindViewHolder(Presenter.ViewHolder viewHolder)
  {
    // no op
  }
}
