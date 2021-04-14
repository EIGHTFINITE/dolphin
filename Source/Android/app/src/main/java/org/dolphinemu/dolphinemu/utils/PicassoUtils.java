package org.dolphinemu.dolphinemu.utils;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.net.Uri;
import android.widget.ImageView;

import com.squareup.picasso.Callback;
import com.squareup.picasso.Picasso;

import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.features.settings.model.BooleanSetting;
import org.dolphinemu.dolphinemu.model.GameFile;

import java.io.File;

public class PicassoUtils
{
  public static void loadGameBanner(ImageView imageView, GameFile gameFile)
  {
    Picasso picassoInstance = new Picasso.Builder(imageView.getContext())
            .addRequestHandler(new GameBannerRequestHandler(gameFile))
            .build();

    picassoInstance
            .load(Uri.parse("iso:/" + gameFile.getPath()))
            .fit()
            .noFade()
            .noPlaceholder()
            .config(Bitmap.Config.RGB_565)
            .error(R.drawable.no_banner)
            .into(imageView);
  }

  public static void loadGameCover(ImageView imageView, GameFile gameFile)
  {
    Context context = imageView.getContext();
    File cover = new File(gameFile.getCustomCoverPath());
    if (cover.exists())
    {
      Picasso.get()
              .load(cover)
              .noFade()
              .noPlaceholder()
              .fit()
              .centerInside()
              .config(Bitmap.Config.ARGB_8888)
              .error(R.drawable.no_banner)
              .into(imageView);
    }
    else if ((cover = new File(gameFile.getCoverPath(context))).exists())
    {
      Picasso.get()
              .load(cover)
              .noFade()
              .noPlaceholder()
              .fit()
              .centerInside()
              .config(Bitmap.Config.ARGB_8888)
              .error(R.drawable.no_banner)
              .into(imageView);
    }
    // GameTDB has a pretty close to complete collection for US/EN covers. First pass at getting
    // the cover will be by the disk's region, second will be the US cover, and third EN.
    else if (BooleanSetting.MAIN_USE_GAME_COVERS.getBooleanGlobal())
    {
      Picasso.get()
              .load(CoverHelper.buildGameTDBUrl(gameFile, CoverHelper.getRegion(gameFile)))
              .noFade()
              .noPlaceholder()
              .fit()
              .centerInside()
              .config(Bitmap.Config.ARGB_8888)
              .error(R.drawable.no_banner)
              .into(imageView, new Callback()
              {
                @Override
                public void onSuccess()
                {
                  CoverHelper.saveCover(((BitmapDrawable) imageView.getDrawable()).getBitmap(),
                          gameFile.getCoverPath(context));
                }

                @Override
                public void onError(Exception ex) // Second pass using US region
                {
                  Picasso.get()
                          .load(CoverHelper.buildGameTDBUrl(gameFile, "US"))
                          .fit()
                          .noFade()
                          .fit()
                          .centerInside()
                          .noPlaceholder()
                          .config(Bitmap.Config.ARGB_8888)
                          .error(R.drawable.no_banner)
                          .into(imageView, new Callback()
                          {
                            @Override
                            public void onSuccess()
                            {
                              CoverHelper.saveCover(
                                      ((BitmapDrawable) imageView.getDrawable()).getBitmap(),
                                      gameFile.getCoverPath(context));
                            }

                            @Override
                            public void onError(Exception ex) // Third and last pass using EN region
                            {
                              Picasso.get()
                                      .load(CoverHelper.buildGameTDBUrl(gameFile, "EN"))
                                      .fit()
                                      .noFade()
                                      .fit()
                                      .centerInside()
                                      .noPlaceholder()
                                      .config(Bitmap.Config.ARGB_8888)
                                      .error(R.drawable.no_banner)
                                      .into(imageView, new Callback()
                                      {
                                        @Override
                                        public void onSuccess()
                                        {
                                          CoverHelper.saveCover(
                                                  ((BitmapDrawable) imageView.getDrawable())
                                                          .getBitmap(),
                                                  gameFile.getCoverPath(context));
                                        }

                                        @Override
                                        public void onError(Exception ex)
                                        {
                                        }
                                      });
                            }
                          });
                }
              });
    }
    else
    {
      Picasso.get()
              .load(R.drawable.no_banner)
              .noFade()
              .noPlaceholder()
              .fit()
              .centerInside()
              .config(Bitmap.Config.ARGB_8888)
              .into(imageView);
    }
  }
}
