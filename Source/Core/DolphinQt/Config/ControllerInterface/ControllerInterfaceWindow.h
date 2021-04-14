// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <QDialog>

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#if defined(CIFACE_USE_DUALSHOCKUDPCLIENT)
class DualShockUDPClientWidget;
#endif
class QTabWidget;
class QDialogButtonBox;

class ControllerInterfaceWindow final : public QDialog
{
  Q_OBJECT
public:
  explicit ControllerInterfaceWindow(QWidget* parent);

private:
  void CreateMainLayout();

  QTabWidget* m_tab_widget;
  QDialogButtonBox* m_button_box;

#if defined(CIFACE_USE_DUALSHOCKUDPCLIENT)
  DualShockUDPClientWidget* m_dsuclient_widget;
#endif
};
