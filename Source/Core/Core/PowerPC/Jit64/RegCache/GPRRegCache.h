// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"

class Jit64;

class GPRRegCache final : public RegCache
{
public:
  explicit GPRRegCache(Jit64& jit);
  void SetImmediate32(preg_t preg, u32 imm_value, bool dirty = true);

protected:
  Gen::OpArg GetDefaultLocation(preg_t preg) const override;
  void StoreRegister(preg_t preg, const Gen::OpArg& new_loc) override;
  void LoadRegister(preg_t preg, Gen::X64Reg new_loc) override;
  const Gen::X64Reg* GetAllocationOrder(size_t* count) const override;
  BitSet32 GetRegUtilization() const override;
  BitSet32 CountRegsIn(preg_t preg, u32 lookahead) const override;
};
