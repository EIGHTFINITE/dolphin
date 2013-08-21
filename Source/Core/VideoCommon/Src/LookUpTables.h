// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _LOOKUPTABLES_H
#define _LOOKUPTABLES_H

#include "Common.h"

inline u8 Convert3To8(u8 v)
{
	// Swizzle bits: 00000123 -> 12312312
	return (v << 5) | (v << 2) | (v >> 1);
}

inline u8 Convert4To8(u8 v)
{
	// Swizzle bits: 00001234 -> 12341234
	return (v << 4) | v;
}

inline u8 Convert5To8(u8 v)
{
	// Swizzle bits: 00012345 -> 12345123
	return (v << 3) | (v >> 2);
}

inline u8 Convert6To8(u8 v)
{
	// Swizzle bits: 00123456 -> 12345612
	return (v << 2) | (v >> 4);
}

#endif // _LOOKUPTABLES_H
