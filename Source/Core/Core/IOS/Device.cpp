// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/IOS/Device.h"

#include <algorithm>
#include <map>

#include "Common/Assert.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SystemTimers.h"
#include "Core/IOS/IOS.h"

namespace IOS::HLE
{
Request::Request(const u32 address_) : address(address_)
{
  command = static_cast<IPCCommandType>(Memory::Read_U32(address));
  fd = Memory::Read_U32(address + 8);
}

OpenRequest::OpenRequest(const u32 address_) : Request(address_)
{
  path = Memory::GetString(Memory::Read_U32(address + 0xc));
  flags = static_cast<OpenMode>(Memory::Read_U32(address + 0x10));
  const Kernel* ios = GetIOS();
  if (ios)
  {
    uid = ios->GetUidForPPC();
    gid = ios->GetGidForPPC();
  }
}

ReadWriteRequest::ReadWriteRequest(const u32 address_) : Request(address_)
{
  buffer = Memory::Read_U32(address + 0xc);
  size = Memory::Read_U32(address + 0x10);
}

SeekRequest::SeekRequest(const u32 address_) : Request(address_)
{
  offset = Memory::Read_U32(address + 0xc);
  mode = static_cast<SeekMode>(Memory::Read_U32(address + 0x10));
}

IOCtlRequest::IOCtlRequest(const u32 address_) : Request(address_)
{
  request = Memory::Read_U32(address + 0x0c);
  buffer_in = Memory::Read_U32(address + 0x10);
  buffer_in_size = Memory::Read_U32(address + 0x14);
  buffer_out = Memory::Read_U32(address + 0x18);
  buffer_out_size = Memory::Read_U32(address + 0x1c);
}

IOCtlVRequest::IOCtlVRequest(const u32 address_) : Request(address_)
{
  request = Memory::Read_U32(address + 0x0c);
  const u32 in_number = Memory::Read_U32(address + 0x10);
  const u32 out_number = Memory::Read_U32(address + 0x14);
  const u32 vectors_base = Memory::Read_U32(address + 0x18);  // address to vectors

  u32 offset = 0;
  for (size_t i = 0; i < (in_number + out_number); ++i)
  {
    IOVector vector;
    vector.address = Memory::Read_U32(vectors_base + offset);
    vector.size = Memory::Read_U32(vectors_base + offset + 4);
    offset += 8;
    if (i < in_number)
      in_vectors.emplace_back(vector);
    else
      io_vectors.emplace_back(vector);
  }
}

const IOCtlVRequest::IOVector* IOCtlVRequest::GetVector(size_t index) const
{
  if (index >= in_vectors.size() + io_vectors.size())
    return nullptr;
  if (index < in_vectors.size())
    return &in_vectors[index];
  return &io_vectors[index - in_vectors.size()];
}

bool IOCtlVRequest::HasNumberOfValidVectors(const size_t in_count, const size_t io_count) const
{
  if (in_vectors.size() != in_count || io_vectors.size() != io_count)
    return false;

  auto IsValidVector = [](const auto& vector) { return vector.size == 0 || vector.address != 0; };
  return std::all_of(in_vectors.begin(), in_vectors.end(), IsValidVector) &&
         std::all_of(io_vectors.begin(), io_vectors.end(), IsValidVector);
}

void IOCtlRequest::Log(std::string_view device_name, Common::Log::LOG_TYPE type,
                       Common::Log::LOG_LEVELS verbosity) const
{
  GENERIC_LOG_FMT(type, verbosity, "{} (fd {}) - IOCtl {:#x} (in_size={:#x}, out_size={:#x})",
                  device_name, fd, request, buffer_in_size, buffer_out_size);
}

void IOCtlRequest::Dump(const std::string& description, Common::Log::LOG_TYPE type,
                        Common::Log::LOG_LEVELS level) const
{
  Log("===== " + description, type, level);
  GENERIC_LOG_FMT(type, level, "In buffer\n{}",
                  HexDump(Memory::GetPointer(buffer_in), buffer_in_size));
  GENERIC_LOG_FMT(type, level, "Out buffer\n{}",
                  HexDump(Memory::GetPointer(buffer_out), buffer_out_size));
}

void IOCtlRequest::DumpUnknown(const std::string& description, Common::Log::LOG_TYPE type,
                               Common::Log::LOG_LEVELS level) const
{
  Dump("Unknown IOCtl - " + description, type, level);
}

void IOCtlVRequest::Dump(std::string_view description, Common::Log::LOG_TYPE type,
                         Common::Log::LOG_LEVELS level) const
{
  GENERIC_LOG_FMT(type, level, "===== {} (fd {}) - IOCtlV {:#x} ({} in, {} io)", description, fd,
                  request, in_vectors.size(), io_vectors.size());

  size_t i = 0;
  for (const auto& vector : in_vectors)
  {
    GENERIC_LOG_FMT(type, level, "in[{}] (size={:#x}):\n{}", i++, vector.size,
                    HexDump(Memory::GetPointer(vector.address), vector.size));
  }

  i = 0;
  for (const auto& vector : io_vectors)
    GENERIC_LOG_FMT(type, level, "io[{}] (size={:#x})", i++, vector.size);
}

void IOCtlVRequest::DumpUnknown(const std::string& description, Common::Log::LOG_TYPE type,
                                Common::Log::LOG_LEVELS level) const
{
  Dump("Unknown IOCtlV - " + description, type, level);
}

Device::Device(Kernel& ios, const std::string& device_name, const DeviceType type)
    : m_ios(ios), m_name(device_name), m_device_type(type)
{
}

void Device::DoState(PointerWrap& p)
{
  DoStateShared(p);
  p.Do(m_is_active);
}

void Device::DoStateShared(PointerWrap& p)
{
  p.Do(m_name);
  p.Do(m_device_type);
  p.Do(m_is_active);
}

std::optional<IPCReply> Device::Open(const OpenRequest& request)
{
  m_is_active = true;
  return IPCReply{IPC_SUCCESS};
}

std::optional<IPCReply> Device::Close(u32 fd)
{
  m_is_active = false;
  return IPCReply{IPC_SUCCESS};
}

std::optional<IPCReply> Device::Unsupported(const Request& request)
{
  static const std::map<IPCCommandType, std::string_view> names{{
      {IPC_CMD_READ, "Read"},
      {IPC_CMD_WRITE, "Write"},
      {IPC_CMD_SEEK, "Seek"},
      {IPC_CMD_IOCTL, "IOCtl"},
      {IPC_CMD_IOCTLV, "IOCtlV"},
  }};

  WARN_LOG_FMT(IOS, "{} does not support {}()", m_name, names.at(request.command));
  return IPCReply{IPC_EINVAL};
}
}  // namespace IOS::HLE
