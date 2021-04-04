// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

class QLayout;
class QWidget;

QWidget* GetWrappedWidget(QWidget* wrapped_widget, QWidget* to_resize = nullptr,
                          int margin_width = 50, int margin_height = 50);

// Wrap wrapped_layout in a QScrollArea and fill the parent widget with it
void WrapInScrollArea(QWidget* parent, QLayout* wrapped_layout, QWidget* to_resize = nullptr);
