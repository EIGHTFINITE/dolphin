// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/Null/NullVertexManager.h"

namespace Null
{
VertexManager::VertexManager() = default;

VertexManager::~VertexManager() = default;

void VertexManager::DrawCurrentBatch(u32 base_index, u32 num_indices, u32 base_vertex)
{
}

}  // namespace Null
