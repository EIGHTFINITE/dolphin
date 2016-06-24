// Copyright 2012 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <memory>
#include "VideoCommon/PerfQueryBase.h"
#include "VideoCommon/VideoConfig.h"

std::unique_ptr<PerfQueryBase> g_perf_query;

bool PerfQueryBase::ShouldEmulate()
{
	return g_ActiveConfig.bPerfQueriesEnable;
}
