// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "Common/Event.h"
#include "Common/Flag.h"
#include "Core/HW/GCMemcard/GCMemcard.h"
#include "Core/HW/GCMemcard/GCMemcardBase.h"

class PointerWrap;

class MemoryCard : public MemoryCardBase
{
public:
  MemoryCard(const std::string& filename, int card_index,
             u16 size_mbits = Memcard::MBIT_SIZE_MEMORY_CARD_2043);
  ~MemoryCard();
  static void CheckPath(std::string& memcardPath, const std::string& gameRegion, bool isSlotA);
  void FlushThread();
  void MakeDirty();

  s32 Read(u32 src_address, s32 length, u8* dest_address) override;
  s32 Write(u32 dest_address, s32 length, const u8* src_address) override;
  void ClearBlock(u32 address) override;
  void ClearAll() override;
  void DoState(PointerWrap& p) override;

private:
  std::string m_filename;
  std::unique_ptr<u8[]> m_memcard_data;
  std::unique_ptr<u8[]> m_flush_buffer;
  std::thread m_flush_thread;
  std::mutex m_flush_mutex;
  Common::Event m_flush_trigger;
  Common::Flag m_dirty;
};
