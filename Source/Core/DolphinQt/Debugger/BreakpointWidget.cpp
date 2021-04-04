// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Debugger/BreakpointWidget.h"

#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/PowerPC/BreakPoints.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"

#include "DolphinQt/Debugger/NewBreakpointDialog.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

// Qt constants
namespace
{
enum CustomRole
{
  ADDRESS_ROLE = Qt::UserRole,
  IS_MEMCHECK_ROLE
};
}

BreakpointWidget::BreakpointWidget(QWidget* parent) : QDockWidget(parent)
{
  setWindowTitle(tr("Breakpoints"));
  setObjectName(QStringLiteral("breakpoints"));

  setHidden(!Settings::Instance().IsBreakpointsVisible() ||
            !Settings::Instance().IsDebugModeEnabled());

  setAllowedAreas(Qt::AllDockWidgetAreas);

  CreateWidgets();

  auto& settings = Settings::GetQSettings();

  restoreGeometry(settings.value(QStringLiteral("breakpointwidget/geometry")).toByteArray());
  // macOS: setHidden() needs to be evaluated before setFloating() for proper window presentation
  // according to Settings
  setFloating(settings.value(QStringLiteral("breakpointwidget/floating")).toBool());

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [this](Core::State state) {
    UpdateButtonsEnabled();
    if (state == Core::State::Uninitialized)
      Update();
  });

  connect(&Settings::Instance(), &Settings::BreakpointsVisibilityChanged, this,
          [this](bool visible) { setHidden(!visible); });

  connect(&Settings::Instance(), &Settings::DebugModeToggled, this, [this](bool enabled) {
    setHidden(!enabled || !Settings::Instance().IsBreakpointsVisible());
  });

  connect(&Settings::Instance(), &Settings::ThemeChanged, this, &BreakpointWidget::UpdateIcons);
  UpdateIcons();
}

BreakpointWidget::~BreakpointWidget()
{
  auto& settings = Settings::GetQSettings();

  settings.setValue(QStringLiteral("breakpointwidget/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("breakpointwidget/floating"), isFloating());
}

void BreakpointWidget::CreateWidgets()
{
  m_toolbar = new QToolBar;
  m_toolbar->setContentsMargins(0, 0, 0, 0);
  m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  m_table = new QTableWidget;
  m_table->setTabKeyNavigation(false);
  m_table->setContentsMargins(0, 0, 0, 0);
  m_table->setColumnCount(5);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->verticalHeader()->hide();

  connect(m_table, &QTableWidget::customContextMenuRequested, this,
          &BreakpointWidget::OnContextMenu);

  m_table->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);

  auto* layout = new QVBoxLayout;

  layout->addWidget(m_toolbar);
  layout->addWidget(m_table);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(0);

  m_new = m_toolbar->addAction(tr("New"), this, &BreakpointWidget::OnNewBreakpoint);
  m_delete = m_toolbar->addAction(tr("Delete"), this, &BreakpointWidget::OnDelete);
  m_clear = m_toolbar->addAction(tr("Clear"), this, &BreakpointWidget::OnClear);

  m_load = m_toolbar->addAction(tr("Load"), this, &BreakpointWidget::OnLoad);
  m_save = m_toolbar->addAction(tr("Save"), this, &BreakpointWidget::OnSave);

  m_new->setEnabled(false);
  m_load->setEnabled(false);
  m_save->setEnabled(false);

  QWidget* widget = new QWidget;
  widget->setLayout(layout);

  setWidget(widget);
}

void BreakpointWidget::UpdateIcons()
{
  m_new->setIcon(Resources::GetScaledThemeIcon("debugger_add_breakpoint"));
  m_delete->setIcon(Resources::GetScaledThemeIcon("debugger_delete"));
  m_clear->setIcon(Resources::GetScaledThemeIcon("debugger_clear"));
  m_load->setIcon(Resources::GetScaledThemeIcon("debugger_load"));
  m_save->setIcon(Resources::GetScaledThemeIcon("debugger_save"));
}

void BreakpointWidget::closeEvent(QCloseEvent*)
{
  Settings::Instance().SetBreakpointsVisible(false);
}

void BreakpointWidget::showEvent(QShowEvent* event)
{
  UpdateButtonsEnabled();
  Update();
}

void BreakpointWidget::UpdateButtonsEnabled()
{
  if (!isVisible())
    return;

  const bool is_initialised = Core::GetState() != Core::State::Uninitialized;
  m_new->setEnabled(is_initialised);
  m_load->setEnabled(is_initialised);
  m_save->setEnabled(is_initialised);
}

void BreakpointWidget::Update()
{
  if (!isVisible())
    return;

  m_table->clear();

  m_table->setHorizontalHeaderLabels(
      {tr("Active"), tr("Type"), tr("Function"), tr("Address"), tr("Flags")});

  int i = 0;
  m_table->setRowCount(i);

  const auto create_item = [](const QString string = {}) {
    QTableWidgetItem* item = new QTableWidgetItem(string);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
  };

  // Breakpoints
  for (const auto& bp : PowerPC::breakpoints.GetBreakPoints())
  {
    m_table->setRowCount(i + 1);

    auto* active = create_item(bp.is_enabled ? tr("on") : tr("off"));

    active->setData(ADDRESS_ROLE, bp.address);
    active->setData(IS_MEMCHECK_ROLE, false);

    m_table->setItem(i, 0, active);
    m_table->setItem(i, 1, create_item(QStringLiteral("BP")));

    if (g_symbolDB.GetSymbolFromAddr(bp.address))
    {
      m_table->setItem(i, 2,
                       create_item(QString::fromStdString(g_symbolDB.GetDescription(bp.address))));
    }

    m_table->setItem(i, 3,
                     create_item(QStringLiteral("%1").arg(bp.address, 8, 16, QLatin1Char('0'))));

    QString flags;

    if (bp.break_on_hit)
      flags.append(QLatin1Char{'b'});

    if (bp.log_on_hit)
      flags.append(QLatin1Char{'l'});

    m_table->setItem(i, 4, create_item(flags));

    i++;
  }

  // Memory Breakpoints
  for (const auto& mbp : PowerPC::memchecks.GetMemChecks())
  {
    m_table->setRowCount(i + 1);
    auto* active =
        create_item(mbp.is_enabled && (mbp.break_on_hit || mbp.log_on_hit) ? tr("on") : tr("off"));
    active->setData(ADDRESS_ROLE, mbp.start_address);
    active->setData(IS_MEMCHECK_ROLE, true);

    m_table->setItem(i, 0, active);
    m_table->setItem(i, 1, create_item(QStringLiteral("MBP")));

    if (g_symbolDB.GetSymbolFromAddr(mbp.start_address))
    {
      m_table->setItem(
          i, 2, create_item(QString::fromStdString(g_symbolDB.GetDescription(mbp.start_address))));
    }

    if (mbp.is_ranged)
    {
      m_table->setItem(i, 3,
                       create_item(QStringLiteral("%1 - %2")
                                       .arg(mbp.start_address, 8, 16, QLatin1Char('0'))
                                       .arg(mbp.end_address, 8, 16, QLatin1Char('0'))));
    }
    else
    {
      m_table->setItem(
          i, 3, create_item(QStringLiteral("%1").arg(mbp.start_address, 8, 16, QLatin1Char('0'))));
    }

    QString flags;

    if (mbp.is_break_on_read)
      flags.append(QLatin1Char{'r'});

    if (mbp.is_break_on_write)
      flags.append(QLatin1Char{'w'});

    m_table->setItem(i, 4, create_item(flags));

    i++;
  }
}

void BreakpointWidget::OnDelete()
{
  const auto selected_items = m_table->selectedItems();
  if (selected_items.empty())
    return;

  const auto item = selected_items.constFirst();
  const auto address = item->data(ADDRESS_ROLE).toUInt();
  const bool is_memcheck = item->data(IS_MEMCHECK_ROLE).toBool();

  if (is_memcheck)
  {
    const QSignalBlocker blocker(Settings::Instance());
    PowerPC::memchecks.Remove(address);
  }
  else
  {
    PowerPC::breakpoints.Remove(address);
  }

  emit BreakpointsChanged();
  Update();
}

void BreakpointWidget::OnClear()
{
  PowerPC::debug_interface.ClearAllBreakpoints();
  {
    const QSignalBlocker blocker(Settings::Instance());
    PowerPC::debug_interface.ClearAllMemChecks();
  }

  m_table->setRowCount(0);

  emit BreakpointsChanged();
  Update();
}

void BreakpointWidget::OnNewBreakpoint()
{
  NewBreakpointDialog* dialog = new NewBreakpointDialog(this);
  dialog->exec();
}

void BreakpointWidget::OnLoad()
{
  IniFile ini;
  if (!ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetGameID() + ".ini",
                false))
  {
    return;
  }

  BreakPoints::TBreakPointsStr new_bps;
  if (ini.GetLines("BreakPoints", &new_bps, false))
  {
    PowerPC::breakpoints.Clear();
    PowerPC::breakpoints.AddFromStrings(new_bps);
  }

  MemChecks::TMemChecksStr new_mcs;
  if (ini.GetLines("MemoryBreakPoints", &new_mcs, false))
  {
    PowerPC::memchecks.Clear();
    const QSignalBlocker blocker(Settings::Instance());
    PowerPC::memchecks.AddFromStrings(new_mcs);
  }

  emit BreakpointsChanged();
  Update();
}

void BreakpointWidget::OnSave()
{
  IniFile ini;
  ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetGameID() + ".ini",
           false);
  ini.SetLines("BreakPoints", PowerPC::breakpoints.GetStrings());
  ini.SetLines("MemoryBreakPoints", PowerPC::memchecks.GetStrings());
  ini.Save(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetGameID() + ".ini");
}

void BreakpointWidget::OnContextMenu()
{
  const auto& selected_items = m_table->selectedItems();
  if (selected_items.isEmpty())
  {
    return;
  }

  const auto& selected_item = selected_items.constFirst();
  const auto bp_address = static_cast<u32>(selected_item->data(ADDRESS_ROLE).toUInt());
  const auto is_memory_breakpoint = selected_item->data(IS_MEMCHECK_ROLE).toBool();

  auto* menu = new QMenu(this);

  if (!is_memory_breakpoint)
  {
    const auto& inst_breakpoints = PowerPC::breakpoints.GetBreakPoints();
    const auto bp_iter =
        std::find_if(inst_breakpoints.begin(), inst_breakpoints.end(),
                     [bp_address](const auto& bp) { return bp.address == bp_address; });
    if (bp_iter == inst_breakpoints.end())
      return;

    menu->addAction(bp_iter->is_enabled ? tr("Disable") : tr("Enable"), [this, &bp_address]() {
      PowerPC::breakpoints.ToggleBreakPoint(bp_address);

      emit BreakpointsChanged();
      Update();
    });
    menu->addAction(tr("Go to"), [this, bp_address] { emit SelectedBreakpoint(bp_address); });
  }
  else
  {
    const auto& memory_breakpoints = PowerPC::memchecks.GetMemChecks();
    const auto mb_iter =
        std::find_if(memory_breakpoints.begin(), memory_breakpoints.end(),
                     [bp_address](const auto& bp) { return bp.start_address == bp_address; });
    if (mb_iter == memory_breakpoints.end())
      return;

    menu->addAction(mb_iter->is_enabled ? tr("Disable") : tr("Enable"), [this, &bp_address]() {
      PowerPC::memchecks.ToggleBreakPoint(bp_address);

      emit BreakpointsChanged();
      Update();
    });
  }

  menu->exec(QCursor::pos());
}

void BreakpointWidget::AddBP(u32 addr)
{
  AddBP(addr, false, true, true);
}

void BreakpointWidget::AddBP(u32 addr, bool temp, bool break_on_hit, bool log_on_hit)
{
  PowerPC::breakpoints.Add(addr, temp, break_on_hit, log_on_hit);

  emit BreakpointsChanged();
  Update();
}

void BreakpointWidget::AddAddressMBP(u32 addr, bool on_read, bool on_write, bool do_log,
                                     bool do_break)
{
  TMemCheck check;

  check.start_address = addr;
  check.end_address = addr;
  check.is_ranged = false;
  check.is_break_on_read = on_read;
  check.is_break_on_write = on_write;
  check.log_on_hit = do_log;
  check.break_on_hit = do_break;

  {
    const QSignalBlocker blocker(Settings::Instance());
    PowerPC::memchecks.Add(check);
  }

  emit BreakpointsChanged();
  Update();
}

void BreakpointWidget::AddRangedMBP(u32 from, u32 to, bool on_read, bool on_write, bool do_log,
                                    bool do_break)
{
  TMemCheck check;

  check.start_address = from;
  check.end_address = to;
  check.is_ranged = true;
  check.is_break_on_read = on_read;
  check.is_break_on_write = on_write;
  check.log_on_hit = do_log;
  check.break_on_hit = do_break;

  {
    const QSignalBlocker blocker(Settings::Instance());
    PowerPC::memchecks.Add(check);
  }

  emit BreakpointsChanged();
  Update();
}
