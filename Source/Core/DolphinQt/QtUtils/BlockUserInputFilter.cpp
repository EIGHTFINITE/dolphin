// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/QtUtils/BlockUserInputFilter.h"

#include <QEvent>

bool BlockUserInputFilter::eventFilter(QObject* object, QEvent* event)
{
  const QEvent::Type event_type = event->type();
  return event_type == QEvent::KeyPress || event_type == QEvent::KeyRelease ||
         event_type == QEvent::MouseButtonPress || event_type == QEvent::MouseButtonRelease ||
         event_type == QEvent::MouseButtonDblClick;
}
