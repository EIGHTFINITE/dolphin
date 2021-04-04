// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstddef>
#include <cstdlib>
#include <string>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/MsgHandler.h"

#ifdef _WIN32
#include <windows.h>
#include "Common/StringUtil.h"
#else
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#if defined __APPLE__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __NetBSD__
#include <sys/sysctl.h>
#elif defined __HAIKU__
#include <OS.h>
#else
#include <sys/sysinfo.h>
#endif
#endif

namespace Common
{
// This is purposely not a full wrapper for virtualalloc/mmap, but it
// provides exactly the primitive operations that Dolphin needs.

void* AllocateExecutableMemory(size_t size)
{
#if defined(_WIN32)
  void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
  void* ptr =
      mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);

  if (ptr == MAP_FAILED)
    ptr = nullptr;
#endif

  if (ptr == nullptr)
    PanicAlertFmt("Failed to allocate executable memory");

  return ptr;
}

void* AllocateMemoryPages(size_t size)
{
#ifdef _WIN32
  void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
#else
  void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

  if (ptr == MAP_FAILED)
    ptr = nullptr;
#endif

  if (ptr == nullptr)
    PanicAlertFmt("Failed to allocate raw memory");

  return ptr;
}

void* AllocateAlignedMemory(size_t size, size_t alignment)
{
#ifdef _WIN32
  void* ptr = _aligned_malloc(size, alignment);
#else
  void* ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0)
    ERROR_LOG_FMT(MEMMAP, "Failed to allocate aligned memory");
#endif

  if (ptr == nullptr)
    PanicAlertFmt("Failed to allocate aligned memory");

  return ptr;
}

void FreeMemoryPages(void* ptr, size_t size)
{
  if (ptr)
  {
#ifdef _WIN32
    if (!VirtualFree(ptr, 0, MEM_RELEASE))
      PanicAlertFmt("FreeMemoryPages failed!\nVirtualFree: {}", GetLastErrorString());
#else
    if (munmap(ptr, size) != 0)
      PanicAlertFmt("FreeMemoryPages failed!\nmunmap: {}", LastStrerrorString());
#endif
  }
}

void FreeAlignedMemory(void* ptr)
{
  if (ptr)
  {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }
}

void ReadProtectMemory(void* ptr, size_t size)
{
#ifdef _WIN32
  DWORD oldValue;
  if (!VirtualProtect(ptr, size, PAGE_NOACCESS, &oldValue))
    PanicAlertFmt("ReadProtectMemory failed!\nVirtualProtect: {}", GetLastErrorString());
#else
  if (mprotect(ptr, size, PROT_NONE) != 0)
    PanicAlertFmt("ReadProtectMemory failed!\nmprotect: {}", LastStrerrorString());
#endif
}

void WriteProtectMemory(void* ptr, size_t size, bool allowExecute)
{
#ifdef _WIN32
  DWORD oldValue;
  if (!VirtualProtect(ptr, size, allowExecute ? PAGE_EXECUTE_READ : PAGE_READONLY, &oldValue))
    PanicAlertFmt("WriteProtectMemory failed!\nVirtualProtect: {}", GetLastErrorString());
#else
  if (mprotect(ptr, size, allowExecute ? (PROT_READ | PROT_EXEC) : PROT_READ) != 0)
    PanicAlertFmt("WriteProtectMemory failed!\nmprotect: {}", LastStrerrorString());
#endif
}

void UnWriteProtectMemory(void* ptr, size_t size, bool allowExecute)
{
#ifdef _WIN32
  DWORD oldValue;
  if (!VirtualProtect(ptr, size, allowExecute ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &oldValue))
    PanicAlertFmt("UnWriteProtectMemory failed!\nVirtualProtect: {}", GetLastErrorString());
#else
  if (mprotect(ptr, size,
               allowExecute ? (PROT_READ | PROT_WRITE | PROT_EXEC) : PROT_WRITE | PROT_READ) != 0)
  {
    PanicAlertFmt("UnWriteProtectMemory failed!\nmprotect: {}", LastStrerrorString());
  }
#endif
}

size_t MemPhysical()
{
#ifdef _WIN32
  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);
  GlobalMemoryStatusEx(&memInfo);
  return memInfo.ullTotalPhys;
#elif defined __APPLE__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __NetBSD__
  int mib[2];
  size_t physical_memory;
  mib[0] = CTL_HW;
#ifdef __APPLE__
  mib[1] = HW_MEMSIZE;
#elif defined __FreeBSD__
  mib[1] = HW_REALMEM;
#elif defined __OpenBSD__ || defined __NetBSD__
  mib[1] = HW_PHYSMEM64;
#endif
  size_t length = sizeof(size_t);
  sysctl(mib, 2, &physical_memory, &length, NULL, 0);
  return physical_memory;
#elif defined __HAIKU__
  system_info sysinfo;
  get_system_info(&sysinfo);
  return static_cast<size_t>(sysinfo.max_pages * B_PAGE_SIZE);
#else
  struct sysinfo memInfo;
  sysinfo(&memInfo);
  return (size_t)memInfo.totalram * memInfo.mem_unit;
#endif
}

}  // namespace Common
