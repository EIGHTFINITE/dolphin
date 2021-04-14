// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include <QDockWidget>

#include "Common/CommonTypes.h"

class MemoryViewWidget;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QShowEvent;
class QSplitter;

class MemoryWidget : public QDockWidget
{
  Q_OBJECT
public:
  explicit MemoryWidget(QWidget* parent = nullptr);
  ~MemoryWidget();

  void SetAddress(u32 address);
  void Update();
signals:
  void BreakpointsChanged();
  void ShowCode(u32 address);
  void RequestWatch(QString name, u32 address);

private:
  void CreateWidgets();
  void ConnectWidgets();

  void LoadSettings();
  void SaveSettings();

  void OnAddressSpaceChanged();
  void OnTypeChanged();
  void OnBPLogChanged();
  void OnBPTypeChanged();

  void OnSearchAddress();
  void OnFindNextValue();
  void OnFindPreviousValue();
  void ValidateSearchValue();

  void OnSetValue();

  void OnDumpMRAM();
  void OnDumpExRAM();
  void OnDumpARAM();
  void OnDumpFakeVMEM();

  std::vector<u8> GetValueData() const;

  void FindValue(bool next);

  void closeEvent(QCloseEvent*) override;
  void showEvent(QShowEvent* event) override;

  MemoryViewWidget* m_memory_view;
  QSplitter* m_splitter;
  QLineEdit* m_search_address;
  QLineEdit* m_search_offset;
  QLineEdit* m_data_edit;
  QPushButton* m_set_value;
  QPushButton* m_dump_mram;
  QPushButton* m_dump_exram;
  QPushButton* m_dump_aram;
  QPushButton* m_dump_fake_vmem;

  // Search
  QPushButton* m_find_next;
  QPushButton* m_find_previous;
  QRadioButton* m_find_ascii;
  QRadioButton* m_find_hex;
  QLabel* m_result_label;

  // Address Spaces
  QRadioButton* m_address_space_physical;
  QRadioButton* m_address_space_effective;
  QRadioButton* m_address_space_auxiliary;

  // Datatypes
  QRadioButton* m_type_u8;
  QRadioButton* m_type_u16;
  QRadioButton* m_type_u32;
  QRadioButton* m_type_ascii;
  QRadioButton* m_type_float;

  // Breakpoint options
  QRadioButton* m_bp_read_write;
  QRadioButton* m_bp_read_only;
  QRadioButton* m_bp_write_only;
  QCheckBox* m_bp_log_check;
};
