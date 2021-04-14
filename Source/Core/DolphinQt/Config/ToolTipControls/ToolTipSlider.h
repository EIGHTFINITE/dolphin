// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "DolphinQt/Config/ToolTipControls/ToolTipWidget.h"

#include <QSlider>

class ToolTipSlider : public ToolTipWidget<QSlider>
{
public:
  explicit ToolTipSlider(Qt::Orientation orientation);

private:
  QPoint GetToolTipPosition() const override;
};
