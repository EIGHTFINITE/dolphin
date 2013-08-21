// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"

// Neither Android nor OS X support TLS
#if  defined(__APPLE__) || (ANDROID && __clang__)
#define __thread
#endif

// Generic function to get last error message.
// Call directly after the command or use the error num.
// This function might change the error code.
const char* GetLastErrorMsg()
{
	static const size_t buff_size = 255;

#ifdef _WIN32
	static __declspec(thread) char err_str[buff_size] = {};

	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		err_str, buff_size, NULL);
#else
	static __thread char err_str[buff_size] = {};

	// Thread safe (XSI-compliant)
	strerror_r(errno, err_str, buff_size);
#endif

	return err_str;
}
