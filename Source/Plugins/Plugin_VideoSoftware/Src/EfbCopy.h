// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _EFB_COPY_H_
#define _EFB_COPY_H_


#include "Common.h"

namespace EfbCopy
{
	// Copy the EFB to RAM as a texture format or XFB
	// Clear the EFB if needed
	void CopyEfb();
}


#endif
