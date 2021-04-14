// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <ctime>
#include <memory>

#include "Common/CommonTypes.h"

struct FrameDumpContext;
class PointerWrap;

class FrameDump
{
public:
  FrameDump();
  ~FrameDump();

  // Holds relevant emulation state during a rendered frame for
  // when it is later asynchronously written.
  struct FrameState
  {
    u64 ticks = 0;
    int frame_number = 0;
    u32 savestate_index = 0;
    int refresh_rate_num = 0;
    int refresh_rate_den = 0;
  };

  struct FrameData
  {
    const u8* data;
    int width;
    int height;
    int stride;
    FrameState state;
  };

  bool Start(int w, int h, u64 start_ticks);
  void AddFrame(const FrameData&);
  void Stop();
  void DoState(PointerWrap&);
  bool IsStarted() const;
  FrameState FetchState(u64 ticks, int frame_number) const;

private:
  bool IsFirstFrameInCurrentFile() const;
  bool PrepareEncoding(int w, int h, u64 start_ticks, u32 savestate_index);
  bool CreateVideoFile();
  void CloseVideoFile();
  void CheckForConfigChange(const FrameData&);
  void ProcessPackets();

#if defined(HAVE_FFMPEG)
  std::unique_ptr<FrameDumpContext> m_context;
#endif

  // Used for FetchState:
  u32 m_savestate_index = 0;

  // Used for filename generation.
  std::time_t m_start_time = {};
  u32 m_file_index = 0;
};

#if !defined(HAVE_FFMPEG)
inline FrameDump::FrameDump() = default;
inline FrameDump::~FrameDump() = default;

inline FrameDump::FrameState FrameDump::FetchState(u64 ticks, int frame_number) const
{
  return {};
}
#endif
