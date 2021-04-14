// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/GL/GLContext.h"

class BWindow;
class BGLView;

class GLContextBGL final : public GLContext
{
public:
  ~GLContextBGL() override;

  bool IsHeadless() const override;

  bool MakeCurrent() override;
  bool ClearCurrent() override;

  void Update() override;

  void Swap() override;

  void* GetFuncAddress(const std::string& name) override;

protected:
  bool Initialize(const WindowSystemInfo& wsi, bool stereo, bool core) override;

private:
  static BGLView* s_current;

  BWindow* m_window;
  BGLView* m_gl;
};
