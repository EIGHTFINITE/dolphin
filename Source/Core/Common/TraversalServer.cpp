// This file is public domain, in case it's useful to anyone. -comex

// The central server implementation.
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "Common/Random.h"
#include "Common/TraversalProto.h"

#define DEBUG 0
#define NUMBER_OF_TRIES 5
#define PORT 6262

static u64 currentTime;

struct OutgoingPacketInfo
{
  TraversalPacket packet;
  TraversalRequestId misc;
  sockaddr_in6 dest;
  int tries;
  u64 sendTime;
};

template <typename T>
struct EvictEntry
{
  u64 updateTime;
  T value;
};

template <typename V>
struct EvictFindResult
{
  bool found;
  V* value;
};

template <typename K, typename V>
EvictFindResult<V> EvictFind(std::unordered_map<K, EvictEntry<V>>& map, const K& key,
                             bool refresh = false)
{
retry:
  const u64 expiryTime = 30 * 1000000;  // 30s
  EvictFindResult<V> result;
  if (map.bucket_count())
  {
    auto bucket = map.bucket(key);
    auto it = map.begin(bucket);
    for (; it != map.end(bucket); ++it)
    {
      if (currentTime - it->second.updateTime > expiryTime)
      {
        map.erase(it->first);
        goto retry;
      }
      if (it->first == key)
      {
        if (refresh)
          it->second.updateTime = currentTime;
        result.found = true;
        result.value = &it->second.value;
        return result;
      }
    }
  }
#if DEBUG
  printf("failed to find key '");
  for (size_t i = 0; i < sizeof(key); i++)
  {
    printf("%02x", ((u8*)&key)[i]);
  }
  printf("'\n");
#endif
  result.found = false;
  return result;
}

template <typename K, typename V>
V* EvictSet(std::unordered_map<K, EvictEntry<V>>& map, const K& key)
{
  // can't use a local_iterator to emplace...
  auto& result = map[key];
  result.updateTime = currentTime;
  return &result.value;
}

namespace std
{
template <>
struct hash<TraversalHostId>
{
  size_t operator()(const TraversalHostId& id) const
  {
    auto p = (u32*)id.data();
    return p[0] ^ ((p[1] << 13) | (p[1] >> 19));
  }
};
}  // namespace std

static int sock;
static std::unordered_map<TraversalRequestId, OutgoingPacketInfo> outgoingPackets;
static std::unordered_map<TraversalHostId, EvictEntry<TraversalInetAddress>> connectedClients;

static TraversalInetAddress MakeInetAddress(const sockaddr_in6& addr)
{
  if (addr.sin6_family != AF_INET6)
  {
    fprintf(stderr, "bad sockaddr_in6\n");
    exit(1);
  }
  u32* words = (u32*)addr.sin6_addr.s6_addr;
  TraversalInetAddress result = {0};
  if (words[0] == 0 && words[1] == 0 && words[2] == 0xffff0000)
  {
    result.isIPV6 = false;
    result.address[0] = words[3];
  }
  else
  {
    result.isIPV6 = true;
    memcpy(result.address, words, sizeof(result.address));
  }
  result.port = addr.sin6_port;
  return result;
}

static sockaddr_in6 MakeSinAddr(const TraversalInetAddress& addr)
{
  sockaddr_in6 result;
#ifdef SIN6_LEN
  result.sin6_len = sizeof(result);
#endif
  result.sin6_family = AF_INET6;
  result.sin6_port = addr.port;
  result.sin6_flowinfo = 0;
  if (addr.isIPV6)
  {
    memcpy(&result.sin6_addr, addr.address, 16);
  }
  else
  {
    u32* words = (u32*)result.sin6_addr.s6_addr;
    words[0] = 0;
    words[1] = 0;
    words[2] = 0xffff0000;
    words[3] = addr.address[0];
  }
  result.sin6_scope_id = 0;
  return result;
}

static void GetRandomHostId(TraversalHostId* hostId)
{
  char buf[9];
  const u32 num = Common::Random::GenerateValue<u32>();
  sprintf(buf, "%08x", num);
  memcpy(hostId->data(), buf, 8);
}

static const char* SenderName(sockaddr_in6* addr)
{
  static char buf[INET6_ADDRSTRLEN + 10];
  inet_ntop(PF_INET6, &addr->sin6_addr, buf, sizeof(buf));
  sprintf(buf + strlen(buf), ":%d", ntohs(addr->sin6_port));
  return buf;
}

static void TrySend(const void* buffer, size_t size, sockaddr_in6* addr)
{
#if DEBUG
  const auto* packet = static_cast<const TraversalPacket*>(buffer);
  printf("-> %d %llu %s\n", static_cast<int>(packet->type),
         static_cast<long long>(packet->requestId), SenderName(addr));
#endif
  if ((size_t)sendto(sock, buffer, size, 0, (sockaddr*)addr, sizeof(*addr)) != size)
  {
    perror("sendto");
  }
}

static TraversalPacket* AllocPacket(const sockaddr_in6& dest, TraversalRequestId misc = 0)
{
  TraversalRequestId requestId;
  Common::Random::Generate(&requestId, sizeof(requestId));
  OutgoingPacketInfo* info = &outgoingPackets[requestId];
  info->dest = dest;
  info->misc = misc;
  info->tries = 0;
  info->sendTime = currentTime;
  TraversalPacket* result = &info->packet;
  memset(result, 0, sizeof(*result));
  result->requestId = requestId;
  return result;
}

static void SendPacket(OutgoingPacketInfo* info)
{
  info->tries++;
  info->sendTime = currentTime;
  TrySend(&info->packet, sizeof(info->packet), &info->dest);
}

static void ResendPackets()
{
  std::vector<std::pair<TraversalInetAddress, TraversalRequestId>> todoFailures;
  todoFailures.clear();
  for (auto it = outgoingPackets.begin(); it != outgoingPackets.end();)
  {
    OutgoingPacketInfo* info = &it->second;
    if (currentTime - info->sendTime >= (u64)(300000 * info->tries))
    {
      if (info->tries >= NUMBER_OF_TRIES)
      {
        if (info->packet.type == TraversalPacketType::PleaseSendPacket)
        {
          todoFailures.push_back(std::make_pair(info->packet.pleaseSendPacket.address, info->misc));
        }
        it = outgoingPackets.erase(it);
        continue;
      }
      else
      {
        SendPacket(info);
      }
    }
    ++it;
  }

  for (const auto& p : todoFailures)
  {
    TraversalPacket* fail = AllocPacket(MakeSinAddr(p.first));
    fail->type = TraversalPacketType::ConnectFailed;
    fail->connectFailed.requestId = p.second;
    fail->connectFailed.reason = TraversalConnectFailedReason::ClientDidntRespond;
  }
}

static void HandlePacket(TraversalPacket* packet, sockaddr_in6* addr)
{
#if DEBUG
  printf("<- %d %llu %s\n", static_cast<int>(packet->type),
         static_cast<long long>(packet->requestId), SenderName(addr));
#endif
  bool packetOk = true;
  switch (packet->type)
  {
  case TraversalPacketType::Ack:
  {
    auto it = outgoingPackets.find(packet->requestId);
    if (it == outgoingPackets.end())
      break;

    OutgoingPacketInfo* info = &it->second;

    if (info->packet.type == TraversalPacketType::PleaseSendPacket)
    {
      TraversalPacket* ready = AllocPacket(MakeSinAddr(info->packet.pleaseSendPacket.address));
      if (packet->ack.ok)
      {
        ready->type = TraversalPacketType::ConnectReady;
        ready->connectReady.requestId = info->misc;
        ready->connectReady.address = MakeInetAddress(info->dest);
      }
      else
      {
        ready->type = TraversalPacketType::ConnectFailed;
        ready->connectFailed.requestId = info->misc;
        ready->connectFailed.reason = TraversalConnectFailedReason::ClientFailure;
      }
    }

    outgoingPackets.erase(it);
    break;
  }
  case TraversalPacketType::Ping:
  {
    auto r = EvictFind(connectedClients, packet->ping.hostId, true);
    packetOk = r.found;
    break;
  }
  case TraversalPacketType::HelloFromClient:
  {
    u8 ok = packet->helloFromClient.protoVersion <= TraversalProtoVersion;
    TraversalPacket* reply = AllocPacket(*addr);
    reply->type = TraversalPacketType::HelloFromServer;
    reply->helloFromServer.ok = ok;
    if (ok)
    {
      TraversalHostId hostId;
      TraversalInetAddress* iaddr;
      // not that there is any significant change of
      // duplication, but...
      GetRandomHostId(&hostId);
      while (true)
      {
        auto r = EvictFind(connectedClients, hostId);
        if (!r.found)
        {
          iaddr = EvictSet(connectedClients, hostId);
          break;
        }
      }

      *iaddr = MakeInetAddress(*addr);

      reply->helloFromServer.yourAddress = *iaddr;
      reply->helloFromServer.yourHostId = hostId;
    }
    break;
  }
  case TraversalPacketType::ConnectPlease:
  {
    TraversalHostId& hostId = packet->connectPlease.hostId;
    auto r = EvictFind(connectedClients, hostId);
    if (!r.found)
    {
      TraversalPacket* reply = AllocPacket(*addr);
      reply->type = TraversalPacketType::ConnectFailed;
      reply->connectFailed.requestId = packet->requestId;
      reply->connectFailed.reason = TraversalConnectFailedReason::NoSuchClient;
    }
    else
    {
      TraversalPacket* please = AllocPacket(MakeSinAddr(*r.value), packet->requestId);
      please->type = TraversalPacketType::PleaseSendPacket;
      please->pleaseSendPacket.address = MakeInetAddress(*addr);
    }
    break;
  }
  default:
    fprintf(stderr, "received unknown packet type %d from %s\n", static_cast<int>(packet->type),
            SenderName(addr));
    break;
  }
  if (packet->type != TraversalPacketType::Ack)
  {
    TraversalPacket ack = {};
    ack.type = TraversalPacketType::Ack;
    ack.requestId = packet->requestId;
    ack.ack.ok = packetOk;
    TrySend(&ack, sizeof(ack), addr);
  }
}

int main()
{
  int rv;
  sock = socket(PF_INET6, SOCK_DGRAM, 0);
  if (sock == -1)
  {
    perror("socket");
    return 1;
  }
  int no = 0;
  rv = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
  if (rv < 0)
  {
    perror("setsockopt IPV6_V6ONLY");
    return 1;
  }
  in6_addr any = IN6ADDR_ANY_INIT;
  sockaddr_in6 addr;
#ifdef SIN6_LEN
  addr.sin6_len = sizeof(addr);
#endif
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(PORT);
  addr.sin6_flowinfo = 0;
  addr.sin6_addr = any;
  addr.sin6_scope_id = 0;

  rv = bind(sock, (sockaddr*)&addr, sizeof(addr));
  if (rv < 0)
  {
    perror("bind");
    return 1;
  }

  timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 300000;
  rv = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  if (rv < 0)
  {
    perror("setsockopt SO_RCVTIMEO");
    return 1;
  }

#ifdef HAVE_LIBSYSTEMD
  sd_notifyf(0, "READY=1\nSTATUS=Listening on port %d", PORT);
#endif

  while (true)
  {
    sockaddr_in6 raddr;
    socklen_t addrLen = sizeof(raddr);
    TraversalPacket packet;
    // note: switch to recvmmsg (yes, mmsg) if this becomes
    // expensive
    rv = recvfrom(sock, &packet, sizeof(packet), 0, (sockaddr*)&raddr, &addrLen);
    currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    if (rv < 0)
    {
      if (errno != EINTR && errno != EAGAIN)
      {
        perror("recvfrom");
        return 1;
      }
    }
    else if ((size_t)rv < sizeof(packet))
    {
      fprintf(stderr, "received short packet from %s\n", SenderName(&raddr));
    }
    else
    {
      HandlePacket(&packet, &raddr);
    }
    ResendPackets();
#ifdef HAVE_LIBSYSTEMD
    sd_notify(0, "WATCHDOG=1");
#endif
  }
}
