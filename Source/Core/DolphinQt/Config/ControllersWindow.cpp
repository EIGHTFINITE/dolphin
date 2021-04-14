// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Config/ControllersWindow.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "DolphinQt/Config/CommonControllersWidget.h"
#include "DolphinQt/Config/GamecubeControllersWidget.h"
#include "DolphinQt/Config/WiimoteControllersWidget.h"
#include "DolphinQt/QtUtils/WrapInScrollArea.h"

ControllersWindow::ControllersWindow(QWidget* parent) : QDialog(parent)
{
  setWindowTitle(tr("Controller Settings"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  m_gamecube_controllers = new GamecubeControllersWidget(this);
  m_wiimote_controllers = new WiimoteControllersWidget(this);
  m_common = new CommonControllersWidget(this);
  CreateMainLayout();
  ConnectWidgets();
}

void ControllersWindow::CreateMainLayout()
{
  auto* layout = new QVBoxLayout();
  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);

  layout->addWidget(m_gamecube_controllers);
  layout->addWidget(m_wiimote_controllers);
  layout->addWidget(m_common);
  layout->addStretch();
  layout->addWidget(m_button_box);

  WrapInScrollArea(this, layout);
}

void ControllersWindow::ConnectWidgets()
{
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
