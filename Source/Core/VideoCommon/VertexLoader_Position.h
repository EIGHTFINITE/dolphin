// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/CommonTypes.h"

class VertexLoader_Position
{
public:
	// GetSize
	static unsigned int GetSize(u64 _type, unsigned int _format, unsigned int _elements);

	// GetFunction
	static TPipelineFunction GetFunction(u64 _type, unsigned int _format, unsigned int _elements);
};

