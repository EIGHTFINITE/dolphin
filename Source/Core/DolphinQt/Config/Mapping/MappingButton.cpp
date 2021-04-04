// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Config/Mapping/MappingButton.h"

#include <QApplication>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QString>

#include "DolphinQt/Config/Mapping/IOWindow.h"
#include "DolphinQt/Config/Mapping/MappingCommon.h"
#include "DolphinQt/Config/Mapping/MappingWidget.h"
#include "DolphinQt/Config/Mapping/MappingWindow.h"

#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

constexpr int SLIDER_TICK_COUNT = 100;

// Escape ampersands and remove ticks
static QString ToDisplayString(QString&& string)
{
  return string.replace(QLatin1Char{'&'}, QStringLiteral("&&"))
      .replace(QLatin1Char{'`'}, QString{});
}

bool MappingButton::IsInput() const
{
  return m_reference->IsInput();
}

MappingButton::MappingButton(MappingWidget* parent, ControlReference* ref, bool indicator)
    : ElidedButton(ToDisplayString(QString::fromStdString(ref->GetExpression()))), m_parent(parent),
      m_reference(ref)
{
  // Force all mapping buttons to stay at a minimal height.
  setFixedHeight(minimumSizeHint().height());

  // Make sure that long entries don't throw our layout out of whack.
  setFixedWidth(WIDGET_MAX_WIDTH);

  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

  if (IsInput())
  {
    setToolTip(
        tr("Left-click to detect input.\nMiddle-click to clear.\nRight-click for more options."));
  }
  else
  {
    setToolTip(tr("Left/Right-click to configure output.\nMiddle-click to clear."));
  }

  connect(this, &MappingButton::clicked, this, &MappingButton::Clicked);

  if (indicator)
    connect(parent, &MappingWidget::Update, this, &MappingButton::UpdateIndicator);

  connect(parent, &MappingWidget::ConfigChanged, this, &MappingButton::ConfigChanged);
}

void MappingButton::AdvancedPressed()
{
  IOWindow io(m_parent, m_parent->GetController(), m_reference,
              m_reference->IsInput() ? IOWindow::Type::Input : IOWindow::Type::Output);
  io.exec();

  ConfigChanged();
  m_parent->SaveSettings();
}

void MappingButton::Clicked()
{
  if (!m_reference->IsInput())
  {
    AdvancedPressed();
    return;
  }

  const auto default_device_qualifier = m_parent->GetController()->GetDefaultDevice();

  QString expression;

  if (m_parent->GetParent()->IsMappingAllDevices())
  {
    expression = MappingCommon::DetectExpression(this, g_controller_interface,
                                                 g_controller_interface.GetAllDeviceStrings(),
                                                 default_device_qualifier);
  }
  else
  {
    expression = MappingCommon::DetectExpression(this, g_controller_interface,
                                                 {default_device_qualifier.ToString()},
                                                 default_device_qualifier);
  }

  if (expression.isEmpty())
    return;

  m_reference->SetExpression(expression.toStdString());
  m_parent->GetController()->UpdateSingleControlReference(g_controller_interface, m_reference);

  ConfigChanged();
  m_parent->SaveSettings();
}

void MappingButton::Clear()
{
  m_reference->range = 100.0 / SLIDER_TICK_COUNT;

  m_reference->SetExpression("");
  m_parent->GetController()->UpdateSingleControlReference(g_controller_interface, m_reference);

  m_parent->SaveSettings();
  ConfigChanged();
}

void MappingButton::UpdateIndicator()
{
  if (!isActiveWindow())
    return;

  QFont f = m_parent->font();

  if (m_reference->GetState<bool>())
    f.setBold(true);

  setFont(f);
}

void MappingButton::ConfigChanged()
{
  setText(ToDisplayString(QString::fromStdString(m_reference->GetExpression())));
}

void MappingButton::mouseReleaseEvent(QMouseEvent* event)
{
  switch (event->button())
  {
  case Qt::MouseButton::MiddleButton:
    Clear();
    return;
  case Qt::MouseButton::RightButton:
    AdvancedPressed();
    return;
  default:
    QPushButton::mouseReleaseEvent(event);
    return;
  }
}
