// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/IOS/IOS.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/Timer.h"
#include "Core/Boot/DolReader.h"
#include "Core/Boot/ElfReader.h"
#include "Core/CommonTitles.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/WII_IPC.h"
#include "Core/IOS/DI/DI.h"
#include "Core/IOS/Device.h"
#include "Core/IOS/DeviceStub.h"
#include "Core/IOS/DolphinDevice.h"
#include "Core/IOS/ES/ES.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/IOS/FS/FileSystemProxy.h"
#include "Core/IOS/MIOS.h"
#include "Core/IOS/Network/IP/Top.h"
#include "Core/IOS/Network/KD/NetKDRequest.h"
#include "Core/IOS/Network/KD/NetKDTime.h"
#include "Core/IOS/Network/NCD/Manage.h"
#include "Core/IOS/Network/SSL.h"
#include "Core/IOS/Network/Socket.h"
#include "Core/IOS/Network/WD/Command.h"
#include "Core/IOS/SDIO/SDIOSlot0.h"
#include "Core/IOS/STM/STM.h"
#include "Core/IOS/USB/Bluetooth/BTEmu.h"
#include "Core/IOS/USB/Bluetooth/BTReal.h"
#include "Core/IOS/USB/OH0/OH0.h"
#include "Core/IOS/USB/OH0/OH0Device.h"
#include "Core/IOS/USB/USB_HID/HIDv4.h"
#include "Core/IOS/USB/USB_HID/HIDv5.h"
#include "Core/IOS/USB/USB_KBD.h"
#include "Core/IOS/USB/USB_VEN/VEN.h"
#include "Core/IOS/VersionInfo.h"
#include "Core/IOS/WFS/WFSI.h"
#include "Core/IOS/WFS/WFSSRV.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/WiiRoot.h"

namespace IOS::HLE
{
static std::unique_ptr<EmulationKernel> s_ios;

constexpr u64 ENQUEUE_REQUEST_FLAG = 0x100000000ULL;
static CoreTiming::EventType* s_event_enqueue;
static CoreTiming::EventType* s_event_sdio_notify;
static CoreTiming::EventType* s_event_finish_ppc_bootstrap;
static CoreTiming::EventType* s_event_finish_ios_boot;

constexpr u32 ADDR_MEM1_SIZE = 0x3100;
constexpr u32 ADDR_MEM1_SIM_SIZE = 0x3104;
constexpr u32 ADDR_MEM1_END = 0x3108;
constexpr u32 ADDR_MEM1_ARENA_BEGIN = 0x310c;
constexpr u32 ADDR_MEM1_ARENA_END = 0x3110;
constexpr u32 ADDR_PH1 = 0x3114;
constexpr u32 ADDR_MEM2_SIZE = 0x3118;
constexpr u32 ADDR_MEM2_SIM_SIZE = 0x311c;
constexpr u32 ADDR_MEM2_END = 0x3120;
constexpr u32 ADDR_MEM2_ARENA_BEGIN = 0x3124;
constexpr u32 ADDR_MEM2_ARENA_END = 0x3128;
constexpr u32 ADDR_PH2 = 0x312c;
constexpr u32 ADDR_IPC_BUFFER_BEGIN = 0x3130;
constexpr u32 ADDR_IPC_BUFFER_END = 0x3134;
constexpr u32 ADDR_HOLLYWOOD_REVISION = 0x3138;
constexpr u32 ADDR_PH3 = 0x313c;
constexpr u32 ADDR_IOS_VERSION = 0x3140;
constexpr u32 ADDR_IOS_DATE = 0x3144;
constexpr u32 ADDR_IOS_RESERVED_BEGIN = 0x3148;
constexpr u32 ADDR_IOS_RESERVED_END = 0x314c;
constexpr u32 ADDR_PH4 = 0x3150;
constexpr u32 ADDR_PH5 = 0x3154;
constexpr u32 ADDR_RAM_VENDOR = 0x3158;
constexpr u32 ADDR_BOOT_FLAG = 0x315c;
constexpr u32 ADDR_APPLOADER_FLAG = 0x315d;
constexpr u32 ADDR_DEVKIT_BOOT_PROGRAM_VERSION = 0x315e;
constexpr u32 ADDR_SYSMENU_SYNC = 0x3160;
constexpr u32 PLACEHOLDER = 0xDEADBEEF;

static bool SetupMemory(u64 ios_title_id, MemorySetupType setup_type)
{
  auto target_imv = std::find_if(
      GetMemoryValues().begin(), GetMemoryValues().end(),
      [&](const MemoryValues& imv) { return imv.ios_number == (ios_title_id & 0xffff); });

  if (target_imv == GetMemoryValues().end())
  {
    ERROR_LOG_FMT(IOS, "Unknown IOS version: {:016x}", ios_title_id);
    return false;
  }

  if (setup_type == MemorySetupType::IOSReload)
  {
    Memory::Write_U32(target_imv->ios_version, ADDR_IOS_VERSION);

    // These values are written by the IOS kernel as part of its boot process (for IOS28 and newer).
    //
    // This works in a slightly different way on a real console: older IOS versions (< IOS28) all
    // have the same range (933E0000 - 93400000), thus they don't write it at boot and just inherit
    // all values. However, the range has changed since IOS28. To make things work properly
    // after a reload, newer IOSes always write the legacy range before loading an IOS kernel;
    // the new IOS either updates the range (>= IOS28) or inherits it (< IOS28).
    //
    // We can skip this convoluted process and just write the correct range directly.
    Memory::Write_U32(target_imv->mem2_physical_size, ADDR_MEM2_SIZE);
    Memory::Write_U32(target_imv->mem2_simulated_size, ADDR_MEM2_SIM_SIZE);
    Memory::Write_U32(target_imv->mem2_end, ADDR_MEM2_END);
    Memory::Write_U32(target_imv->mem2_arena_begin, ADDR_MEM2_ARENA_BEGIN);
    Memory::Write_U32(target_imv->mem2_arena_end, ADDR_MEM2_ARENA_END);
    Memory::Write_U32(target_imv->ipc_buffer_begin, ADDR_IPC_BUFFER_BEGIN);
    Memory::Write_U32(target_imv->ipc_buffer_end, ADDR_IPC_BUFFER_END);
    Memory::Write_U32(target_imv->ios_reserved_begin, ADDR_IOS_RESERVED_BEGIN);
    Memory::Write_U32(target_imv->ios_reserved_end, ADDR_IOS_RESERVED_END);

    RAMOverrideForIOSMemoryValues(setup_type);

    return true;
  }

  // This region is typically used to store constants (e.g. game ID, console type, ...)
  // and system information (see below).
  constexpr u32 LOW_MEM1_REGION_START = 0;
  constexpr u32 LOW_MEM1_REGION_SIZE = 0x3fff;
  Memory::Memset(LOW_MEM1_REGION_START, 0, LOW_MEM1_REGION_SIZE);

  Memory::Write_U32(target_imv->mem1_physical_size, ADDR_MEM1_SIZE);
  Memory::Write_U32(target_imv->mem1_simulated_size, ADDR_MEM1_SIM_SIZE);
  Memory::Write_U32(target_imv->mem1_end, ADDR_MEM1_END);
  Memory::Write_U32(target_imv->mem1_arena_begin, ADDR_MEM1_ARENA_BEGIN);
  Memory::Write_U32(target_imv->mem1_arena_end, ADDR_MEM1_ARENA_END);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH1);
  Memory::Write_U32(target_imv->mem2_physical_size, ADDR_MEM2_SIZE);
  Memory::Write_U32(target_imv->mem2_simulated_size, ADDR_MEM2_SIM_SIZE);
  Memory::Write_U32(target_imv->mem2_end, ADDR_MEM2_END);
  Memory::Write_U32(target_imv->mem2_arena_begin, ADDR_MEM2_ARENA_BEGIN);
  Memory::Write_U32(target_imv->mem2_arena_end, ADDR_MEM2_ARENA_END);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH2);
  Memory::Write_U32(target_imv->ipc_buffer_begin, ADDR_IPC_BUFFER_BEGIN);
  Memory::Write_U32(target_imv->ipc_buffer_end, ADDR_IPC_BUFFER_END);
  Memory::Write_U32(target_imv->hollywood_revision, ADDR_HOLLYWOOD_REVISION);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH3);
  Memory::Write_U32(target_imv->ios_version, ADDR_IOS_VERSION);
  Memory::Write_U32(target_imv->ios_date, ADDR_IOS_DATE);
  Memory::Write_U32(target_imv->ios_reserved_begin, ADDR_IOS_RESERVED_BEGIN);
  Memory::Write_U32(target_imv->ios_reserved_end, ADDR_IOS_RESERVED_END);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH4);
  Memory::Write_U32(PLACEHOLDER, ADDR_PH5);
  Memory::Write_U32(target_imv->ram_vendor, ADDR_RAM_VENDOR);
  Memory::Write_U8(0xDE, ADDR_BOOT_FLAG);
  Memory::Write_U8(0xAD, ADDR_APPLOADER_FLAG);
  Memory::Write_U16(0xBEEF, ADDR_DEVKIT_BOOT_PROGRAM_VERSION);
  Memory::Write_U32(target_imv->sysmenu_sync, ADDR_SYSMENU_SYNC);

  RAMOverrideForIOSMemoryValues(setup_type);

  return true;
}

// On a real console, the Starlet resets the PPC and holds it in reset limbo
// by asserting the PPC's HRESET signal (via HW_RESETS).
// We will simulate that by resetting MSR and putting the PPC into an infinite loop.
// The memory write will not be observable since the PPC is not running any code...
static void ResetAndPausePPC()
{
  // This should be cleared when the PPC is released so that the write is not observable.
  Memory::Write_U32(0x48000000, 0x00000000);  // b 0x0
  PowerPC::Reset();
  PC = 0;
}

static void ReleasePPC()
{
  Memory::Write_U32(0, 0);
  // HLE the bootstub that jumps to 0x3400.
  // NAND titles start with address translation off at 0x3400 (via the PPC bootstub)
  // The state of other CPU registers (like the BAT registers) doesn't matter much
  // because the realmode code at 0x3400 initializes everything itself anyway.
  PC = 0x3400;
}

void RAMOverrideForIOSMemoryValues(MemorySetupType setup_type)
{
  // Don't touch anything if the feature isn't enabled.
  if (!Config::Get(Config::MAIN_RAM_OVERRIDE_ENABLE))
    return;

  // Some unstated constants that can be inferred.
  const u32 ipc_buffer_size =
      Memory::Read_U32(ADDR_IPC_BUFFER_END) - Memory::Read_U32(ADDR_IPC_BUFFER_BEGIN);
  const u32 ios_reserved_size =
      Memory::Read_U32(ADDR_IOS_RESERVED_END) - Memory::Read_U32(ADDR_IOS_RESERVED_BEGIN);

  const u32 mem1_physical_size = Memory::GetRamSizeReal();
  const u32 mem1_simulated_size = Memory::GetRamSizeReal();
  const u32 mem1_end = Memory::MEM1_BASE_ADDR + mem1_simulated_size;
  const u32 mem1_arena_begin = 0;
  const u32 mem1_arena_end = mem1_end;
  const u32 mem2_physical_size = Memory::GetExRamSizeReal();
  const u32 mem2_simulated_size = Memory::GetExRamSizeReal();
  const u32 mem2_end = Memory::MEM2_BASE_ADDR + mem2_simulated_size - ios_reserved_size;
  const u32 mem2_arena_begin = Memory::MEM2_BASE_ADDR + 0x800U;
  const u32 mem2_arena_end = mem2_end - ipc_buffer_size;
  const u32 ipc_buffer_begin = mem2_arena_end;
  const u32 ipc_buffer_end = mem2_end;
  const u32 ios_reserved_begin = mem2_end;
  const u32 ios_reserved_end = Memory::MEM2_BASE_ADDR + mem2_simulated_size;

  if (setup_type == MemorySetupType::Full)
  {
    // Overwriting these after the game's apploader sets them would be bad
    Memory::Write_U32(mem1_physical_size, ADDR_MEM1_SIZE);
    Memory::Write_U32(mem1_simulated_size, ADDR_MEM1_SIM_SIZE);
    Memory::Write_U32(mem1_end, ADDR_MEM1_END);
    Memory::Write_U32(mem1_arena_begin, ADDR_MEM1_ARENA_BEGIN);
    Memory::Write_U32(mem1_arena_end, ADDR_MEM1_ARENA_END);
  }
  Memory::Write_U32(mem2_physical_size, ADDR_MEM2_SIZE);
  Memory::Write_U32(mem2_simulated_size, ADDR_MEM2_SIM_SIZE);
  Memory::Write_U32(mem2_end, ADDR_MEM2_END);
  Memory::Write_U32(mem2_arena_begin, ADDR_MEM2_ARENA_BEGIN);
  Memory::Write_U32(mem2_arena_end, ADDR_MEM2_ARENA_END);
  Memory::Write_U32(ipc_buffer_begin, ADDR_IPC_BUFFER_BEGIN);
  Memory::Write_U32(ipc_buffer_end, ADDR_IPC_BUFFER_END);
  Memory::Write_U32(ios_reserved_begin, ADDR_IOS_RESERVED_BEGIN);
  Memory::Write_U32(ios_reserved_end, ADDR_IOS_RESERVED_END);
}

void WriteReturnValue(s32 value, u32 address)
{
  Memory::Write_U32(static_cast<u32>(value), address);
}

Kernel::Kernel()
{
  // Until the Wii root and NAND path stuff is entirely managed by IOS and made non-static,
  // using more than one IOS instance at a time is not supported.
  ASSERT(GetIOS() == nullptr);
  Core::InitializeWiiRoot(false);
  m_is_responsible_for_nand_root = true;
  AddCoreDevices();
}

Kernel::~Kernel()
{
  {
    std::lock_guard lock(m_device_map_mutex);
    m_device_map.clear();
  }

  if (m_is_responsible_for_nand_root)
    Core::ShutdownWiiRoot();
}

Kernel::Kernel(u64 title_id) : m_title_id(title_id)
{
}

EmulationKernel::EmulationKernel(u64 title_id) : Kernel(title_id)
{
  INFO_LOG_FMT(IOS, "Starting IOS {:016x}", title_id);

  if (!SetupMemory(title_id, MemorySetupType::IOSReload))
    WARN_LOG_FMT(IOS, "No information about this IOS -- cannot set up memory values");

  if (title_id == Titles::MIOS)
  {
    MIOS::Load();
    return;
  }

  AddCoreDevices();
  AddStaticDevices();
}

EmulationKernel::~EmulationKernel()
{
  CoreTiming::RemoveAllEvents(s_event_enqueue);
}

// The title ID is a u64 where the first 32 bits are used for the title type.
// For IOS title IDs, the type will always be 00000001 (system), and the lower 32 bits
// are used for the IOS major version -- which is what we want here.
u32 Kernel::GetVersion() const
{
  return static_cast<u32>(m_title_id);
}

std::shared_ptr<FS::FileSystem> Kernel::GetFS()
{
  return m_fs;
}

std::shared_ptr<FSDevice> Kernel::GetFSDevice()
{
  return std::static_pointer_cast<FSDevice>(m_device_map.at("/dev/fs"));
}

std::shared_ptr<ESDevice> Kernel::GetES()
{
  return std::static_pointer_cast<ESDevice>(m_device_map.at("/dev/es"));
}

// Since we don't have actual processes, we keep track of only the PPC's UID/GID.
// These functions roughly correspond to syscalls 0x2b, 0x2c, 0x2d, 0x2e (though only for the PPC).
void Kernel::SetUidForPPC(u32 uid)
{
  m_ppc_uid = uid;
}

u32 Kernel::GetUidForPPC() const
{
  return m_ppc_uid;
}

void Kernel::SetGidForPPC(u16 gid)
{
  m_ppc_gid = gid;
}

u16 Kernel::GetGidForPPC() const
{
  return m_ppc_gid;
}

static std::vector<u8> ReadBootContent(FSDevice* fs, const std::string& path, size_t max_size,
                                       Ticks ticks = {})
{
  const s64 fd = fs->Open(0, 0, path, FS::Mode::Read, {}, ticks);
  if (fd < 0)
    return {};

  const size_t file_size = fs->GetFileStatus(fd, ticks)->size;
  if (max_size != 0 && file_size > max_size)
    return {};

  std::vector<u8> buffer(file_size);
  if (!fs->Read(fd, buffer.data(), buffer.size(), ticks))
    return {};
  return buffer;
}

// This corresponds to syscall 0x41, which loads a binary from the NAND and bootstraps the PPC.
// Unlike 0x42, IOS will set up some constants in memory before booting the PPC.
bool Kernel::BootstrapPPC(const std::string& boot_content_path)
{
  // Seeking and processing overhead is ignored as most time is spent reading from the NAND.
  u64 ticks = 0;

  const DolReader dol{ReadBootContent(GetFSDevice().get(), boot_content_path, 0, &ticks)};

  if (!dol.IsValid())
    return false;

  if (!SetupMemory(m_title_id, MemorySetupType::Full))
    return false;

  // Reset the PPC and pause its execution until we're ready.
  ResetAndPausePPC();

  if (!dol.LoadIntoMemory())
    return false;

  INFO_LOG_FMT(IOS, "BootstrapPPC: {}", boot_content_path);
  CoreTiming::ScheduleEvent(ticks, s_event_finish_ppc_bootstrap);
  return true;
}

struct ARMBinary final
{
  explicit ARMBinary(std::vector<u8>&& bytes) : m_bytes(std::move(bytes)) {}
  bool IsValid() const
  {
    // The header is at least 0x10.
    if (m_bytes.size() < 0x10)
      return false;
    return m_bytes.size() >= (GetHeaderSize() + GetElfOffset() + GetElfSize());
  }

  std::vector<u8> GetElf() const
  {
    const auto iterator = m_bytes.cbegin() + GetHeaderSize() + GetElfOffset();
    return std::vector<u8>(iterator, iterator + GetElfSize());
  }

  u32 GetHeaderSize() const { return Common::swap32(m_bytes.data()); }
  u32 GetElfOffset() const { return Common::swap32(m_bytes.data() + 0x4); }
  u32 GetElfSize() const { return Common::swap32(m_bytes.data() + 0x8); }

private:
  std::vector<u8> m_bytes;
};

static void FinishIOSBoot(u64 ios_title_id)
{
  // Shut down the active IOS first before switching to the new one.
  s_ios.reset();
  s_ios = std::make_unique<EmulationKernel>(ios_title_id);
}

static constexpr SystemTimers::TimeBaseTick GetIOSBootTicks(u32 version)
{
  // Older IOS versions are monolithic so the main ELF is much larger and takes longer to load.
  if (version < 28)
    return 16'000'000_tbticks;
  return 2'600'000_tbticks;
}

// Similar to syscall 0x42 (ios_boot); this is used to change the current active IOS.
// IOS writes the new version to 0x3140 before restarting, but it does *not* poke any
// of the other constants to the memory. Warning: this resets the kernel instance.
//
// Passing a boot content path is optional because we do not require IOSes
// to be installed at the moment. If one is passed, the boot binary must exist
// on the NAND, or the call will fail like on a Wii.
bool Kernel::BootIOS(const u64 ios_title_id, HangPPC hang_ppc, const std::string& boot_content_path)
{
  // IOS suspends regular PPC<->ARM IPC before loading a new IOS.
  // IPC is not resumed if the boot fails for any reason.
  m_ipc_paused = true;

  if (!boot_content_path.empty())
  {
    // Load the ARM binary to memory (if possible).
    // Because we do not actually emulate the Starlet, only load the sections that are in MEM1.

    ARMBinary binary{ReadBootContent(GetFSDevice().get(), boot_content_path, 0xB00000)};
    if (!binary.IsValid())
      return false;

    ElfReader elf{binary.GetElf()};
    if (!elf.LoadIntoMemory(true))
      return false;
  }

  if (hang_ppc == HangPPC::Yes)
    ResetAndPausePPC();

  if (Core::IsRunningAndStarted())
    CoreTiming::ScheduleEvent(GetIOSBootTicks(GetVersion()), s_event_finish_ios_boot, ios_title_id);
  else
    FinishIOSBoot(ios_title_id);

  return true;
}

void Kernel::InitIPC()
{
  if (s_ios == nullptr)
    return;

  INFO_LOG_FMT(IOS, "IPC initialised.");
  GenerateAck(0);
}

void Kernel::AddDevice(std::unique_ptr<Device> device)
{
  ASSERT(device->GetDeviceType() == Device::DeviceType::Static);
  m_device_map.insert_or_assign(device->GetDeviceName(), std::move(device));
}

void Kernel::AddCoreDevices()
{
  m_fs = FS::MakeFileSystem();
  ASSERT(m_fs);

  std::lock_guard lock(m_device_map_mutex);
  AddDevice(std::make_unique<FSDevice>(*this, "/dev/fs"));
  AddDevice(std::make_unique<ESDevice>(*this, "/dev/es"));
  AddDevice(std::make_unique<DolphinDevice>(*this, "/dev/dolphin"));
}

void Kernel::AddStaticDevices()
{
  std::lock_guard lock(m_device_map_mutex);

  const Feature features = GetFeatures(GetVersion());

  // OH1 (Bluetooth)
  AddDevice(std::make_unique<DeviceStub>(*this, "/dev/usb/oh1"));
  if (!SConfig::GetInstance().m_bt_passthrough_enabled)
    AddDevice(std::make_unique<BluetoothEmuDevice>(*this, "/dev/usb/oh1/57e/305"));
  else
    AddDevice(std::make_unique<BluetoothRealDevice>(*this, "/dev/usb/oh1/57e/305"));

  // Other core modules
  AddDevice(std::make_unique<STMImmediateDevice>(*this, "/dev/stm/immediate"));
  AddDevice(std::make_unique<STMEventHookDevice>(*this, "/dev/stm/eventhook"));
  AddDevice(std::make_unique<DIDevice>(*this, "/dev/di"));
  AddDevice(std::make_unique<SDIOSlot0Device>(*this, "/dev/sdio/slot0"));
  AddDevice(std::make_unique<DeviceStub>(*this, "/dev/sdio/slot1"));

  // Network modules
  if (HasFeature(features, Feature::KD))
  {
    AddDevice(std::make_unique<NetKDRequestDevice>(*this, "/dev/net/kd/request"));
    AddDevice(std::make_unique<NetKDTimeDevice>(*this, "/dev/net/kd/time"));
  }
  if (HasFeature(features, Feature::NCD))
  {
    AddDevice(std::make_unique<NetNCDManageDevice>(*this, "/dev/net/ncd/manage"));
  }
  if (HasFeature(features, Feature::WiFi))
  {
    AddDevice(std::make_unique<NetWDCommandDevice>(*this, "/dev/net/wd/command"));
  }
  if (HasFeature(features, Feature::SO))
  {
    AddDevice(std::make_unique<NetIPTopDevice>(*this, "/dev/net/ip/top"));
  }
  if (HasFeature(features, Feature::SSL))
  {
    AddDevice(std::make_unique<NetSSLDevice>(*this, "/dev/net/ssl"));
  }

  // USB modules
  // OH0 is unconditionally added because this device path is registered in all cases.
  AddDevice(std::make_unique<OH0>(*this, "/dev/usb/oh0"));
  if (HasFeature(features, Feature::NewUSB))
  {
    AddDevice(std::make_unique<USB_HIDv5>(*this, "/dev/usb/hid"));
    AddDevice(std::make_unique<USB_VEN>(*this, "/dev/usb/ven"));

    // TODO(IOS): register /dev/usb/usb, /dev/usb/msc, /dev/usb/hub and /dev/usb/ehc
    //            as stubs that return IPC_EACCES.
  }
  else
  {
    if (HasFeature(features, Feature::USB_HIDv4))
      AddDevice(std::make_unique<USB_HIDv4>(*this, "/dev/usb/hid"));
    if (HasFeature(features, Feature::USB_KBD))
      AddDevice(std::make_unique<USB_KBD>(*this, "/dev/usb/kbd"));
  }

  if (HasFeature(features, Feature::WFS))
  {
    AddDevice(std::make_unique<WFSSRVDevice>(*this, "/dev/usb/wfssrv"));
    AddDevice(std::make_unique<WFSIDevice>(*this, "/dev/wfsi"));
  }
}

s32 Kernel::GetFreeDeviceID()
{
  for (u32 i = 0; i < IPC_MAX_FDS; i++)
  {
    if (m_fdmap[i] == nullptr)
    {
      return i;
    }
  }

  return -1;
}

std::shared_ptr<Device> Kernel::GetDeviceByName(std::string_view device_name)
{
  std::lock_guard lock(m_device_map_mutex);
  const auto iterator = m_device_map.find(device_name);
  return iterator != m_device_map.end() ? iterator->second : nullptr;
}

std::shared_ptr<Device> EmulationKernel::GetDeviceByName(std::string_view device_name)
{
  return Kernel::GetDeviceByName(device_name);
}

// Returns the FD for the newly opened device (on success) or an error code.
std::optional<IPCReply> Kernel::OpenDevice(OpenRequest& request)
{
  const s32 new_fd = GetFreeDeviceID();
  INFO_LOG_FMT(IOS, "Opening {} (mode {}, fd {})", request.path, request.flags, new_fd);
  if (new_fd < 0 || new_fd >= IPC_MAX_FDS)
  {
    ERROR_LOG_FMT(IOS, "Couldn't get a free fd, too many open files");
    return IPCReply{IPC_EMAX, 5000_tbticks};
  }
  request.fd = new_fd;

  std::shared_ptr<Device> device;
  if (request.path.find("/dev/usb/oh0/") == 0 && !GetDeviceByName(request.path) &&
      !HasFeature(GetVersion(), Feature::NewUSB))
  {
    device = std::make_shared<OH0Device>(*this, request.path);
  }
  else if (request.path.find("/dev/") == 0)
  {
    device = GetDeviceByName(request.path);
  }
  else if (request.path.find('/') == 0)
  {
    device = GetDeviceByName("/dev/fs");
  }

  if (!device)
  {
    ERROR_LOG_FMT(IOS, "Unknown device: {}", request.path);
    return IPCReply{IPC_ENOENT, 3700_tbticks};
  }

  std::optional<IPCReply> result = device->Open(request);
  if (result && result->return_value >= IPC_SUCCESS)
  {
    m_fdmap[new_fd] = device;
    result->return_value = new_fd;
  }
  return result;
}

std::optional<IPCReply> Kernel::HandleIPCCommand(const Request& request)
{
  if (request.command < IPC_CMD_OPEN || request.command > IPC_CMD_IOCTLV)
    return IPCReply{IPC_EINVAL, 978_tbticks};

  if (request.command == IPC_CMD_OPEN)
  {
    OpenRequest open_request{request.address};
    return OpenDevice(open_request);
  }

  const auto device = (request.fd < IPC_MAX_FDS) ? m_fdmap[request.fd] : nullptr;
  if (!device)
    return IPCReply{IPC_EINVAL, 550_tbticks};

  std::optional<IPCReply> ret;
  const u64 wall_time_before = Common::Timer::GetTimeUs();

  switch (request.command)
  {
  case IPC_CMD_CLOSE:
    m_fdmap[request.fd].reset();
    ret = device->Close(request.fd);
    break;
  case IPC_CMD_READ:
    ret = device->Read(ReadWriteRequest{request.address});
    break;
  case IPC_CMD_WRITE:
    ret = device->Write(ReadWriteRequest{request.address});
    break;
  case IPC_CMD_SEEK:
    ret = device->Seek(SeekRequest{request.address});
    break;
  case IPC_CMD_IOCTL:
    ret = device->IOCtl(IOCtlRequest{request.address});
    break;
  case IPC_CMD_IOCTLV:
    ret = device->IOCtlV(IOCtlVRequest{request.address});
    break;
  default:
    ASSERT_MSG(IOS, false, "Unexpected command: %x", request.command);
    ret = IPCReply{IPC_EINVAL, 978_tbticks};
    break;
  }

  const u64 wall_time_after = Common::Timer::GetTimeUs();
  constexpr u64 BLOCKING_IPC_COMMAND_THRESHOLD_US = 2000;
  if (wall_time_after - wall_time_before > BLOCKING_IPC_COMMAND_THRESHOLD_US)
  {
    WARN_LOG_FMT(IOS, "Previous request to device {} blocked emulation for {} microseconds.",
                 device->GetDeviceName(), wall_time_after - wall_time_before);
  }

  return ret;
}

void Kernel::ExecuteIPCCommand(const u32 address)
{
  Request request{address};
  std::optional<IPCReply> result = HandleIPCCommand(request);

  if (!result)
    return;

  // Ensure replies happen in order
  const s64 ticks_until_last_reply = m_last_reply_time - CoreTiming::GetTicks();
  if (ticks_until_last_reply > 0)
    result->reply_delay_ticks += ticks_until_last_reply;
  m_last_reply_time = CoreTiming::GetTicks() + result->reply_delay_ticks;

  EnqueueIPCReply(request, result->return_value, result->reply_delay_ticks);
}

// Happens AS SOON AS IPC gets a new pointer!
void Kernel::EnqueueIPCRequest(u32 address)
{
  // Based on hardware tests, IOS takes between 5µs and 10µs to acknowledge an IPC request.
  // Console 1: 456 TB ticks before ACK
  // Console 2: 658 TB ticks before ACK
  CoreTiming::ScheduleEvent(500_tbticks, s_event_enqueue, address | ENQUEUE_REQUEST_FLAG);
}

// Called to send a reply to an IOS syscall
void Kernel::EnqueueIPCReply(const Request& request, const s32 return_value, s64 cycles_in_future,
                             CoreTiming::FromThread from)
{
  Memory::Write_U32(static_cast<u32>(return_value), request.address + 4);
  // IOS writes back the command that was responded to in the FD field.
  Memory::Write_U32(request.command, request.address + 8);
  // IOS also overwrites the command type with the reply type.
  Memory::Write_U32(IPC_REPLY, request.address);
  CoreTiming::ScheduleEvent(cycles_in_future, s_event_enqueue, request.address, from);
}

void Kernel::HandleIPCEvent(u64 userdata)
{
  if (userdata & ENQUEUE_REQUEST_FLAG)
    m_request_queue.push_back(static_cast<u32>(userdata));
  else
    m_reply_queue.push_back(static_cast<u32>(userdata));

  UpdateIPC();
}

void Kernel::UpdateIPC()
{
  if (m_ipc_paused || !IsReady())
    return;

  if (!m_request_queue.empty())
  {
    ClearX1();
    GenerateAck(m_request_queue.front());
    u32 command = m_request_queue.front();
    m_request_queue.pop_front();
    ExecuteIPCCommand(command);
    return;
  }

  if (!m_reply_queue.empty())
  {
    GenerateReply(m_reply_queue.front());
    DEBUG_LOG_FMT(IOS, "<<-- Reply to IPC Request @ {:#010x}", m_reply_queue.front());
    m_reply_queue.pop_front();
    return;
  }
}

void Kernel::UpdateDevices()
{
  // Check if a hardware device must be updated
  for (const auto& entry : m_device_map)
  {
    if (entry.second->IsOpened())
    {
      entry.second->Update();
    }
  }
}

void Kernel::UpdateWantDeterminism(const bool new_want_determinism)
{
  WiiSockMan::GetInstance().UpdateWantDeterminism(new_want_determinism);
  for (const auto& device : m_device_map)
    device.second->UpdateWantDeterminism(new_want_determinism);
}

void Kernel::SDIO_EventNotify()
{
  // TODO: Potential race condition: If IsRunning() becomes false after
  // it's checked, an event may be scheduled after CoreTiming shuts down.
  if (SConfig::GetInstance().bWii && Core::IsRunning())
    CoreTiming::ScheduleEvent(0, s_event_sdio_notify, 0, CoreTiming::FromThread::NON_CPU);
}

void Kernel::DoState(PointerWrap& p)
{
  p.Do(m_request_queue);
  p.Do(m_reply_queue);
  p.Do(m_last_reply_time);
  p.Do(m_ipc_paused);
  p.Do(m_title_id);
  p.Do(m_ppc_uid);
  p.Do(m_ppc_gid);

  m_iosc.DoState(p);
  m_fs->DoState(p);

  if (m_title_id == Titles::MIOS)
    return;

  for (const auto& entry : m_device_map)
    entry.second->DoState(p);

  if (p.GetMode() == PointerWrap::MODE_READ)
  {
    for (u32 i = 0; i < IPC_MAX_FDS; i++)
    {
      u32 exists = 0;
      p.Do(exists);
      if (exists)
      {
        auto device_type = Device::DeviceType::Static;
        p.Do(device_type);
        switch (device_type)
        {
        case Device::DeviceType::Static:
        {
          std::string device_name;
          p.Do(device_name);
          m_fdmap[i] = GetDeviceByName(device_name);
          break;
        }
        case Device::DeviceType::OH0:
          m_fdmap[i] = std::make_shared<OH0Device>(*this, "");
          m_fdmap[i]->DoState(p);
          break;
        }
      }
    }
  }
  else
  {
    for (auto& descriptor : m_fdmap)
    {
      u32 exists = descriptor ? 1 : 0;
      p.Do(exists);
      if (exists)
      {
        auto device_type = descriptor->GetDeviceType();
        p.Do(device_type);
        if (device_type == Device::Device::DeviceType::Static)
        {
          std::string device_name = descriptor->GetDeviceName();
          p.Do(device_name);
        }
        else
        {
          descriptor->DoState(p);
        }
      }
    }
  }
}

IOSC& Kernel::GetIOSC()
{
  return m_iosc;
}

static void FinishPPCBootstrap(u64 userdata, s64 cycles_late)
{
  ReleasePPC();
  SConfig::OnNewTitleLoad();
  INFO_LOG_FMT(IOS, "Bootstrapping done.");
}

void Init()
{
  s_event_enqueue = CoreTiming::RegisterEvent("IPCEvent", [](u64 userdata, s64) {
    if (s_ios)
      s_ios->HandleIPCEvent(userdata);
  });

  s_event_sdio_notify = CoreTiming::RegisterEvent("SDIO_EventNotify", [](u64, s64) {
    if (!s_ios)
      return;

    auto sdio_slot0 = s_ios->GetDeviceByName("/dev/sdio/slot0");
    auto device = static_cast<SDIOSlot0Device*>(sdio_slot0.get());
    if (device)
      device->EventNotify();
  });

  ESDevice::InitializeEmulationState();

  s_event_finish_ppc_bootstrap =
      CoreTiming::RegisterEvent("IOSFinishPPCBootstrap", FinishPPCBootstrap);

  s_event_finish_ios_boot = CoreTiming::RegisterEvent(
      "IOSFinishIOSBoot", [](u64 ios_title_id, s64) { FinishIOSBoot(ios_title_id); });

  DIDevice::s_finish_executing_di_command =
      CoreTiming::RegisterEvent("FinishDICommand", DIDevice::FinishDICommandCallback);

  // Start with IOS80 to simulate part of the Wii boot process.
  s_ios = std::make_unique<EmulationKernel>(Titles::SYSTEM_MENU_IOS);
  // On a Wii, boot2 launches the system menu IOS, which then launches the system menu
  // (which bootstraps the PPC). Bootstrapping the PPC results in memory values being set up.
  // This means that the constants in the 0x3100 region are always set up by the time
  // a game is launched. This is necessary because booting games from the game list skips
  // a significant part of a Wii's boot process.
  SetupMemory(Titles::SYSTEM_MENU_IOS, MemorySetupType::Full);
}

void Shutdown()
{
  s_ios.reset();
  ESDevice::FinalizeEmulationState();
}

EmulationKernel* GetIOS()
{
  return s_ios.get();
}

// Based on a hardware test, a device takes at least ~2700 ticks to reply to an IPC request.
// Depending on how much work a command performs, this can take much longer (10000+)
// especially if the NAND filesystem is accessed.
//
// Because we currently don't emulate timing very accurately, we should not return
// the minimum possible reply time (~960 ticks from the kernel or ~2700 from devices)
// but an average value, otherwise we are going to be much too fast in most cases.
IPCReply::IPCReply(s32 return_value_) : IPCReply(return_value_, 4000_tbticks)
{
}

IPCReply::IPCReply(s32 return_value_, u64 reply_delay_ticks_)
    : return_value(return_value_), reply_delay_ticks(reply_delay_ticks_)
{
}
}  // namespace IOS::HLE
