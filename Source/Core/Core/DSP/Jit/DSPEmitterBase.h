// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "Common/CommonTypes.h"

class PointerWrap;

namespace DSP
{
class DSPCore;
}

namespace DSP::JIT
{
class DSPEmitter
{
public:
  virtual ~DSPEmitter();

  virtual u16 RunCycles(u16 cycles) = 0;
  virtual void ClearIRAM() = 0;

  virtual void DoState(PointerWrap& p) = 0;
};

class DSPEmitterNull final : public DSPEmitter
{
public:
  u16 RunCycles(u16) override { return 0; }
  void ClearIRAM() override {}
  void DoState(PointerWrap&) override {}
};

std::unique_ptr<DSPEmitter> CreateDSPEmitter(DSPCore& dsp);
}  // namespace DSP::JIT
