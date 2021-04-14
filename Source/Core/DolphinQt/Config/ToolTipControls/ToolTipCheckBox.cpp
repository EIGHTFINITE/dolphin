// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Config/ToolTipControls/ToolTipCheckBox.h"

#include <QStyle>
#include <QStyleOption>

ToolTipCheckBox::ToolTipCheckBox(const QString& label) : ToolTipWidget(label)
{
  SetTitle(label);
}

QPoint ToolTipCheckBox::GetToolTipPosition() const
{
  int checkbox_width = 18;
  if (style())
  {
    QStyleOptionButton opt;
    initStyleOption(&opt);
    checkbox_width =
        style()->subElementRect(QStyle::SubElement::SE_CheckBoxIndicator, &opt, this).width();
  }

  return pos() + QPoint(checkbox_width / 2, height() / 2);
}
