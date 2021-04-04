package org.dolphinemu.dolphinemu.features.settings.ui.viewholder;

import android.content.Context;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.recyclerview.widget.RecyclerView;

import org.dolphinemu.dolphinemu.DolphinApplication;
import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.features.settings.model.view.SettingsItem;
import org.dolphinemu.dolphinemu.features.settings.ui.SettingsAdapter;

public abstract class SettingViewHolder extends RecyclerView.ViewHolder
        implements View.OnClickListener, View.OnLongClickListener
{
  private SettingsAdapter mAdapter;

  public SettingViewHolder(View itemView, SettingsAdapter adapter)
  {
    super(itemView);

    mAdapter = adapter;

    itemView.setOnClickListener(this);
    itemView.setOnLongClickListener(this);

    findViews(itemView);
  }

  protected SettingsAdapter getAdapter()
  {
    return mAdapter;
  }

  protected void setStyle(TextView textView, SettingsItem settingsItem)
  {
    boolean overridden = settingsItem.isOverridden(mAdapter.getSettings());
    textView.setTypeface(null, overridden ? Typeface.BOLD : Typeface.NORMAL);

    if (!settingsItem.isEditable())
      textView.setPaintFlags(textView.getPaintFlags() | Paint.STRIKE_THRU_TEXT_FLAG);
  }

  protected static void showNotRuntimeEditableError()
  {
    Toast.makeText(DolphinApplication.getAppContext(), R.string.setting_not_runtime_editable,
            Toast.LENGTH_SHORT).show();
  }

  /**
   * Gets handles to all this ViewHolder's child views using their XML-defined identifiers.
   *
   * @param root The newly inflated top-level view.
   */
  protected abstract void findViews(View root);

  /**
   * Called by the adapter to set this ViewHolder's child views to display the list item
   * it must now represent.
   *
   * @param item The list item that should be represented by this ViewHolder.
   */
  public abstract void bind(SettingsItem item);

  /**
   * Called when this ViewHolder's view is clicked on. Implementations should usually pass
   * this event up to the adapter.
   *
   * @param clicked The view that was clicked on.
   */
  public abstract void onClick(View clicked);

  @Nullable
  protected abstract SettingsItem getItem();

  public boolean onLongClick(View clicked)
  {
    SettingsItem item = getItem();

    if (item == null || !item.hasSetting())
      return false;

    if (!item.isEditable())
    {
      showNotRuntimeEditableError();
      return true;
    }

    Context context = clicked.getContext();

    AlertDialog.Builder builder = new AlertDialog.Builder(context, R.style.DolphinDialogBase)
            .setMessage(R.string.setting_clear_confirm);

    builder
            .setPositiveButton(R.string.ok, (dialog, whichButton) ->
            {
              getAdapter().clearSetting(item, getAdapterPosition());
              bind(item);
              Toast.makeText(context, R.string.setting_cleared, Toast.LENGTH_SHORT).show();
              dialog.dismiss();
            })
            .setNegativeButton(R.string.cancel, (dialog, whichButton) -> dialog.dismiss());

    builder.show();

    return true;
  }
}
