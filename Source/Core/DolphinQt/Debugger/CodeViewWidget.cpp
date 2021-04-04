// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Debugger/CodeViewWidget.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTableWidgetItem>
#include <QWheelEvent>

#include "Common/GekkoDisassembler.h"
#include "Common/StringUtil.h"
#include "Core/Core.h"
#include "Core/Debugger/PPCDebugInterface.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "DolphinQt/Debugger/PatchInstructionDialog.h"
#include "DolphinQt/Host.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

struct CodeViewBranch
{
  u32 src_addr;
  u32 dst_addr;
  u32 indentation = 0;
  bool is_link;
};

constexpr u32 WIDTH_PER_BRANCH_ARROW = 16;

class BranchDisplayDelegate : public QStyledItemDelegate
{
public:
  BranchDisplayDelegate(CodeViewWidget* parent) : m_parent(parent) {}

private:
  CodeViewWidget* m_parent;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    QStyledItemDelegate::paint(painter, option, index);

    painter->save();
    painter->setClipRect(option.rect);
    painter->setPen(m_parent->palette().text().color());

    constexpr u32 x_offset_in_branch_for_vertical_line = 10;
    const u32 addr = m_parent->AddressForRow(index.row());
    for (const CodeViewBranch& branch : m_parent->m_branches)
    {
      const int y_center = option.rect.top() + option.rect.height() / 2;
      const int x_left = option.rect.left() + WIDTH_PER_BRANCH_ARROW * branch.indentation;
      const int x_right = x_left + x_offset_in_branch_for_vertical_line;

      if (branch.is_link)
      {
        // just draw an arrow pointing right from the branch instruction for link branches, they
        // rarely are close enough to actually see the target and are just visual noise otherwise
        if (addr == branch.src_addr)
        {
          painter->drawLine(x_left, y_center, x_right, y_center);
          painter->drawLine(x_right, y_center, x_right - 6, y_center - 3);
          painter->drawLine(x_right, y_center, x_right - 6, y_center + 3);
        }
      }
      else
      {
        const u32 addr_lower = std::min(branch.src_addr, branch.dst_addr);
        const u32 addr_higher = std::max(branch.src_addr, branch.dst_addr);
        const bool in_range = addr >= addr_lower && addr <= addr_higher;

        if (in_range)
        {
          const bool is_lowest = addr == addr_lower;
          const bool is_highest = addr == addr_higher;
          const int top = (is_lowest ? y_center : option.rect.top());
          const int bottom = (is_highest ? y_center : option.rect.bottom());

          // draw vertical part of the branch line
          painter->drawLine(x_right, top, x_right, bottom);

          if (is_lowest || is_highest)
          {
            // draw horizontal part of the branch line if this is either the source or destination
            painter->drawLine(x_left, y_center, x_right, y_center);
          }

          if (addr == branch.dst_addr)
          {
            // draw arrow if this is the destination address
            painter->drawLine(x_left, y_center, x_left + 6, y_center - 3);
            painter->drawLine(x_left, y_center, x_left + 6, y_center + 3);
          }
        }
      }
    }

    painter->restore();
  }
};

// "Most mouse types work in steps of 15 degrees, in which case the delta value is a multiple of
// 120; i.e., 120 units * 1/8 = 15 degrees." (http://doc.qt.io/qt-5/qwheelevent.html#angleDelta)
constexpr double SCROLL_FRACTION_DEGREES = 15.;

constexpr size_t VALID_BRANCH_LENGTH = 10;

constexpr int CODE_VIEW_COLUMN_BREAKPOINT = 0;
constexpr int CODE_VIEW_COLUMN_ADDRESS = 1;
constexpr int CODE_VIEW_COLUMN_INSTRUCTION = 2;
constexpr int CODE_VIEW_COLUMN_PARAMETERS = 3;
constexpr int CODE_VIEW_COLUMN_DESCRIPTION = 4;
constexpr int CODE_VIEW_COLUMN_BRANCH_ARROWS = 5;
constexpr int CODE_VIEW_COLUMNCOUNT = 6;

CodeViewWidget::CodeViewWidget()
{
  setColumnCount(CODE_VIEW_COLUMNCOUNT);
  setShowGrid(false);
  setContextMenuPolicy(Qt::CustomContextMenu);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

  verticalHeader()->hide();
  horizontalHeader()->setSectionResizeMode(CODE_VIEW_COLUMN_BREAKPOINT, QHeaderView::Fixed);
  horizontalHeader()->setStretchLastSection(true);
  setHorizontalHeaderItem(CODE_VIEW_COLUMN_BREAKPOINT, new QTableWidgetItem());
  setHorizontalHeaderItem(CODE_VIEW_COLUMN_ADDRESS, new QTableWidgetItem(tr("Address")));
  setHorizontalHeaderItem(CODE_VIEW_COLUMN_INSTRUCTION, new QTableWidgetItem(tr("Instr.")));
  setHorizontalHeaderItem(CODE_VIEW_COLUMN_PARAMETERS, new QTableWidgetItem(tr("Parameters")));
  setHorizontalHeaderItem(CODE_VIEW_COLUMN_DESCRIPTION, new QTableWidgetItem(tr("Symbols")));
  setHorizontalHeaderItem(CODE_VIEW_COLUMN_BRANCH_ARROWS, new QTableWidgetItem(tr("Branches")));

  setFont(Settings::Instance().GetDebugFont());
  setItemDelegateForColumn(CODE_VIEW_COLUMN_BRANCH_ARROWS, new BranchDisplayDelegate(this));

  FontBasedSizing();

  connect(this, &CodeViewWidget::customContextMenuRequested, this, &CodeViewWidget::OnContextMenu);
  connect(this, &CodeViewWidget::itemSelectionChanged, this, &CodeViewWidget::OnSelectionChanged);
  connect(&Settings::Instance(), &Settings::DebugFontChanged, this, &QWidget::setFont);
  connect(&Settings::Instance(), &Settings::DebugFontChanged, this,
          &CodeViewWidget::FontBasedSizing);

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [this] {
    m_address = PC;
    Update();
  });
  connect(Host::GetInstance(), &Host::UpdateDisasmDialog, this, [this] {
    m_address = PC;
    Update();
  });

  connect(&Settings::Instance(), &Settings::ThemeChanged, this, &CodeViewWidget::Update);
}

CodeViewWidget::~CodeViewWidget() = default;

static u32 GetBranchFromAddress(u32 addr)
{
  std::string disasm = PowerPC::debug_interface.Disassemble(addr);
  size_t pos = disasm.find("->0x");

  if (pos == std::string::npos)
    return 0;

  std::string hex = disasm.substr(pos + 2);
  return std::stoul(hex, nullptr, 16);
}

void CodeViewWidget::FontBasedSizing()
{
  // just text width is too small with some fonts, so increase by a bit
  constexpr int extra_text_width = 8;

  const QFontMetrics fm(Settings::Instance().GetDebugFont());

  const int rowh = fm.height() + 1;
  verticalHeader()->setMaximumSectionSize(rowh);
  horizontalHeader()->setMinimumSectionSize(rowh + 5);
  setColumnWidth(CODE_VIEW_COLUMN_BREAKPOINT, rowh + 5);
  setColumnWidth(CODE_VIEW_COLUMN_ADDRESS,
                 fm.boundingRect(QStringLiteral("80000000")).width() + extra_text_width);

  // The longest instruction is technically 'ps_merge00' (0x10000420u), but those instructions are
  // very rare and would needlessly increase the column size, so let's go with 'rlwinm.' instead.
  // Similarly, the longest parameter set is 'rtoc, rtoc, r10, 10, 10 (00000800)' (0x5c425294u),
  // but one is unlikely to encounter that in practice, so let's use a slightly more reasonable
  // 'r31, r31, 16, 16, 31 (ffff0000)'. The user can resize the columns as necessary anyway.
  const std::string disas = Common::GekkoDisassembler::Disassemble(0x57ff843fu, 0);
  const auto split = disas.find('\t');
  const std::string ins = (split == std::string::npos ? disas : disas.substr(0, split));
  const std::string param = (split == std::string::npos ? "" : disas.substr(split + 1));
  setColumnWidth(CODE_VIEW_COLUMN_INSTRUCTION,
                 fm.boundingRect(QString::fromStdString(ins)).width() + extra_text_width);
  setColumnWidth(CODE_VIEW_COLUMN_PARAMETERS,
                 fm.boundingRect(QString::fromStdString(param)).width() + extra_text_width);
  setColumnWidth(CODE_VIEW_COLUMN_DESCRIPTION,
                 fm.boundingRect(QChar(u'0')).width() * 25 + extra_text_width);

  Update();
}

u32 CodeViewWidget::AddressForRow(int row) const
{
  // m_address is defined as the center row of the table, so we have rowCount/2 instructions above
  // it; an instruction is 4 bytes long on GC/Wii so we increment 4 bytes per row
  const u32 row_zero_address = m_address - ((rowCount() / 2) * 4);
  return row_zero_address + row * 4;
}

static bool IsBranchInstructionWithLink(std::string_view ins)
{
  return StringEndsWith(ins, "l") || StringEndsWith(ins, "la") || StringEndsWith(ins, "l+") ||
         StringEndsWith(ins, "la+") || StringEndsWith(ins, "l-") || StringEndsWith(ins, "la-");
}

void CodeViewWidget::Update()
{
  if (!isVisible())
    return;

  if (m_updating)
    return;

  m_updating = true;

  clearSelection();
  if (rowCount() == 0)
    setRowCount(1);

  // Calculate (roughly) how many rows will fit in our table
  int rows = std::round((height() / static_cast<float>(rowHeight(0))) - 0.25);

  setRowCount(rows);

  const QFontMetrics fm(Settings::Instance().GetDebugFont());
  const int rowh = fm.height() + 1;

  for (int i = 0; i < rows; i++)
    setRowHeight(i, rowh);

  u32 pc = PowerPC::ppcState.pc;

  if (Core::GetState() != Core::State::Paused && PowerPC::debug_interface.IsBreakpoint(pc))
    Core::SetState(Core::State::Paused);

  const bool dark_theme = qApp->palette().color(QPalette::Base).valueF() < 0.5;

  m_branches.clear();

  for (int i = 0; i < rowCount(); i++)
  {
    const u32 addr = AddressForRow(i);
    const u32 color = PowerPC::debug_interface.GetColor(addr);
    auto* bp_item = new QTableWidgetItem;
    auto* addr_item = new QTableWidgetItem(QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));

    std::string disas = PowerPC::debug_interface.Disassemble(addr);
    auto split = disas.find('\t');

    std::string ins = (split == std::string::npos ? disas : disas.substr(0, split));
    std::string param = (split == std::string::npos ? "" : disas.substr(split + 1));
    std::string desc = PowerPC::debug_interface.GetDescription(addr);

    // Adds whitespace and a minimum size to ins and param. Helps to prevent frequent resizing while
    // scrolling.
    const QString ins_formatted =
        QStringLiteral("%1").arg(QString::fromStdString(ins), -7, QLatin1Char(' '));
    const QString param_formatted =
        QStringLiteral("%1").arg(QString::fromStdString(param), -19, QLatin1Char(' '));
    const QString desc_formatted = QStringLiteral("%1   ").arg(QString::fromStdString(desc));

    auto* ins_item = new QTableWidgetItem(ins_formatted);
    auto* param_item = new QTableWidgetItem(param_formatted);
    auto* description_item = new QTableWidgetItem(desc_formatted);
    auto* branch_item = new QTableWidgetItem();

    for (auto* item : {bp_item, addr_item, ins_item, param_item, description_item, branch_item})
    {
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      item->setData(Qt::UserRole, addr);

      if (addr == pc && item != bp_item)
      {
        item->setBackground(QColor(Qt::green));
        item->setForeground(QColor(Qt::black));
      }
      else if (color != 0xFFFFFF)
      {
        item->setBackground(dark_theme ? QColor(color).darker(240) : QColor(color));
      }
    }

    // look for hex strings to decode branches
    std::string hex_str;
    size_t pos = param.find("0x");
    if (pos != std::string::npos)
    {
      hex_str = param.substr(pos);
    }

    if (hex_str.length() == VALID_BRANCH_LENGTH && desc != "---")
    {
      u32 branch_addr = GetBranchFromAddress(addr);
      CodeViewBranch& branch = m_branches.emplace_back();
      branch.src_addr = addr;
      branch.dst_addr = branch_addr;
      branch.is_link = IsBranchInstructionWithLink(ins);

      description_item->setText(tr("--> %1").arg(
          QString::fromStdString(PowerPC::debug_interface.GetDescription(branch_addr))));
      param_item->setForeground(Qt::magenta);
    }

    if (ins == "blr")
      ins_item->setForeground(dark_theme ? QColor(0xa0FFa0) : Qt::darkGreen);

    if (PowerPC::debug_interface.IsBreakpoint(addr))
    {
      auto icon =
          Resources::GetScaledThemeIcon("debugger_breakpoint").pixmap(QSize(rowh - 2, rowh - 2));
      if (!PowerPC::breakpoints.IsBreakPointEnable(addr))
      {
        QPixmap disabled_icon(icon.size());
        disabled_icon.fill(Qt::transparent);
        QPainter p(&disabled_icon);
        p.setOpacity(0.20);
        p.drawPixmap(0, 0, icon);
        p.end();
        icon = disabled_icon;
      }
      bp_item->setData(Qt::DecorationRole, icon);
    }

    setItem(i, CODE_VIEW_COLUMN_BREAKPOINT, bp_item);
    setItem(i, CODE_VIEW_COLUMN_ADDRESS, addr_item);
    setItem(i, CODE_VIEW_COLUMN_INSTRUCTION, ins_item);
    setItem(i, CODE_VIEW_COLUMN_PARAMETERS, param_item);
    setItem(i, CODE_VIEW_COLUMN_DESCRIPTION, description_item);
    setItem(i, CODE_VIEW_COLUMN_BRANCH_ARROWS, branch_item);

    if (addr == GetAddress())
    {
      selectRow(addr_item->row());
    }
  }

  CalculateBranchIndentation();

  g_symbolDB.FillInCallers();

  repaint();
  m_updating = false;
}

void CodeViewWidget::CalculateBranchIndentation()
{
  const size_t rows = rowCount();
  const size_t columns = m_branches.size();
  if (rows < 1 || columns < 1)
    return;

  // process in order of how much vertical space the drawn arrow would take up
  // so shorter arrows go further to the left
  const auto priority = [](const CodeViewBranch& b) {
    return b.is_link ? 0 : (std::max(b.src_addr, b.dst_addr) - std::min(b.src_addr, b.dst_addr));
  };
  std::stable_sort(m_branches.begin(), m_branches.end(),
                   [&priority](const CodeViewBranch& lhs, const CodeViewBranch& rhs) {
                     return priority(lhs) < priority(rhs);
                   });

  // build a 2D lookup table representing the columns and rows the arrow could be drawn in
  // and try to place all branch arrows in it as far left as possible
  std::vector<bool> arrow_space_used(columns * rows, false);
  const auto index = [&](u32 column, u32 row) { return column * rows + row; };
  const u32 first_visible_addr = AddressForRow(0);
  const u32 last_visible_addr = AddressForRow(static_cast<int>(rows - 1));
  for (CodeViewBranch& branch : m_branches)
  {
    const u32 arrow_src_addr = branch.src_addr;
    const u32 arrow_dst_addr = branch.is_link ? branch.src_addr : branch.dst_addr;
    const u32 arrow_addr_lower = std::min(arrow_src_addr, arrow_dst_addr);
    const u32 arrow_addr_higher = std::max(arrow_src_addr, arrow_dst_addr);
    const bool is_visible =
        last_visible_addr >= arrow_addr_lower || first_visible_addr <= arrow_addr_higher;
    if (!is_visible)
      continue;

    const u32 arrow_first_visible_addr =
        std::clamp(arrow_addr_lower, first_visible_addr, last_visible_addr);
    const u32 arrow_last_visible_addr =
        std::clamp(arrow_addr_higher, first_visible_addr, last_visible_addr);
    const u32 arrow_first_visible_row = (arrow_first_visible_addr - first_visible_addr) / 4;
    const u32 arrow_last_visible_row = (arrow_last_visible_addr - first_visible_addr) / 4;

    const auto free_column = [&]() -> std::optional<u32> {
      for (u32 column = 0; column < columns; ++column)
      {
        const bool column_is_free = [&] {
          for (u32 row = arrow_first_visible_row; row <= arrow_last_visible_row; ++row)
          {
            if (arrow_space_used[index(column, row)])
              return false;
          }
          return true;
        }();
        if (column_is_free)
          return column;
      }
      return std::nullopt;
    }();

    if (!free_column)
      continue;

    branch.indentation = *free_column;
    for (u32 row = arrow_first_visible_row; row <= arrow_last_visible_row; ++row)
      arrow_space_used[index(*free_column, row)] = true;
  }
}

u32 CodeViewWidget::GetAddress() const
{
  return m_address;
}

void CodeViewWidget::SetAddress(u32 address, SetAddressUpdate update)
{
  if (m_address == address)
    return;

  m_address = address;
  switch (update)
  {
  case SetAddressUpdate::WithoutUpdate:
    return;
  case SetAddressUpdate::WithUpdate:
    // Update the CodeViewWidget
    Update();
    break;
  case SetAddressUpdate::WithDetailedUpdate:
    // Update the CodeWidget's views (code view, function calls/callers, ...)
    emit UpdateCodeWidget();
    break;
  }
}

void CodeViewWidget::ReplaceAddress(u32 address, ReplaceWith replace)
{
  PowerPC::debug_interface.UnsetPatch(address);
  PowerPC::debug_interface.SetPatch(address, replace == ReplaceWith::BLR ? 0x4e800020 : 0x60000000);
  Update();
}

void CodeViewWidget::OnContextMenu()
{
  QMenu* menu = new QMenu(this);

  bool running = Core::GetState() != Core::State::Uninitialized;

  const u32 addr = GetContextAddress();

  bool has_symbol = g_symbolDB.GetSymbolFromAddr(addr);

  auto* follow_branch_action =
      menu->addAction(tr("Follow &branch"), this, &CodeViewWidget::OnFollowBranch);

  menu->addSeparator();

  menu->addAction(tr("&Copy address"), this, &CodeViewWidget::OnCopyAddress);
  auto* copy_address_action =
      menu->addAction(tr("Copy &function"), this, &CodeViewWidget::OnCopyFunction);
  auto* copy_line_action =
      menu->addAction(tr("Copy code &line"), this, &CodeViewWidget::OnCopyCode);
  auto* copy_hex_action = menu->addAction(tr("Copy &hex"), this, &CodeViewWidget::OnCopyHex);

  menu->addAction(tr("Show in &memory"), this, &CodeViewWidget::OnShowInMemory);

  menu->addSeparator();

  auto* symbol_rename_action =
      menu->addAction(tr("&Rename symbol"), this, &CodeViewWidget::OnRenameSymbol);
  auto* symbol_size_action =
      menu->addAction(tr("Set symbol &size"), this, &CodeViewWidget::OnSetSymbolSize);
  auto* symbol_end_action =
      menu->addAction(tr("Set symbol &end address"), this, &CodeViewWidget::OnSetSymbolEndAddress);
  menu->addSeparator();

  menu->addAction(tr("Run &To Here"), this, &CodeViewWidget::OnRunToHere);
  auto* function_action =
      menu->addAction(tr("&Add function"), this, &CodeViewWidget::OnAddFunction);
  auto* ppc_action = menu->addAction(tr("PPC vs Host"), this, &CodeViewWidget::OnPPCComparison);
  auto* insert_blr_action = menu->addAction(tr("&Insert blr"), this, &CodeViewWidget::OnInsertBLR);
  auto* insert_nop_action = menu->addAction(tr("Insert &nop"), this, &CodeViewWidget::OnInsertNOP);
  auto* replace_action =
      menu->addAction(tr("Re&place instruction"), this, &CodeViewWidget::OnReplaceInstruction);
  auto* restore_action =
      menu->addAction(tr("Restore instruction"), this, &CodeViewWidget::OnRestoreInstruction);

  follow_branch_action->setEnabled(running && GetBranchFromAddress(addr));

  for (auto* action : {copy_address_action, copy_line_action, copy_hex_action, function_action,
                       ppc_action, insert_blr_action, insert_nop_action, replace_action})
    action->setEnabled(running);

  for (auto* action : {symbol_rename_action, symbol_size_action, symbol_end_action})
    action->setEnabled(has_symbol);

  restore_action->setEnabled(running && PowerPC::debug_interface.HasEnabledPatch(addr));

  menu->exec(QCursor::pos());
  Update();
}

void CodeViewWidget::OnCopyAddress()
{
  const u32 addr = GetContextAddress();

  QApplication::clipboard()->setText(QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
}

void CodeViewWidget::OnShowInMemory()
{
  emit ShowMemory(GetContextAddress());
}

void CodeViewWidget::OnCopyCode()
{
  const u32 addr = GetContextAddress();

  QApplication::clipboard()->setText(
      QString::fromStdString(PowerPC::debug_interface.Disassemble(addr)));
}

void CodeViewWidget::OnCopyFunction()
{
  const u32 address = GetContextAddress();

  const Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(address);
  if (!symbol)
    return;

  std::string text = symbol->name + "\r\n";
  // we got a function
  const u32 start = symbol->address;
  const u32 end = start + symbol->size;
  for (u32 addr = start; addr != end; addr += 4)
  {
    const std::string disasm = PowerPC::debug_interface.Disassemble(addr);
    text += StringFromFormat("%08x: ", addr) + disasm + "\r\n";
  }

  QApplication::clipboard()->setText(QString::fromStdString(text));
}

void CodeViewWidget::OnCopyHex()
{
  const u32 addr = GetContextAddress();
  const u32 instruction = PowerPC::debug_interface.ReadInstruction(addr);

  QApplication::clipboard()->setText(
      QStringLiteral("%1").arg(instruction, 8, 16, QLatin1Char('0')));
}

void CodeViewWidget::OnRunToHere()
{
  const u32 addr = GetContextAddress();

  PowerPC::debug_interface.SetBreakpoint(addr);
  PowerPC::debug_interface.RunToBreakpoint();
  Update();
}

void CodeViewWidget::OnPPCComparison()
{
  const u32 addr = GetContextAddress();

  emit RequestPPCComparison(addr);
}

void CodeViewWidget::OnAddFunction()
{
  const u32 addr = GetContextAddress();

  g_symbolDB.AddFunction(addr);
  emit SymbolsChanged();
  Update();
}

void CodeViewWidget::OnInsertBLR()
{
  const u32 addr = GetContextAddress();

  ReplaceAddress(addr, ReplaceWith::BLR);
}

void CodeViewWidget::OnInsertNOP()
{
  const u32 addr = GetContextAddress();

  ReplaceAddress(addr, ReplaceWith::NOP);
}

void CodeViewWidget::OnFollowBranch()
{
  const u32 addr = GetContextAddress();

  u32 branch_addr = GetBranchFromAddress(addr);

  if (!branch_addr)
    return;

  SetAddress(branch_addr, SetAddressUpdate::WithDetailedUpdate);
}

void CodeViewWidget::OnRenameSymbol()
{
  const u32 addr = GetContextAddress();

  Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(addr);

  if (!symbol)
    return;

  bool good;
  QString name =
      QInputDialog::getText(this, tr("Rename symbol"), tr("Symbol name:"), QLineEdit::Normal,
                            QString::fromStdString(symbol->name), &good);

  if (good && !name.isEmpty())
  {
    symbol->Rename(name.toStdString());
    emit SymbolsChanged();
    Update();
  }
}

void CodeViewWidget::OnSelectionChanged()
{
  if (m_address == PowerPC::ppcState.pc)
  {
    setStyleSheet(
        QStringLiteral("QTableView::item:selected {background-color: #00FF00; color: #000000;}"));
  }
  else if (!styleSheet().isEmpty())
  {
    setStyleSheet(QString{});
  }
}

void CodeViewWidget::OnSetSymbolSize()
{
  const u32 addr = GetContextAddress();

  Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(addr);

  if (!symbol)
    return;

  bool good;
  int size =
      QInputDialog::getInt(this, tr("Rename symbol"),
                           tr("Set symbol size (%1):").arg(QString::fromStdString(symbol->name)),
                           symbol->size, 1, 0xFFFF, 1, &good);

  if (!good)
    return;

  PPCAnalyst::ReanalyzeFunction(symbol->address, *symbol, size);
  emit SymbolsChanged();
  Update();
}

void CodeViewWidget::OnSetSymbolEndAddress()
{
  const u32 addr = GetContextAddress();

  Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(addr);

  if (!symbol)
    return;

  bool good;
  QString name = QInputDialog::getText(
      this, tr("Set symbol end address"),
      tr("Symbol (%1) end address:").arg(QString::fromStdString(symbol->name)), QLineEdit::Normal,
      QStringLiteral("%1").arg(addr + symbol->size, 8, 16, QLatin1Char('0')), &good);

  u32 address = name.toUInt(&good, 16);

  if (!good)
    return;

  PPCAnalyst::ReanalyzeFunction(symbol->address, *symbol, address - symbol->address);
  emit SymbolsChanged();
  Update();
}

void CodeViewWidget::OnReplaceInstruction()
{
  const u32 addr = GetContextAddress();

  if (!PowerPC::HostIsInstructionRAMAddress(addr))
    return;

  const PowerPC::TryReadInstResult read_result = PowerPC::TryReadInstruction(addr);
  if (!read_result.valid)
    return;

  PatchInstructionDialog dialog(this, addr, PowerPC::debug_interface.ReadInstruction(addr));

  if (dialog.exec() == QDialog::Accepted)
  {
    PowerPC::debug_interface.UnsetPatch(addr);
    PowerPC::debug_interface.SetPatch(addr, dialog.GetCode());
    Update();
  }
}

void CodeViewWidget::OnRestoreInstruction()
{
  const u32 addr = GetContextAddress();

  PowerPC::debug_interface.UnsetPatch(addr);
  Update();
}

void CodeViewWidget::resizeEvent(QResizeEvent*)
{
  Update();
}

void CodeViewWidget::keyPressEvent(QKeyEvent* event)
{
  switch (event->key())
  {
  case Qt::Key_Up:
    m_address -= sizeof(u32);
    Update();
    return;
  case Qt::Key_Down:
    m_address += sizeof(u32);
    Update();
    return;
  case Qt::Key_PageUp:
    m_address -= rowCount() * sizeof(u32);
    Update();
    return;
  case Qt::Key_PageDown:
    m_address += rowCount() * sizeof(u32);
    Update();
    return;
  default:
    QWidget::keyPressEvent(event);
    break;
  }
}

void CodeViewWidget::wheelEvent(QWheelEvent* event)
{
  auto delta =
      -static_cast<int>(std::round((event->angleDelta().y() / (SCROLL_FRACTION_DEGREES * 8))));

  if (delta == 0)
    return;

  m_address += delta * sizeof(u32);
  Update();
}

void CodeViewWidget::mousePressEvent(QMouseEvent* event)
{
  auto* item = itemAt(event->pos());
  if (item == nullptr)
    return;

  const u32 addr = item->data(Qt::UserRole).toUInt();

  m_context_address = addr;

  switch (event->button())
  {
  case Qt::LeftButton:
    if (column(item) == CODE_VIEW_COLUMN_BREAKPOINT)
      ToggleBreakpoint();
    else
      SetAddress(addr, SetAddressUpdate::WithDetailedUpdate);

    Update();
    break;
  default:
    break;
  }
}

void CodeViewWidget::showEvent(QShowEvent* event)
{
  Update();
}

void CodeViewWidget::ToggleBreakpoint()
{
  if (PowerPC::debug_interface.IsBreakpoint(GetContextAddress()))
    PowerPC::breakpoints.Remove(GetContextAddress());
  else
    PowerPC::breakpoints.Add(GetContextAddress());

  emit BreakpointsChanged();
  Update();
}

void CodeViewWidget::AddBreakpoint()
{
  PowerPC::breakpoints.Add(GetContextAddress());

  emit BreakpointsChanged();
  Update();
}

u32 CodeViewWidget::GetContextAddress() const
{
  return m_context_address;
}
