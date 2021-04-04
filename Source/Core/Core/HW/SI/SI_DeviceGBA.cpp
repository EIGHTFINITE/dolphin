// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/SI/SI_DeviceGBA.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include <SFML/Network.hpp>

#include "Common/CommonTypes.h"
#include "Common/Flag.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"
#include "Common/Thread.h"
#include "Core/CoreTiming.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/SystemTimers.h"

namespace SerialInterface
{
namespace
{
std::thread s_connection_thread;
std::queue<std::unique_ptr<sf::TcpSocket>> s_waiting_socks;
std::queue<std::unique_ptr<sf::TcpSocket>> s_waiting_clocks;
std::mutex s_cs_gba;
std::mutex s_cs_gba_clk;
int s_num_connected;
Common::Flag s_server_running;
}  // namespace

enum EJoybusCmds
{
  CMD_RESET = 0xff,
  CMD_STATUS = 0x00,
  CMD_READ = 0x14,
  CMD_WRITE = 0x15
};

constexpr auto BITS_PER_SECOND = 115200;
constexpr auto BYTES_PER_SECOND = BITS_PER_SECOND / 8;
constexpr auto SEND_MAX_SIZE = 5, RECV_MAX_SIZE = 5;

// --- GameBoy Advance "Link Cable" ---

static int GetTransferTime(u8 cmd)
{
  u64 bytes_transferred = 0;

  switch (cmd)
  {
  case CMD_RESET:
  case CMD_STATUS:
  {
    bytes_transferred = 4;
    break;
  }
  case CMD_READ:
  {
    bytes_transferred = 6;
    break;
  }
  case CMD_WRITE:
  {
    bytes_transferred = 1;
    break;
  }
  default:
  {
    bytes_transferred = 1;
    break;
  }
  }
  return static_cast<int>(bytes_transferred * SystemTimers::GetTicksPerSecond() /
                          (std::max(s_num_connected, 1) * BYTES_PER_SECOND));
}

static void GBAConnectionWaiter()
{
  s_server_running.Set();

  Common::SetCurrentThreadName("GBA Connection Waiter");

  sf::TcpListener server;
  sf::TcpListener clock_server;

  // "dolphin gba"
  if (server.listen(0xd6ba) != sf::Socket::Done)
    return;

  // "clock"
  if (clock_server.listen(0xc10c) != sf::Socket::Done)
    return;

  server.setBlocking(false);
  clock_server.setBlocking(false);

  auto new_client = std::make_unique<sf::TcpSocket>();
  while (s_server_running.IsSet())
  {
    if (server.accept(*new_client) == sf::Socket::Done)
    {
      std::lock_guard lk(s_cs_gba);
      s_waiting_socks.push(std::move(new_client));

      new_client = std::make_unique<sf::TcpSocket>();
    }
    if (clock_server.accept(*new_client) == sf::Socket::Done)
    {
      std::lock_guard lk(s_cs_gba_clk);
      s_waiting_clocks.push(std::move(new_client));

      new_client = std::make_unique<sf::TcpSocket>();
    }

    Common::SleepCurrentThread(1);
  }
}

void GBAConnectionWaiter_Shutdown()
{
  s_server_running.Clear();
  if (s_connection_thread.joinable())
    s_connection_thread.join();
}

template <typename T>
static std::unique_ptr<T> MoveFromFront(std::queue<std::unique_ptr<T>>& ptrs)
{
  if (ptrs.empty())
    return nullptr;
  std::unique_ptr<T> ptr = std::move(ptrs.front());
  ptrs.pop();
  return ptr;
}

static std::unique_ptr<sf::TcpSocket> GetNextSock()
{
  std::lock_guard lk(s_cs_gba);
  return MoveFromFront(s_waiting_socks);
}

static std::unique_ptr<sf::TcpSocket> GetNextClock()
{
  std::lock_guard lk(s_cs_gba_clk);
  return MoveFromFront(s_waiting_clocks);
}

GBASockServer::GBASockServer()
{
  if (!s_connection_thread.joinable())
    s_connection_thread = std::thread(GBAConnectionWaiter);

  s_num_connected = 0;
}

GBASockServer::~GBASockServer()
{
  Disconnect();
}

void GBASockServer::Disconnect()
{
  if (m_client)
  {
    s_num_connected--;
    m_client->disconnect();
    m_client = nullptr;
  }
  if (m_clock_sync)
  {
    m_clock_sync->disconnect();
    m_clock_sync = nullptr;
  }
  m_last_time_slice = 0;
  m_booted = false;
}

void GBASockServer::ClockSync()
{
  if (!m_clock_sync)
    if (!(m_clock_sync = GetNextClock()))
      return;

  u32 time_slice = 0;

  if (m_last_time_slice == 0)
  {
    s_num_connected++;
    m_last_time_slice = CoreTiming::GetTicks();
    time_slice = (u32)(SystemTimers::GetTicksPerSecond() / 60);
  }
  else
  {
    time_slice = (u32)(CoreTiming::GetTicks() - m_last_time_slice);
  }

  time_slice = (u32)((u64)time_slice * 16777216 / SystemTimers::GetTicksPerSecond());
  m_last_time_slice = CoreTiming::GetTicks();
  char bytes[4] = {0, 0, 0, 0};
  bytes[0] = (time_slice >> 24) & 0xff;
  bytes[1] = (time_slice >> 16) & 0xff;
  bytes[2] = (time_slice >> 8) & 0xff;
  bytes[3] = time_slice & 0xff;

  sf::Socket::Status status = m_clock_sync->send(bytes, 4);
  if (status == sf::Socket::Disconnected)
  {
    m_clock_sync->disconnect();
    m_clock_sync = nullptr;
  }
}

bool GBASockServer::Connect()
{
  if (!IsConnected())
  {
    m_client = GetNextSock();
    if (m_client)
      m_client->setBlocking(false);
  }
  return IsConnected();
}

bool GBASockServer::IsConnected()
{
  return static_cast<bool>(m_client);
}

void GBASockServer::Send(const u8* si_buffer)
{
  if (!Connect())
    return;

  std::array<u8, SEND_MAX_SIZE> send_data;
  for (size_t i = 0; i < send_data.size(); i++)
    send_data[i] = si_buffer[i];

  u8 cmd = send_data[0];

  sf::Socket::Status status;
  if (cmd == CMD_WRITE)
    status = m_client->send(send_data.data(), send_data.size());
  else
    status = m_client->send(send_data.data(), 1);

  if (status == sf::Socket::Disconnected)
    Disconnect();
}

int GBASockServer::Receive(u8* si_buffer, u8 bytes)
{
  if (!m_client)
    return 0;

  if (m_booted)
  {
    sf::SocketSelector selector;
    selector.add(*m_client);
    selector.wait(sf::milliseconds(1000));
  }

  size_t num_received = 0;
  std::array<u8, RECV_MAX_SIZE> recv_data;
  sf::Socket::Status recv_stat = m_client->receive(recv_data.data(), bytes, num_received);
  if (recv_stat == sf::Socket::Disconnected)
  {
    Disconnect();
    return 0;
  }

  if (recv_stat == sf::Socket::NotReady || num_received == 0)
  {
    m_booted = false;
    return 0;
  }
  m_booted = true;

  for (size_t i = 0; i < recv_data.size(); i++)
    si_buffer[i] = recv_data[i];
  return static_cast<int>(std::min(num_received, recv_data.size()));
}

void GBASockServer::Flush()
{
  if (!m_client)
    return;

  size_t num_received = 1;
  u8 byte;
  while (num_received)
  {
    sf::Socket::Status recv_stat = m_client->receive(&byte, 1, num_received);
    if (recv_stat != sf::Socket::Done)
      break;
  }
}

CSIDevice_GBA::CSIDevice_GBA(SIDevices device, int device_number) : ISIDevice(device, device_number)
{
}

int CSIDevice_GBA::RunBuffer(u8* buffer, int request_length)
{
  switch (m_next_action)
  {
  case NextAction::SendCommand:
  {
    m_sock_server.ClockSync();
    if (m_sock_server.Connect())
    {
#ifdef _DEBUG
      NOTICE_LOG_FMT(SERIALINTERFACE, "{} cmd {:02x} [> {:02x}{:02x}{:02x}{:02x}]", m_device_number,
                     buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
#endif
      m_sock_server.Flush();  // Clear out any replies we might have timed out waiting for
      m_sock_server.Send(buffer);
    }
    else
    {
      u32 reply = Common::swap32(SI_ERROR_NO_RESPONSE);
      std::memcpy(buffer, &reply, sizeof(reply));
      return sizeof(reply);
    }
    m_last_cmd = buffer[0];
    m_timestamp_sent = CoreTiming::GetTicks();
    m_next_action = NextAction::WaitTransferTime;
    return 0;
  }

  case NextAction::WaitTransferTime:
  {
    int elapsed_time = static_cast<int>(CoreTiming::GetTicks() - m_timestamp_sent);
    // Tell SI to ask again after TransferInterval() cycles
    if (GetTransferTime(m_last_cmd) > elapsed_time)
      return 0;
    m_next_action = NextAction::ReceiveResponse;
    [[fallthrough]];
  }

  case NextAction::ReceiveResponse:
  {
    u8 bytes = 1;
    switch (m_last_cmd)
    {
    case CMD_RESET:
    case CMD_STATUS:
      bytes = 3;
      break;
    case CMD_READ:
      bytes = 5;
      break;
    default:
      break;
    }
    int num_data_received = m_sock_server.Receive(buffer, bytes);

    m_next_action = NextAction::SendCommand;
    if (num_data_received == 0)
    {
      u32 reply = Common::swap32(SI_ERROR_NO_RESPONSE);
      std::memcpy(buffer, &reply, sizeof(reply));
      return sizeof(reply);
    }
#ifdef _DEBUG
    const Common::Log::LOG_LEVELS log_level =
        (m_last_cmd == CMD_STATUS || m_last_cmd == CMD_RESET) ? Common::Log::LERROR :
                                                                Common::Log::LWARNING;
    GENERIC_LOG_FMT(Common::Log::SERIALINTERFACE, log_level,
                    "{}                              [< {:02x}{:02x}{:02x}{:02x}{:02x}] ({})",
                    m_device_number, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4],
                    num_data_received);
#endif
    return num_data_received;
  }
  }

  // This should never happen, but appease MSVC which thinks it might.
  ERROR_LOG_FMT(SERIALINTERFACE, "Unknown state {}\n", m_next_action);
  return 0;
}

int CSIDevice_GBA::TransferInterval()
{
  return GetTransferTime(m_last_cmd);
}

bool CSIDevice_GBA::GetData(u32& hi, u32& low)
{
  return false;
}

void CSIDevice_GBA::SendCommand(u32 command, u8 poll)
{
}
}  // namespace SerialInterface
