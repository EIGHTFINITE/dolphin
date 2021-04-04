// This file is public domain, in case it's useful to anyone. -comex

#include "Common/TraversalClient.h"

#include <cstddef>
#include <cstring>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Random.h"
#include "Core/NetPlayProto.h"

TraversalClient::TraversalClient(ENetHost* netHost, const std::string& server, const u16 port)
    : m_NetHost(netHost), m_Server(server), m_port(port)
{
  netHost->intercept = TraversalClient::InterceptCallback;

  Reset();

  ReconnectToServer();
}

TraversalClient::~TraversalClient() = default;

TraversalHostId TraversalClient::GetHostID() const
{
  return m_HostId;
}

TraversalClient::State TraversalClient::GetState() const
{
  return m_State;
}

TraversalClient::FailureReason TraversalClient::GetFailureReason() const
{
  return m_FailureReason;
}

void TraversalClient::ReconnectToServer()
{
  if (enet_address_set_host(&m_ServerAddress, m_Server.c_str()))
  {
    OnFailure(FailureReason::BadHost);
    return;
  }
  m_ServerAddress.port = m_port;

  m_State = State::Connecting;

  TraversalPacket hello = {};
  hello.type = TraversalPacketType::HelloFromClient;
  hello.helloFromClient.protoVersion = TraversalProtoVersion;
  SendTraversalPacket(hello);
  if (m_Client)
    m_Client->OnTraversalStateChanged();
}

static ENetAddress MakeENetAddress(const TraversalInetAddress& address)
{
  ENetAddress eaddr{};
  if (address.isIPV6)
  {
    eaddr.port = 0;  // no support yet :(
  }
  else
  {
    eaddr.host = address.address[0];
    eaddr.port = ntohs(address.port);
  }
  return eaddr;
}

void TraversalClient::ConnectToClient(std::string_view host)
{
  if (host.size() > sizeof(TraversalHostId))
  {
    PanicAlertFmt("Host too long");
    return;
  }
  TraversalPacket packet = {};
  packet.type = TraversalPacketType::ConnectPlease;
  memcpy(packet.connectPlease.hostId.data(), host.data(), host.size());
  m_ConnectRequestId = SendTraversalPacket(packet);
  m_PendingConnect = true;
}

bool TraversalClient::TestPacket(u8* data, size_t size, ENetAddress* from)
{
  if (from->host == m_ServerAddress.host && from->port == m_ServerAddress.port)
  {
    if (size < sizeof(TraversalPacket))
    {
      ERROR_LOG_FMT(NETPLAY, "Received too-short traversal packet.");
    }
    else
    {
      HandleServerPacket((TraversalPacket*)data);
      return true;
    }
  }
  return false;
}

//--Temporary until more of the old netplay branch is moved over
void TraversalClient::Update()
{
  ENetEvent netEvent;
  if (enet_host_service(m_NetHost, &netEvent, 4) > 0)
  {
    switch (netEvent.type)
    {
    case ENET_EVENT_TYPE_RECEIVE:
      TestPacket(netEvent.packet->data, netEvent.packet->dataLength, &netEvent.peer->address);

      enet_packet_destroy(netEvent.packet);
      break;
    default:
      break;
    }
  }
  HandleResends();
}

void TraversalClient::HandleServerPacket(TraversalPacket* packet)
{
  u8 ok = 1;
  switch (packet->type)
  {
  case TraversalPacketType::Ack:
    if (!packet->ack.ok)
    {
      OnFailure(FailureReason::ServerForgotAboutUs);
      break;
    }
    for (auto it = m_OutgoingTraversalPackets.begin(); it != m_OutgoingTraversalPackets.end(); ++it)
    {
      if (it->packet.requestId == packet->requestId)
      {
        m_OutgoingTraversalPackets.erase(it);
        break;
      }
    }
    break;
  case TraversalPacketType::HelloFromServer:
    if (!IsConnecting())
      break;
    if (!packet->helloFromServer.ok)
    {
      OnFailure(FailureReason::VersionTooOld);
      break;
    }
    m_HostId = packet->helloFromServer.yourHostId;
    m_State = State::Connected;
    if (m_Client)
      m_Client->OnTraversalStateChanged();
    break;
  case TraversalPacketType::PleaseSendPacket:
  {
    // security is overrated.
    ENetAddress addr = MakeENetAddress(packet->pleaseSendPacket.address);
    if (addr.port != 0)
    {
      char message[] = "Hello from Dolphin Netplay...";
      ENetBuffer buf;
      buf.data = message;
      buf.dataLength = sizeof(message) - 1;
      enet_socket_send(m_NetHost->socket, &addr, &buf, 1);
    }
    else
    {
      // invalid IPV6
      ok = 0;
    }
    break;
  }
  case TraversalPacketType::ConnectReady:
  case TraversalPacketType::ConnectFailed:
  {
    if (!m_PendingConnect || packet->connectReady.requestId != m_ConnectRequestId)
      break;

    m_PendingConnect = false;

    if (!m_Client)
      break;

    if (packet->type == TraversalPacketType::ConnectReady)
      m_Client->OnConnectReady(MakeENetAddress(packet->connectReady.address));
    else
      m_Client->OnConnectFailed(packet->connectFailed.reason);
    break;
  }
  default:
    WARN_LOG_FMT(NETPLAY, "Received unknown packet with type {}", packet->type);
    break;
  }
  if (packet->type != TraversalPacketType::Ack)
  {
    TraversalPacket ack = {};
    ack.type = TraversalPacketType::Ack;
    ack.requestId = packet->requestId;
    ack.ack.ok = ok;

    ENetBuffer buf;
    buf.data = &ack;
    buf.dataLength = sizeof(ack);
    if (enet_socket_send(m_NetHost->socket, &m_ServerAddress, &buf, 1) == -1)
      OnFailure(FailureReason::SocketSendError);
  }
}

void TraversalClient::OnFailure(FailureReason reason)
{
  m_State = State::Failure;
  m_FailureReason = reason;

  if (m_Client)
    m_Client->OnTraversalStateChanged();
}

void TraversalClient::ResendPacket(OutgoingTraversalPacketInfo* info)
{
  info->sendTime = enet_time_get();
  info->tries++;
  ENetBuffer buf;
  buf.data = &info->packet;
  buf.dataLength = sizeof(info->packet);
  if (enet_socket_send(m_NetHost->socket, &m_ServerAddress, &buf, 1) == -1)
    OnFailure(FailureReason::SocketSendError);
}

void TraversalClient::HandleResends()
{
  const u32 now = enet_time_get();
  for (auto& tpi : m_OutgoingTraversalPackets)
  {
    if (now - tpi.sendTime >= (u32)(300 * tpi.tries))
    {
      if (tpi.tries >= 5)
      {
        OnFailure(FailureReason::ResendTimeout);
        m_OutgoingTraversalPackets.clear();
        break;
      }
      else
      {
        ResendPacket(&tpi);
      }
    }
  }
  HandlePing();
}

void TraversalClient::HandlePing()
{
  const u32 now = enet_time_get();
  if (IsConnected() && now - m_PingTime >= 500)
  {
    TraversalPacket ping = {};
    ping.type = TraversalPacketType::Ping;
    ping.ping.hostId = m_HostId;
    SendTraversalPacket(ping);
    m_PingTime = now;
  }
}

TraversalRequestId TraversalClient::SendTraversalPacket(const TraversalPacket& packet)
{
  OutgoingTraversalPacketInfo info;
  info.packet = packet;
  info.packet.requestId = Common::Random::GenerateValue<TraversalRequestId>();
  info.tries = 0;
  m_OutgoingTraversalPackets.push_back(info);
  ResendPacket(&m_OutgoingTraversalPackets.back());
  return info.packet.requestId;
}

void TraversalClient::Reset()
{
  m_PendingConnect = false;
  m_Client = nullptr;
}

int ENET_CALLBACK TraversalClient::InterceptCallback(ENetHost* host, ENetEvent* event)
{
  auto traversalClient = g_TraversalClient.get();
  if (traversalClient->TestPacket(host->receivedData, host->receivedDataLength,
                                  &host->receivedAddress) ||
      (host->receivedDataLength == 1 && host->receivedData[0] == 0))
  {
    event->type = (ENetEventType)42;
    return 1;
  }
  return 0;
}

std::unique_ptr<TraversalClient> g_TraversalClient;
std::unique_ptr<ENetHost> g_MainNetHost;

// The settings at the previous TraversalClient reset - notably, we
// need to know not just what port it's on, but whether it was
// explicitly requested.
static std::string g_OldServer;
static u16 g_OldServerPort;
static u16 g_OldListenPort;

bool EnsureTraversalClient(const std::string& server, u16 server_port, u16 listen_port)
{
  if (!g_MainNetHost || !g_TraversalClient || server != g_OldServer ||
      server_port != g_OldServerPort || listen_port != g_OldListenPort)
  {
    g_OldServer = server;
    g_OldServerPort = server_port;
    g_OldListenPort = listen_port;

    ENetAddress addr = {ENET_HOST_ANY, listen_port};
    ENetHost* host = enet_host_create(&addr,                   // address
                                      50,                      // peerCount
                                      NetPlay::CHANNEL_COUNT,  // channelLimit
                                      0,                       // incomingBandwidth
                                      0);                      // outgoingBandwidth
    if (!host)
    {
      g_MainNetHost.reset();
      return false;
    }
    g_MainNetHost.reset(host);
    g_TraversalClient.reset(new TraversalClient(g_MainNetHost.get(), server, server_port));
  }
  return true;
}

void ReleaseTraversalClient()
{
  if (!g_TraversalClient)
    return;

  g_TraversalClient.reset();
  g_MainNetHost.reset();
}
