package org.dolphinemu.dolphinemu.viewholders;

import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.recyclerview.widget.RecyclerView;

import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.model.GameFile;

/**
 * A simple class that stores references to views so that the GameAdapter doesn't need to
 * keep calling findViewById(), which is expensive.
 */
public class GameViewHolder extends RecyclerView.ViewHolder
{
  public ImageView imageScreenshot;
  public TextView textGameTitle;
  public TextView textGameCaption;

  public GameFile gameFile;

  public GameViewHolder(View itemView)
  {
    super(itemView);

    itemView.setTag(this);

    imageScreenshot = itemView.findViewById(R.id.image_game_screen);
    textGameTitle = itemView.findViewById(R.id.text_game_title);
    textGameCaption = itemView.findViewById(R.id.text_game_caption);
  }
}
