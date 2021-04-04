// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

namespace Common::Debug
{
struct PartialContext
{
  std::optional<std::array<u32, 32>> gpr;
  std::optional<u32> cr;
  std::optional<u32> lr;
  std::optional<u32> ctr;
  std::optional<u32> xer;
  std::optional<std::array<double, 32>> fpr;
  std::optional<u64> fpscr;
  std::optional<u32> srr0;
  std::optional<u32> srr1;
  std::optional<u16> dummy;
  std::optional<u16> state;
  std::optional<std::array<u32, 8>> gqr;
  std::optional<std::array<double, 32>> psf;
};

class ThreadView
{
public:
  virtual ~ThreadView() = default;

  enum class API
  {
    OSThread,   // Nintendo SDK thread
    LWPThread,  // devkitPro libogc thread
  };

  virtual PartialContext GetContext() const = 0;
  virtual u32 GetAddress() const = 0;
  virtual u16 GetState() const = 0;
  virtual bool IsSuspended() const = 0;
  virtual bool IsDetached() const = 0;
  virtual s32 GetBasePriority() const = 0;
  virtual s32 GetEffectivePriority() const = 0;
  virtual u32 GetStackStart() const = 0;
  virtual u32 GetStackEnd() const = 0;
  virtual std::size_t GetStackSize() const = 0;
  virtual s32 GetErrno() const = 0;
  // Implementation specific, used to store arbitrary data
  virtual std::string GetSpecific() const = 0;
  virtual bool IsValid() const = 0;
};

using Threads = std::vector<std::unique_ptr<ThreadView>>;

}  // namespace Common::Debug
