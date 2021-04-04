// Copyright 2021 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Config/CommonControllersWidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ConfigManager.h"
#include "Core/Core.h"

#include "DolphinQt/Config/ControllerInterface/ControllerInterfaceWindow.h"

CommonControllersWidget::CommonControllersWidget(QWidget* parent) : QWidget(parent)
{
  CreateLayout();
  LoadSettings();
  ConnectWidgets();
}

void CommonControllersWidget::CreateLayout()
{
  // i18n: This is "common" as in "shared", not the opposite of "uncommon"
  m_common_box = new QGroupBox(tr("Common"));
  m_common_layout = new QVBoxLayout();
  m_common_bg_input = new QCheckBox(tr("Background Input"));
  m_common_configure_controller_interface = new QPushButton(tr("Alternate Input Sources"));

  m_common_layout->addWidget(m_common_bg_input);
  m_common_layout->addWidget(m_common_configure_controller_interface);

  m_common_box->setLayout(m_common_layout);

  auto* layout = new QVBoxLayout;
  layout->setMargin(0);
  layout->setAlignment(Qt::AlignTop);
  layout->addWidget(m_common_box);
  setLayout(layout);
}

void CommonControllersWidget::ConnectWidgets()
{
  connect(m_common_bg_input, &QCheckBox::toggled, this, &CommonControllersWidget::SaveSettings);
  connect(m_common_configure_controller_interface, &QPushButton::clicked, this,
          &CommonControllersWidget::OnControllerInterfaceConfigure);
}

void CommonControllersWidget::OnControllerInterfaceConfigure()
{
  ControllerInterfaceWindow* window = new ControllerInterfaceWindow(this);
  window->setAttribute(Qt::WA_DeleteOnClose, true);
  window->setWindowModality(Qt::WindowModality::WindowModal);
  window->show();
}

void CommonControllersWidget::LoadSettings()
{
  m_common_bg_input->setChecked(SConfig::GetInstance().m_BackgroundInput);
}

void CommonControllersWidget::SaveSettings()
{
  SConfig::GetInstance().m_BackgroundInput = m_common_bg_input->isChecked();
  SConfig::GetInstance().SaveSettings();
}
